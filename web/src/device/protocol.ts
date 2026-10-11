import {
  type BootBootsSystemStatus,
  type CameraSettings,
  DEFAULT_CAMERA_SETTINGS,
  type DeviceCommand,
  type DeviceResponse,
  type PeripheralState,
} from './types';

/** Chunks keyed by their index, so transfers survive out-of-order delivery (MQTT goes via Lambda). */
type Chunks = Record<number, string>;

export interface Progress {
  current: number;
  total: number;
}

export interface DeviceState {
  status: BootBootsSystemStatus | null;
  lastUpdate: number | null;
  version: string | null;
  error: string | null;
  logs: { chunks: Chunks; next: number; text: string; loading: boolean };
  previousLogs: { files: string[]; loading: boolean };
  imageList: {
    list: string[];
    chunks: Chunks;
    expected: number | null;
    loading: boolean;
    progress: Progress | null;
  };
  image: {
    selected: string;
    loading: boolean;
    chunks: Chunks;
    expected: number | null;
    receiving: string | null;
    progress: Progress | null;
    current: string | null;
    metadata: string | null;
    /** Filename whose metadata is awaited, so image + metadata can be cached together. */
    awaitingMetadata: string | null;
  };
  pendingPhoto: string | null;
  takingPhoto: boolean;
  settings: { trainingMode: boolean; dryRun: boolean; triggerThresh: number; claudeInfer: boolean };
  camera: CameraSettings;
  updatingSetting: boolean;
  rebooting: boolean;
  peripherals: PeripheralState;
  peripheralPending: string | null;
  simulatePending: boolean;
  responseLog: string[];
}

export interface CacheEntry {
  imageData: string;
  metadata: string | null;
}

export interface Effects {
  commands: DeviceCommand[];
  cache?: { filename: string; entry: CacheEntry };
}

export interface Reduction {
  state: DeviceState;
  effects: Effects;
}

export const initialDeviceState = (): DeviceState => ({
  status: null,
  lastUpdate: null,
  version: null,
  error: null,
  logs: { chunks: {}, next: 0, text: '', loading: false },
  previousLogs: { files: [], loading: false },
  imageList: { list: [], chunks: {}, expected: null, loading: false, progress: null },
  image: {
    selected: '',
    loading: false,
    chunks: {},
    expected: null,
    receiving: null,
    progress: null,
    current: null,
    metadata: null,
    awaitingMetadata: null,
  },
  pendingPhoto: null,
  takingPhoto: false,
  settings: { trainingMode: false, dryRun: false, triggerThresh: 0.8, claudeInfer: false },
  camera: DEFAULT_CAMERA_SETTINGS,
  updatingSetting: false,
  rebooting: false,
  peripherals: { pir_active: false, flash_led_on: false, led_strip_on: false, spray_on: false },
  peripheralPending: null,
  simulatePending: false,
  responseLog: [],
});

const MAX_RESPONSE_LOG = 50;
const PERIPHERAL_FIELDS: Record<string, keyof PeripheralState> = {
  flash_led: 'flash_led_on',
  led_strip: 'led_strip_on',
  spray: 'spray_on',
};

const joinChunks = (chunks: Chunks, separator: string) =>
  Object.keys(chunks)
    .map(Number)
    .sort((a, b) => a - b)
    .map((k) => chunks[k])
    .join(separator);

const count = (chunks: Chunks) => Object.keys(chunks).length;
const num = (v: unknown): number | undefined => (typeof v === 'number' ? v : undefined);
const str = (v: unknown): string | undefined => (typeof v === 'string' ? v : undefined);

/** Starts fetching an image from the device. */
export function requestImage(state: DeviceState, filename: string): DeviceState {
  return {
    ...state,
    error: null,
    image: {
      ...state.image,
      selected: filename,
      loading: true,
      chunks: {},
      expected: null,
      receiving: filename,
      progress: null,
      current: null,
      metadata: null,
      awaitingMetadata: null,
    },
  };
}

function finishImageList(state: DeviceState, commands: DeviceCommand[]): DeviceState {
  const { imageList } = state;
  if (imageList.expected === null || count(imageList.chunks) < imageList.expected) return state;
  const list = joinChunks(imageList.chunks, '\n').split('\n').filter(Boolean);
  let next: DeviceState = {
    ...state,
    imageList: { list, chunks: {}, expected: null, loading: false, progress: null },
  };
  // A photo just taken: show it as soon as it appears in the list.
  if (state.pendingPhoto && list.includes(state.pendingPhoto)) {
    commands.push({ command: 'get_image', filename: state.pendingPhoto });
    next = { ...requestImage(next, state.pendingPhoto), pendingPhoto: null };
  }
  return next;
}

function finishImage(state: DeviceState, commands: DeviceCommand[]): DeviceState {
  const { image } = state;
  if (image.expected === null || count(image.chunks) < image.expected || !image.receiving) return state;
  const filename = image.receiving;
  commands.push({ command: 'get_image_metadata', filename });
  return {
    ...state,
    image: {
      ...image,
      current: `data:image/jpeg;base64,${joinChunks(image.chunks, '')}`,
      chunks: {},
      expected: null,
      receiving: null,
      progress: null,
      awaitingMetadata: filename,
    },
  };
}

function withLog(state: DeviceState, response: unknown, at: number): DeviceState {
  const type = Array.isArray(response) ? 'logs' : (str((response as DeviceResponse).type) ?? 'response');
  const line = `[${new Date(at).toLocaleTimeString('en-GB')}] ${type}: ${JSON.stringify(response).substring(0, 100)}...`;
  return { ...state, responseLog: [line, ...state.responseLog.slice(0, MAX_RESPONSE_LOG - 1)] };
}

/** Applies a status report (BLE status characteristic, MQTT `status` response or status channel). */
export function applyStatus(state: DeviceState, status: BootBootsSystemStatus, at: number): DeviceState {
  return {
    ...state,
    status,
    lastUpdate: at,
    error: null,
    peripherals: status.peripherals ? { ...status.peripherals } : state.peripherals,
  };
}

/** Folds one device response into the state, returning follow-up commands for the transport. */
export function reduceResponse(previous: DeviceState, raw: unknown, at: number): Reduction {
  const commands: DeviceCommand[] = [];
  const effects: Effects = { commands };
  let state = withLog(previous, raw, at);

  // Legacy firmware sends the whole log as one JSON array.
  if (Array.isArray(raw)) {
    state = { ...state, logs: { ...state.logs, text: JSON.stringify(raw), loading: false } };
    return { state, effects };
  }
  const r = raw as DeviceResponse;

  if (r.response === 'pong' || r.type === 'pong') return { state: { ...state, error: null }, effects };

  switch (r.type) {
    case 'log_chunk':
    case 'log_file_chunk': {
      const index = num(r.chunk) ?? state.logs.next;
      const chunks = { ...state.logs.chunks, [index]: str(r.data) ?? '' };
      state = {
        ...state,
        logs: {
          ...state.logs,
          chunks,
          next: Math.max(state.logs.next, index + 1),
          text: joinChunks(chunks, '\n'),
        },
      };
      break;
    }
    case 'logs_complete':
    case 'log_file_complete':
      state = { ...state, logs: { ...state.logs, loading: false } };
      break;
    case 'log_list_debug':
      console.warn(`Log list debug: ${r.message}`);
      break;
    case 'log_list_chunk': {
      const filename = str(r.filename);
      if (filename && !state.previousLogs.files.includes(filename)) {
        state = {
          ...state,
          previousLogs: { ...state.previousLogs, files: [...state.previousLogs.files, filename] },
        };
      }
      break;
    }
    case 'log_list_complete':
      state = { ...state, previousLogs: { ...state.previousLogs, loading: false } };
      break;

    case 'image_list':
      state = {
        ...state,
        imageList: { ...state.imageList, list: (r.images as string[]) ?? [], loading: false, progress: null },
      };
      break;
    case 'image_list_chunk': {
      const index = num(r.chunk) ?? count(state.imageList.chunks);
      const chunks = { ...state.imageList.chunks, [index]: str(r.filename) ?? '' };
      const total = num(r.total) ?? 0;
      state = {
        ...state,
        imageList: { ...state.imageList, chunks, progress: { current: count(chunks), total } },
      };
      state = finishImageList(state, commands);
      break;
    }
    case 'image_list_complete':
      state = finishImageList(
        {
          ...state,
          imageList: { ...state.imageList, expected: num(r.count) ?? count(state.imageList.chunks) },
        },
        commands,
      );
      break;

    case 'image_start':
      state = {
        ...state,
        image: {
          ...state.image,
          receiving: str(r.filename) ?? state.image.receiving,
          progress: state.image.progress ?? { current: 0, total: 0 },
        },
      };
      break;
    case 'image_chunk': {
      const index = num(r.chunk) ?? count(state.image.chunks);
      const chunks = { ...state.image.chunks, [index]: str(r.data) ?? '' };
      state = {
        ...state,
        image: { ...state.image, chunks, progress: { current: count(chunks), total: num(r.total) ?? 0 } },
      };
      state = finishImage(state, commands);
      break;
    }
    case 'image_complete':
      state = finishImage(
        {
          ...state,
          image: {
            ...state.image,
            receiving: str(r.filename) ?? state.image.receiving,
            expected: num(r.chunks) ?? num(r.total) ?? count(state.image.chunks),
          },
        },
        commands,
      );
      break;
    case 'metadata_result': {
      const metadata = r.found ? (str(r.content) ?? null) : null;
      const filename = str(r.filename);
      if (filename && filename === state.image.awaitingMetadata && state.image.current) {
        effects.cache = { filename, entry: { imageData: state.image.current, metadata } };
      }
      state = { ...state, image: { ...state.image, metadata, loading: false, awaitingMetadata: null } };
      break;
    }

    case 'error':
      console.error('Error from device:', r.message);
      state = {
        ...state,
        error: str(r.message) ?? 'Device reported an error',
        logs: { ...state.logs, loading: false },
        imageList: { ...state.imageList, loading: false },
        image: { ...state.image, loading: false },
        takingPhoto: false,
        updatingSetting: false,
        peripheralPending: null,
      };
      break;

    case 'photo_started':
      state = { ...state, takingPhoto: true };
      break;
    case 'photo_complete': {
      state = { ...state, takingPhoto: false };
      const filename = str(r.filename);
      if (filename) {
        commands.push({ command: 'list_images' });
        state = {
          ...state,
          pendingPhoto: filename,
          imageList: { ...state.imageList, chunks: {}, expected: null, loading: true, progress: null },
        };
      }
      break;
    }

    case 'simulation_started':
      state = { ...state, simulatePending: true };
      break;
    case 'simulation_complete':
      state = { ...state, simulatePending: false };
      break;

    case 'settings':
      state = {
        ...state,
        settings: {
          trainingMode: (r.training_mode as boolean) || false,
          dryRun: (r.dry_run as boolean) || false,
          triggerThresh: num(r.trigger_threshold) ?? 0.8,
          claudeInfer: (r.claude_infer as boolean) || false,
        },
        camera: r.camera ? { ...state.camera, ...(r.camera as Partial<CameraSettings>) } : state.camera,
      };
      break;
    case 'camera_settings':
      if (r.camera)
        state = { ...state, camera: { ...state.camera, ...(r.camera as Partial<CameraSettings>) } };
      break;
    case 'setting_updated': {
      const setting = str(r.setting) ?? '';
      const s = state.settings;
      if (setting === 'training_mode') state = { ...state, settings: { ...s, trainingMode: !!r.value } };
      else if (setting === 'dry_run') state = { ...state, settings: { ...s, dryRun: !!r.value } };
      else if (setting === 'trigger_threshold')
        state = { ...state, settings: { ...s, triggerThresh: num(r.value) ?? s.triggerThresh } };
      else if (setting === 'claude_infer') state = { ...state, settings: { ...s, claudeInfer: !!r.value } };
      else if (setting.startsWith('camera_'))
        state = { ...state, camera: { ...state.camera, [setting.substring(7)]: r.value } };
      state = { ...state, updatingSetting: false };
      break;
    }

    case 'reboot_ack':
      state = { ...state, rebooting: false };
      break;

    case 'peripheral_updated': {
      const field = PERIPHERAL_FIELDS[str(r.peripheral) ?? ''];
      state = {
        ...state,
        peripheralPending: null,
        peripherals: field ? { ...state.peripherals, [field]: !!r.state } : state.peripherals,
      };
      break;
    }

    case 'status':
      // Kappa-Warmer shares the protocol but isn't a catcam.
      if (r.device !== 'Kappa-Warmer') state = applyStatus(state, r as unknown as BootBootsSystemStatus, at);
      break;

    case 'version':
      state = { ...state, version: str(r.version) ?? state.version };
      break;
  }
  return { state, effects };
}
