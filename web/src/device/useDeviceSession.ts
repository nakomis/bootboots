import { useCallback, useMemo, useReducer, useRef } from 'react';
import {
  applyStatus,
  type CacheEntry,
  type DeviceState,
  initialDeviceState,
  reduceResponse,
  requestImage,
} from './protocol';
import type { BootBootsSystemStatus, DeviceCommand, DeviceTransport } from './types';

const IMAGE_CACHE_SIZE = 20;

/** A small LRU of fetched images and their inference metadata. */
export class ImageCache {
  private entries = new Map<string, CacheEntry>();
  constructor(private readonly max = IMAGE_CACHE_SIZE) {}
  get(key: string) {
    const entry = this.entries.get(key);
    if (entry) {
      this.entries.delete(key);
      this.entries.set(key, entry);
    }
    return entry;
  }
  set(key: string, entry: CacheEntry) {
    this.entries.delete(key);
    this.entries.set(key, entry);
    if (this.entries.size > this.max) this.entries.delete(this.entries.keys().next().value as string);
  }
}

const message = (err: unknown) => (err instanceof Error ? err.message : String(err));

/**
 * Device state and actions shared by the Bluetooth and remote pages. The page wires responses from its
 * transport into `receive` / `receiveStatus`; actions send commands through `transport`.
 */
export function useDeviceSession(transport: DeviceTransport | null, now: () => number = Date.now) {
  const stateRef = useRef<DeviceState>(initialDeviceState());
  const transportRef = useRef(transport);
  transportRef.current = transport;
  const cacheRef = useRef(new ImageCache());
  const [, rerender] = useReducer((n: number) => n + 1, 0);

  const update = useCallback((fn: (s: DeviceState) => DeviceState) => {
    stateRef.current = fn(stateRef.current);
    rerender();
  }, []);

  /** Sends a command; on failure shows the error and applies `rollback`. */
  const send = useCallback(
    async (command: DeviceCommand, what: string, rollback: (s: DeviceState) => DeviceState = (s) => s) => {
      const t = transportRef.current;
      try {
        if (!t) throw new Error('Not connected');
        await t.send(command);
      } catch (err) {
        console.error(`Failed to ${what}:`, err);
        update((s) => ({ ...rollback(s), error: `Failed to ${what}: ${message(err)}` }));
      }
    },
    [update],
  );

  const receive = useCallback(
    (response: unknown) => {
      const { state, effects } = reduceResponse(stateRef.current, response, now());
      stateRef.current = state;
      if (effects.cache) cacheRef.current.set(effects.cache.filename, effects.cache.entry);
      rerender();
      for (const command of effects.commands) void send(command, `send ${command.command}`);
    },
    [now, send],
  );

  const receiveStatus = useCallback(
    (status: BootBootsSystemStatus) => update((s) => applyStatus(s, status, now())),
    [now, update],
  );

  const reset = useCallback(() => update(() => initialDeviceState()), [update]);

  const actions = useMemo(
    () => ({
      clearError: () => update((s) => ({ ...s, error: null })),
      ping: () => send({ command: 'ping' }, 'send ping'),
      // Always ask with get_status: it reports live state. The BLE status characteristic is only set at
      // boot (firmware 1.0.331 never refreshes it), so reading it shows stale peripherals.
      requestStatus: () => send({ command: 'get_status' }, 'request status'),
      requestSettings: async () => {
        await send({ command: 'get_settings' }, 'request settings');
        await send({ command: 'get_camera_settings' }, 'request camera settings');
      },
      getVersion: () => send({ command: 'get_version' }, 'request version'),
      /** `entries` limits to the most recent N log lines; omit for the whole file. */
      requestLogs: (entries?: number) => {
        update((s) => ({ ...s, error: null, logs: { chunks: {}, next: 0, text: '', loading: true } }));
        return send({ command: 'request_logs', ...(entries ? { entries } : {}) }, 'request logs', (s) => ({
          ...s,
          logs: { ...s.logs, loading: false },
        }));
      },
      listPreviousLogs: () => {
        update((s) => ({ ...s, error: null, previousLogs: { files: [], loading: true } }));
        return send({ command: 'list_logs' }, 'request log list', (s) => ({
          ...s,
          previousLogs: { ...s.previousLogs, loading: false },
        }));
      },
      getLogFile: (filename: string, entries?: number) => {
        update((s) => ({ ...s, error: null, logs: { chunks: {}, next: 0, text: '', loading: true } }));
        return send(
          { command: 'get_log_file', filename, ...(entries ? { entries } : {}) },
          'request log file',
          (s) => ({ ...s, logs: { ...s.logs, loading: false } }),
        );
      },
      listImages: () => {
        update((s) => ({
          ...s,
          error: null,
          imageList: { ...s.imageList, chunks: {}, expected: null, loading: true, progress: null },
        }));
        return send({ command: 'list_images' }, 'request image list', (s) => ({
          ...s,
          imageList: { ...s.imageList, loading: false },
        }));
      },
      selectImage: async (filename: string) => {
        if (!filename) {
          update((s) => ({ ...s, image: { ...s.image, selected: '', current: null, metadata: null } }));
          return;
        }
        const cached = cacheRef.current.get(filename);
        if (cached) {
          update((s) => ({
            ...s,
            image: {
              ...s.image,
              selected: filename,
              current: cached.imageData,
              metadata: cached.metadata,
              loading: false,
            },
          }));
          return;
        }
        update((s) => requestImage(s, filename));
        await send({ command: 'get_image', filename }, 'request image', (s) => ({
          ...s,
          image: { ...s.image, loading: false },
        }));
      },
      takePhoto: () => {
        update((s) => ({ ...s, error: null, takingPhoto: true }));
        return send({ command: 'take_photo' }, 'take photo', (s) => ({ ...s, takingPhoto: false }));
      },
      toggleTrainingMode: () => {
        const value = !stateRef.current.settings.trainingMode;
        update((s) => ({ ...s, updatingSetting: true }));
        return send(
          { command: 'set_setting', setting: 'training_mode', value },
          'toggle training mode',
          (s) => ({
            ...s,
            updatingSetting: false,
          }),
        );
      },
      toggleDryRun: () => {
        const enabled = !stateRef.current.settings.dryRun;
        update((s) => ({ ...s, updatingSetting: true }));
        return send({ command: 'set_dry_run', enabled }, 'toggle dry run', (s) => ({
          ...s,
          updatingSetting: false,
        }));
      },
      setTriggerThresh: (value: number) => {
        update((s) => ({ ...s, updatingSetting: true }));
        return send({ command: 'set_trigger_threshold', value }, 'set trigger threshold', (s) => ({
          ...s,
          updatingSetting: false,
        }));
      },
      toggleClaudeInfer: () => {
        const enabled = !stateRef.current.settings.claudeInfer;
        update((s) => ({ ...s, updatingSetting: true }));
        return send({ command: 'set_claude_infer', enabled }, 'toggle Claude inference', (s) => ({
          ...s,
          updatingSetting: false,
        }));
      },
      setCameraSetting: (setting: string, value: number | boolean) => {
        update((s) => ({ ...s, updatingSetting: true }));
        return send(
          { command: 'set_setting', setting: `camera_${setting}`, value },
          'update camera setting',
          (s) => ({
            ...s,
            updatingSetting: false,
          }),
        );
      },
      reboot: () => {
        update((s) => ({ ...s, error: null, rebooting: true }));
        return send({ command: 'reboot' }, 'reboot device', (s) => ({ ...s, rebooting: false }));
      },
      setPeripheral: (peripheral: string, state: boolean) => {
        update((s) => ({ ...s, peripheralPending: peripheral }));
        return send({ command: 'set_peripheral', peripheral, state }, 'control peripheral', (s) => ({
          ...s,
          peripheralPending: null,
        }));
      },
      simulateDetection: async () => {
        if (stateRef.current.simulatePending) return;
        await send({ command: 'simulate_detection' }, 'simulate detection', (s) => ({
          ...s,
          simulatePending: false,
        }));
      },
    }),
    [send, update],
  );

  return { state: stateRef.current, receive, receiveStatus, reset, actions };
}

export type DeviceSession = ReturnType<typeof useDeviceSession>;
export type DeviceActions = DeviceSession['actions'];
