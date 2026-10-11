import { act, fireEvent, screen, waitFor } from '@testing-library/react';
import { renderWithBootboots } from '../test/render';
import { DevicePage } from './DevicePage';

const enc = (v: unknown) => new DataView(new TextEncoder().encode(JSON.stringify(v)).buffer);

class FakeChar extends EventTarget {
  value?: DataView;
  writes: Record<string, unknown>[] = [];
  readValue = vi.fn(async () => this.value as DataView);
  startNotifications = vi.fn(async () => this);
  writeValue = vi.fn(async (d: Uint8Array) => {
    this.writes.push(JSON.parse(new TextDecoder().decode(d)));
  });
  notify(v: unknown) {
    this.value = enc(v);
    this.dispatchEvent(new Event('characteristicvaluechanged'));
  }
}

function fakeBluetooth(hang = false) {
  const status = new FakeChar();
  const command = new FakeChar();
  const service = { getCharacteristic: async (uuid: string) => (uuid.includes('e90f') ? status : command) };
  const server = {
    connected: true,
    getPrimaryService: vi.fn(async () => (hang ? new Promise(() => {}) : service)),
    disconnect: vi.fn(),
  };
  const device = Object.assign(new EventTarget(), {
    id: 'dev1',
    name: 'BootBoots-1',
    gatt: { connect: vi.fn(async () => server) },
    forget: vi.fn(async () => {}),
  });
  const bluetooth = {
    getDevices: vi.fn(async () => [device]),
    requestDevice: vi.fn(async () => device),
  };
  vi.stubGlobal('navigator', { ...navigator, bluetooth });
  return { device, server, status, command, bluetooth };
}

afterEach(() => {
  vi.unstubAllGlobals();
  vi.useRealTimers();
});

test('explains when Web Bluetooth is unavailable', () => {
  renderWithBootboots(<DevicePage />);
  expect(screen.getByText(/Web Bluetooth isn't available/)).toBeInTheDocument();
  expect(screen.getByRole('button', { name: 'Connect to BootBoots' })).toBeDisabled();
  expect(screen.getByRole('link', { name: 'Fleet' })).toHaveAttribute(
    'href',
    '/fleet/devices/BootBootsThing',
  );
});

test('connects, fetches settings, shows status and disconnects on reboot', async () => {
  vi.useFakeTimers({ shouldAdvanceTime: true });
  const { command, status, device } = fakeBluetooth();
  renderWithBootboots(<DevicePage />);
  expect(await screen.findByRole('button', { name: 'Reconnect to BootBoots-1' })).toBeInTheDocument();

  fireEvent.click(screen.getByRole('button', { name: 'Connect to BootBoots' }));
  expect(await screen.findByText('Connected')).toBeInTheDocument();
  expect(screen.getByText('BootBoots-1 (dev1)')).toBeInTheDocument();
  await act(async () => {
    await vi.advanceTimersByTimeAsync(600);
  });
  expect(command.writes.map((w) => w.command).sort()).toEqual([
    'get_camera_settings',
    'get_settings',
    'get_version',
  ]);

  await act(async () => {
    status.notify({
      device: 'BootBoots',
      uptime_seconds: 3725,
      system: { camera_ready: true, pcf8574_ready: false },
      statistics: { total_detections: 3 },
      timing: {},
    });
    await vi.advanceTimersByTimeAsync(10);
  });
  expect(await screen.findByText('Uptime: 1h 2m 5s')).toBeInTheDocument();
  expect(screen.getByText('PCF8574 not ready — check I2C connections')).toBeInTheDocument();

  act(() => command.notify({ type: 'reboot_ack' }));
  expect(await screen.findByText('Disconnected')).toBeInTheDocument();

  // Reconnect, then lose the link.
  fireEvent.click(screen.getByRole('button', { name: 'Reconnect to BootBoots-1' }));
  expect(await screen.findByText('Connected')).toBeInTheDocument();
  act(() => {
    device.dispatchEvent(new Event('gattserverdisconnected'));
  });
  expect(await screen.findByText('Disconnected')).toBeInTheDocument();
});

test('forgets a device whose GATT profile is stale', async () => {
  vi.useFakeTimers({ shouldAdvanceTime: true });
  const { device } = fakeBluetooth(true);
  vi.spyOn(console, 'error').mockImplementation(() => {});
  renderWithBootboots(<DevicePage />);
  fireEvent.click(await screen.findByRole('button', { name: 'Connect to BootBoots' }));
  await act(async () => {
    await vi.advanceTimersByTimeAsync(10_500);
  });
  expect(await screen.findByText(/profile was stale/)).toBeInTheDocument();
  expect(device.forget).toHaveBeenCalled();
  expect(screen.queryByRole('button', { name: 'Reconnect to BootBoots-1' })).not.toBeInTheDocument();
});

test('reports a cancelled picker and lets you forget paired devices', async () => {
  const { bluetooth, device } = fakeBluetooth();
  bluetooth.requestDevice.mockRejectedValueOnce(new Error('User cancelled the requestDevice() chooser.'));
  vi.spyOn(console, 'error').mockImplementation(() => {});
  renderWithBootboots(<DevicePage />);
  fireEvent.click(await screen.findByRole('button', { name: 'Connect to BootBoots' }));
  expect(await screen.findByText(/Connection failed: Error: User cancelled/)).toBeInTheDocument();
  fireEvent.click(screen.getByRole('button', { name: 'Forget' }));
  await waitFor(() => expect(device.forget).toHaveBeenCalled());
});
