import { act, renderHook } from '@testing-library/react';
import type { DeviceTransport } from './types';
import { ImageCache, useDeviceSession } from './useDeviceSession';

function setup(transport: DeviceTransport | null = { send: vi.fn(async () => {}) }) {
  const hook = renderHook(({ t }) => useDeviceSession(t, () => 0), { initialProps: { t: transport } });
  const sent = () =>
    ((transport?.send as ReturnType<typeof vi.fn> | undefined)?.mock.calls ?? []).map((c) => c[0]);
  return { hook, sent, state: () => hook.result.current.state, actions: () => hook.result.current.actions };
}

test('actions send the protocol commands', async () => {
  const { sent, actions, state } = setup();
  await act(async () => {
    const a = actions();
    await a.ping();
    await a.requestStatus();
    await a.requestSettings();
    await a.getVersion();
    await a.requestLogs(50);
    await a.requestLogs();
    await a.listPreviousLogs();
    await a.getLogFile('old.log', 10);
    await a.listImages();
    await a.takePhoto();
    await a.setTriggerThresh(0.9);
    await a.setCameraSetting('vflip', true);
    await a.reboot();
    await a.setPeripheral('spray', true);
    await a.simulateDetection();
  });
  expect(sent()).toEqual([
    { command: 'ping' },
    { command: 'get_status' },
    { command: 'get_settings' },
    { command: 'get_camera_settings' },
    { command: 'get_version' },
    { command: 'request_logs', entries: 50 },
    { command: 'request_logs' },
    { command: 'list_logs' },
    { command: 'get_log_file', filename: 'old.log', entries: 10 },
    { command: 'list_images' },
    { command: 'take_photo' },
    { command: 'set_trigger_threshold', value: 0.9 },
    { command: 'set_setting', setting: 'camera_vflip', value: true },
    { command: 'reboot' },
    { command: 'set_peripheral', peripheral: 'spray', state: true },
    { command: 'simulate_detection' },
  ]);
  expect(state().takingPhoto).toBe(true);
  expect(state().rebooting).toBe(true);
  expect(state().peripheralPending).toBe('spray');
});

test('toggles send the opposite of the current setting', async () => {
  const { sent, actions, hook } = setup();
  act(() =>
    hook.result.current.receive({
      type: 'settings',
      training_mode: true,
      dry_run: false,
      claude_infer: true,
    }),
  );
  await act(async () => {
    await actions().toggleTrainingMode();
    await actions().toggleDryRun();
    await actions().toggleClaudeInfer();
  });
  expect(sent()).toEqual([
    { command: 'set_setting', setting: 'training_mode', value: false },
    { command: 'set_dry_run', enabled: true },
    { command: 'set_claude_infer', enabled: false },
  ]);
});

test('follow-up commands are sent and fetched images are cached', async () => {
  const { sent, actions, hook, state } = setup();
  await act(async () => {
    await actions().selectImage('a.jpg');
  });
  act(() => {
    hook.result.current.receive({ type: 'image_chunk', chunk: 0, total: 1, data: 'QQ==' });
    hook.result.current.receive({ type: 'image_complete', filename: 'a.jpg', chunks: 1 });
    hook.result.current.receive({ type: 'metadata_result', filename: 'a.jpg', found: true, content: 'm' });
  });
  expect(sent()).toEqual([
    { command: 'get_image', filename: 'a.jpg' },
    { command: 'get_image_metadata', filename: 'a.jpg' },
  ]);
  await act(async () => {
    await actions().selectImage('');
  });
  expect(state().image.current).toBeNull();
  await act(async () => {
    await actions().selectImage('a.jpg');
  });
  expect(state().image.current).toBe('data:image/jpeg;base64,QQ==');
  expect(state().image.metadata).toBe('m');
  expect(sent()).toHaveLength(2);
});

test('send failures show an error and roll back the spinner', async () => {
  const transport = { send: vi.fn(async () => Promise.reject(new Error('offline'))) };
  const { actions, state } = setup(transport);
  const err = vi.spyOn(console, 'error').mockImplementation(() => {});
  await act(async () => {
    await actions().takePhoto();
  });
  expect(state().takingPhoto).toBe(false);
  expect(state().error).toBe('Failed to take photo: offline');
  act(() => actions().clearError());
  expect(state().error).toBeNull();
  err.mockRestore();
});

test('without a transport, commands fail with "Not connected"', async () => {
  const err = vi.spyOn(console, 'error').mockImplementation(() => {});
  const { hook } = setup(null);
  await act(async () => {
    await hook.result.current.actions.ping();
  });
  expect(hook.result.current.state.error).toBe('Failed to send ping: Not connected');
  err.mockRestore();
});

test('requestStatus sends get_status even when the transport can read the status characteristic', async () => {
  const status = { device: 'BootBoots', system: {}, statistics: {} };
  const transport = { send: vi.fn(async () => {}), readStatus: vi.fn(async () => status as never) };
  const { actions, state, hook } = setup(transport);
  await act(async () => {
    await actions().requestStatus();
  });
  expect(transport.send).toHaveBeenCalledWith({ command: 'get_status' });
  expect(transport.readStatus).not.toHaveBeenCalled();

  act(() => hook.result.current.receiveStatus({ ...status, device: 'Again' } as never));
  expect(state().status?.device).toBe('Again');
  act(() => hook.result.current.reset());
  expect(state().status).toBeNull();
});

test('ImageCache evicts the least recently used entry', () => {
  const cache = new ImageCache(2);
  cache.set('a', { imageData: 'a', metadata: null });
  cache.set('b', { imageData: 'b', metadata: null });
  cache.get('a');
  cache.set('c', { imageData: 'c', metadata: null });
  expect(cache.get('b')).toBeUndefined();
  expect(cache.get('a')?.imageData).toBe('a');
  expect(cache.get('c')?.imageData).toBe('c');
});
