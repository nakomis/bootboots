import { applyStatus, type DeviceState, initialDeviceState, reduceResponse, requestImage } from './protocol';
import type { BootBootsSystemStatus } from './types';

const AT = Date.UTC(2026, 9, 11, 12, 0, 0);

/** Feeds responses through the reducer, collecting follow-up commands. */
function run(state: DeviceState, ...responses: unknown[]) {
  const commands: unknown[] = [];
  let cache: unknown;
  for (const r of responses) {
    const out = reduceResponse(state, r, AT);
    state = out.state;
    commands.push(...out.effects.commands);
    if (out.effects.cache) cache = out.effects.cache;
  }
  return { state, commands, cache };
}

const status: BootBootsSystemStatus = {
  device: 'BootBoots',
  timestamp: 1,
  uptime_seconds: 3725,
  system: {
    initialized: true,
    camera_ready: true,
    wifi_connected: true,
    sd_card_ready: true,
    i2c_ready: true,
    pcf8574_ready: true,
    atomizer_enabled: false,
    training_mode: false,
  },
  statistics: {
    total_detections: 4,
    boots_detections: 1,
    atomizer_activations: 1,
    false_positives_avoided: 2,
  },
  timing: { last_detection: 0, last_status_report: 0 },
  peripherals: { pir_active: true, flash_led_on: false, led_strip_on: true, spray_on: false },
};

test('reassembles log chunks in index order, even out of order', () => {
  let s = { ...initialDeviceState(), logs: { chunks: {}, next: 0, text: '', loading: true } };
  s = run(
    s,
    { type: 'log_chunk', chunk: 1, total: 2, data: 'second' },
    { type: 'log_chunk', chunk: 0, total: 2, data: 'first' },
  ).state;
  expect(s.logs.text).toBe('first\nsecond');
  expect(s.logs.loading).toBe(true);
  s = run(s, { type: 'logs_complete', total_chunks: 2 }).state;
  expect(s.logs.loading).toBe(false);
});

test('appends un-indexed log file chunks and handles legacy array logs', () => {
  let s = run(
    initialDeviceState(),
    { type: 'log_file_chunk', data: 'a' },
    { type: 'log_file_chunk', data: 'b' },
  ).state;
  expect(s.logs.text).toBe('a\nb');
  s = run(s, { type: 'log_file_complete' }, ['x', 'y']).state;
  expect(s.logs.text).toBe('["x","y"]');
  expect(s.logs.loading).toBe(false);
});

test('collects previous log files without duplicates', () => {
  const warn = vi.spyOn(console, 'warn').mockImplementation(() => {});
  const s = run(
    { ...initialDeviceState(), previousLogs: { files: [], loading: true } },
    { type: 'log_list_debug', message: 'hi' },
    { type: 'log_list_chunk', chunk: 0, filename: 'a.log' },
    { type: 'log_list_chunk', chunk: 0, filename: 'a.log' },
    { type: 'log_list_chunk', chunk: 1, filename: 'b.log' },
    { type: 'log_list_complete', count: 2 },
  ).state;
  expect(s.previousLogs).toEqual({ files: ['a.log', 'b.log'], loading: false });
  warn.mockRestore();
});

test('image list: chunks then complete, or complete arriving before the last chunk', () => {
  let s = run(
    initialDeviceState(),
    { type: 'image_list_chunk', chunk: 0, total: 2, filename: 'a.jpg' },
    { type: 'image_list_chunk', chunk: 1, total: 2, filename: 'b.jpg' },
    { type: 'image_list_complete', count: 2 },
  ).state;
  expect(s.imageList.list).toEqual(['a.jpg', 'b.jpg']);
  expect(s.imageList.loading).toBe(false);

  s = run(
    { ...s, imageList: { ...s.imageList, chunks: {}, loading: true } },
    { type: 'image_list_chunk', chunk: 1, total: 2, filename: 'd.jpg' },
    { type: 'image_list_complete', count: 2 },
  ).state;
  expect(s.imageList.loading).toBe(true);
  expect(s.imageList.progress).toEqual({ current: 1, total: 2 });
  s = run(s, { type: 'image_list_chunk', chunk: 0, total: 2, filename: 'c.jpg' }).state;
  expect(s.imageList.list).toEqual(['c.jpg', 'd.jpg']);

  s = run(s, { type: 'image_list', images: ['legacy.jpg'] }).state;
  expect(s.imageList.list).toEqual(['legacy.jpg']);
});

test('image transfer reassembles base64 out of order, asks for metadata, then caches both', () => {
  let s = requestImage(initialDeviceState(), 'cat.jpg');
  let out = run(
    s,
    { type: 'image_start', filename: 'cat.jpg', size: 6 },
    { type: 'image_chunk', chunk: 1, total: 2, data: 'CD' },
    { type: 'image_complete', filename: 'cat.jpg', chunks: 2 },
  );
  expect(out.state.image.current).toBeNull();
  expect(out.state.image.progress).toEqual({ current: 1, total: 2 });
  out = run(out.state, { type: 'image_chunk', chunk: 0, total: 2, data: 'AB' });
  s = out.state;
  expect(s.image.current).toBe('data:image/jpeg;base64,ABCD');
  expect(out.commands).toEqual([{ command: 'get_image_metadata', filename: 'cat.jpg' }]);
  expect(s.image.loading).toBe(true);

  out = run(s, { type: 'metadata_result', filename: 'cat.jpg', found: true, content: '{"x":1}' });
  expect(out.state.image.metadata).toBe('{"x":1}');
  expect(out.state.image.loading).toBe(false);
  expect(out.cache).toEqual({
    filename: 'cat.jpg',
    entry: { imageData: 'data:image/jpeg;base64,ABCD', metadata: '{"x":1}' },
  });

  // Metadata for something else is shown but not cached.
  out = run(out.state, { type: 'metadata_result', filename: 'other.jpg', found: false });
  expect(out.state.image.metadata).toBeNull();
  expect(out.cache).toBeUndefined();
});

test('a new photo refreshes the image list and opens the photo', () => {
  let out = run(initialDeviceState(), { type: 'photo_started' });
  expect(out.state.takingPhoto).toBe(true);
  out = run(out.state, { type: 'photo_complete', filename: 'new.jpg' });
  expect(out.state.takingPhoto).toBe(false);
  expect(out.state.pendingPhoto).toBe('new.jpg');
  expect(out.commands).toEqual([{ command: 'list_images' }]);
  out = run(
    out.state,
    { type: 'image_list_chunk', chunk: 0, total: 1, filename: 'new.jpg' },
    { type: 'image_list_complete', count: 1 },
  );
  expect(out.commands).toEqual([{ command: 'get_image', filename: 'new.jpg' }]);
  expect(out.state.image.selected).toBe('new.jpg');
  expect(out.state.image.loading).toBe(true);
  expect(out.state.pendingPhoto).toBeNull();
});

test('settings, camera settings and setting updates', () => {
  let s = run(initialDeviceState(), {
    type: 'settings',
    training_mode: true,
    dry_run: true,
    trigger_threshold: 0.9,
    claude_infer: true,
    camera: { brightness: 1 },
  }).state;
  expect(s.settings).toEqual({ trainingMode: true, dryRun: true, triggerThresh: 0.9, claudeInfer: true });
  expect(s.camera.brightness).toBe(1);
  s = run(s, { type: 'camera_settings', camera: { contrast: -1 } }, { type: 'camera_settings' }).state;
  expect(s.camera.contrast).toBe(-1);

  s = { ...s, updatingSetting: true };
  s = run(
    s,
    { type: 'setting_updated', setting: 'training_mode', value: false },
    { type: 'setting_updated', setting: 'dry_run', value: false },
    { type: 'setting_updated', setting: 'trigger_threshold', value: 0.75 },
    { type: 'setting_updated', setting: 'claude_infer', value: false },
    { type: 'setting_updated', setting: 'camera_vflip', value: true },
  ).state;
  expect(s.settings).toEqual({ trainingMode: false, dryRun: false, triggerThresh: 0.75, claudeInfer: false });
  expect(s.camera.vflip).toBe(true);
  expect(s.updatingSetting).toBe(false);

  s = run({ ...initialDeviceState() }, { type: 'settings' }).state;
  expect(s.settings.triggerThresh).toBe(0.8);
});

test('status responses, version, peripherals, simulation and reboot', () => {
  let s = run(
    { ...initialDeviceState(), rebooting: true, peripheralPending: 'spray' },
    { type: 'status', ...status },
    { type: 'status', device: 'Kappa-Warmer' },
    { type: 'version', version: '1.2.3' },
    { type: 'peripheral_updated', peripheral: 'spray', state: true },
    { type: 'peripheral_updated', peripheral: 'unknown', state: true },
    { type: 'simulation_started' },
    { type: 'reboot_ack' },
  ).state;
  expect(s.status?.device).toBe('BootBoots');
  expect(s.lastUpdate).toBe(AT);
  expect(s.version).toBe('1.2.3');
  expect(s.peripherals).toEqual({
    pir_active: true,
    flash_led_on: false,
    led_strip_on: true,
    spray_on: true,
  });
  expect(s.simulatePending).toBe(true);
  expect(s.rebooting).toBe(false);
  s = run(s, { type: 'simulation_complete' }).state;
  expect(s.simulatePending).toBe(false);

  const noPeripherals = applyStatus(s, { ...status, peripherals: undefined }, AT);
  expect(noPeripherals.peripherals).toEqual(s.peripherals);
});

test('errors stop every spinner; pong clears the error; the response log is capped', () => {
  const err = vi.spyOn(console, 'error').mockImplementation(() => {});
  let s: DeviceState = {
    ...initialDeviceState(),
    takingPhoto: true,
    updatingSetting: true,
    logs: { chunks: {}, next: 0, text: '', loading: true },
  };
  s = run(s, { type: 'error', message: 'SD card missing' }).state;
  expect(s.error).toBe('SD card missing');
  expect(s.takingPhoto || s.updatingSetting || s.logs.loading).toBe(false);
  s = run(s, { response: 'pong' }).state;
  expect(s.error).toBeNull();
  s = run(s, { type: 'error' }, { type: 'pong' }).state;
  expect(s.error).toBeNull();

  for (let i = 0; i < 60; i++) s = run(s, { type: 'ota_progress', progress: i }).state;
  expect(s.responseLog).toHaveLength(50);
  expect(s.responseLog[0]).toContain('ota_progress');
  err.mockRestore();
});
