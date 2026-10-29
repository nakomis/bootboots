import { act, fireEvent, screen, waitFor, within } from '@testing-library/react';
import type { FleetApi } from '../services/fleet';
import { fakeLive, renderWithBootboots } from '../test/render';
import { RemotePage } from './RemotePage';

const devices = [
  { deviceId: 'Other', project: 'nakostat', capabilities: [], hasThing: true },
  { deviceId: 'BootBootsSpare', project: 'bootboots', capabilities: [], hasThing: true },
  {
    deviceId: 'BootBootsThing',
    project: 'BootBoots',
    deviceType: 'catcam',
    capabilities: [],
    hasThing: true,
    currentVersion: '1.0.0',
    lastSeen: '2026-10-11T10:00:00Z',
  },
];

function setup() {
  const fleet = {
    listDevices: vi.fn(async () => devices),
    sendCommand: vi.fn(async () => ({})),
  } as unknown as FleetApi & { sendCommand: ReturnType<typeof vi.fn> };
  const live = fakeLive('connecting');
  renderWithBootboots(<RemotePage />, { fleet, live: live.live });
  return { fleet, live };
}

const sentCommands = (fleet: { sendCommand: ReturnType<typeof vi.fn> }) =>
  fleet.sendCommand.mock.calls.map((c) => c[1].command);

test('picks the catcam, starts live updates and fetches its state once live', async () => {
  vi.useFakeTimers({ shouldAdvanceTime: true });
  const { fleet, live } = setup();
  expect(await screen.findByText('Connecting…')).toBeInTheDocument();
  expect(live.live.start).toHaveBeenCalled();
  expect(await screen.findByText('catcam')).toBeInTheDocument();
  expect(screen.getByText('1.0.0')).toBeInTheDocument();

  act(() => live.setStatus('live'));
  await act(async () => {
    await vi.advanceTimersByTimeAsync(600);
  });
  expect(sentCommands(fleet).sort()).toEqual([
    'get_camera_settings',
    'get_settings',
    'get_status',
    'get_version',
    'list_images',
  ]);
  expect(fleet.sendCommand.mock.calls[0][0]).toBe('BootBootsThing');

  // Responses for this device update the console; others are ignored.
  act(() => {
    live.emit({
      type: 'deviceMessage',
      deviceId: 'BootBootsThing',
      project: 'bootboots',
      channel: 'responses',
      message: { type: 'version', version: '2.0.0' },
    });
    live.emit({
      type: 'deviceMessage',
      deviceId: 'Other',
      project: 'x',
      channel: 'responses',
      message: { type: 'version', version: '9' },
    });
    live.emit({
      type: 'deviceMessage',
      deviceId: 'BootBootsThing',
      project: 'bootboots',
      channel: 'status',
      message: {
        device: 'BootBoots',
        uptime_seconds: 61,
        system: { camera_ready: true },
        statistics: { total_detections: 7 },
        timing: {},
      },
    });
    live.emit({
      type: 'deviceMessage',
      deviceId: 'BootBootsThing',
      project: 'bootboots',
      channel: 'status',
      message: { heartbeat: 1 },
    });
  });
  expect(screen.getByText('Firmware 2.0.0')).toBeInTheDocument();
  expect(screen.getByText('Total Detections: 7')).toBeInTheDocument();
  expect(screen.getByText('Response Log')).toBeInTheDocument();
  vi.useRealTimers();
});

test('drives the device console', async () => {
  const { fleet } = setup();
  await screen.findByText('catcam');
  fleet.sendCommand.mockClear();
  vi.spyOn(window, 'confirm').mockReturnValue(true);

  fireEvent.click(screen.getByRole('button', { name: 'Ping' }));
  fireEvent.click(screen.getByRole('button', { name: 'Take Photo' }));
  fireEvent.click(screen.getByRole('button', { name: 'More log options' }));
  fireEvent.click(await screen.findByRole('menuitem', { name: 'Previous logs...' }));
  fireEvent.click(screen.getByRole('button', { name: 'Get Logs' }));
  fireEvent.click(screen.getByRole('button', { name: 'Reboot' }));
  fireEvent.click(screen.getByRole('button', { name: 'Refresh Status' }));
  fireEvent.click(screen.getByRole('button', { name: 'Trigger Deterrent Sequence' }));
  fireEvent.click(screen.getByRole('switch', { name: 'Spray (Atomiser)' }));
  fireEvent.click(screen.getByText('BootBoots Settings'));
  fireEvent.click(screen.getByRole('switch', { name: 'Dry Run' }));
  await waitFor(() =>
    expect(sentCommands(fleet)).toEqual([
      'ping',
      'take_photo',
      'list_logs',
      'request_logs',
      'reboot',
      'get_status',
      'simulate_detection',
      'set_peripheral',
      'set_dry_run',
    ]),
  );
  expect(screen.getByRole('button', { name: 'Capturing...' })).toBeDisabled();
});

test('shows images, logs and previous logs from the device', async () => {
  const { live, fleet } = setup();
  await screen.findByText('catcam');
  const say = (message: Record<string, unknown>) =>
    act(() =>
      live.emit({
        type: 'deviceMessage',
        deviceId: 'BootBootsThing',
        project: 'bootboots',
        channel: 'responses',
        message,
      }),
    );
  say({ type: 'image_list', images: ['a.jpg', 'b.jpg'] });
  say({ type: 'log_chunk', chunk: 0, data: 'hello log' });
  say({ type: 'log_list_chunk', chunk: 0, filename: 'old.log' });
  expect(screen.getByText('(2 images available)')).toBeInTheDocument();
  expect(screen.getByText('hello log')).toBeInTheDocument();

  fireEvent.mouseDown(screen.getByLabelText('Select image'));
  fireEvent.click(within(await screen.findByRole('listbox')).getByText('b.jpg'));
  await waitFor(() =>
    expect(fleet.sendCommand).toHaveBeenCalledWith('BootBootsThing', {
      command: 'get_image',
      filename: 'b.jpg',
    }),
  );
  say({ type: 'image_chunk', chunk: 0, total: 1, data: 'QQ==' });
  say({ type: 'image_complete', filename: 'b.jpg', chunks: 1 });
  say({
    type: 'metadata_result',
    filename: 'b.jpg',
    found: true,
    content: JSON.stringify({
      mostLikelyCat: { name: 'Mu', confidence: 0.8, index: 3 },
      data: { probabilities: [0.1, 0, 0, 0.8, 0.1, 0] },
    }),
  });
  expect(screen.getByAltText('b.jpg')).toHaveAttribute('src', 'data:image/jpeg;base64,QQ==');
  expect(screen.getByText('Confidence: 80.0%')).toBeInTheDocument();

  fireEvent.mouseDown(screen.getByLabelText('Previous log'));
  fireEvent.click(within(await screen.findByRole('listbox')).getByText('old.log'));
  await waitFor(() =>
    expect(fleet.sendCommand).toHaveBeenCalledWith('BootBootsThing', {
      command: 'get_log_file',
      filename: 'old.log',
    }),
  );

  say({ type: 'error', message: 'SD card missing' });
  expect(screen.getByText('SD card missing')).toBeInTheDocument();
});

test('reports Fleet failures and an empty project', async () => {
  const fleet = {
    listDevices: vi.fn().mockRejectedValueOnce(new Error('401')).mockResolvedValueOnce([]),
    sendCommand: vi.fn(),
  } as unknown as FleetApi;
  renderWithBootboots(<RemotePage />, { fleet, live: fakeLive().live });
  expect(await screen.findByText('Failed to load devices: 401')).toBeInTheDocument();
  fireEvent.click(screen.getByRole('button', { name: 'Refresh' }));
  expect(await screen.findByText('No bootboots devices in Fleet.')).toBeInTheDocument();
});
