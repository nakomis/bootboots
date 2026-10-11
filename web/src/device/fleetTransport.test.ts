import { FleetTransport } from './fleetTransport';

test('sends commands through Fleet and refuses OTA commands', async () => {
  const api = { sendCommand: vi.fn(async () => ({})) };
  const t = new FleetTransport(api, 'BootBootsThing');
  await t.send({ command: 'take_photo' });
  expect(api.sendCommand).toHaveBeenCalledWith('BootBootsThing', { command: 'take_photo' });
  await expect(t.send({ command: 'ota_update', url: 'x' })).rejects.toThrow('only available through Fleet');
  await expect(t.send({ command: 'url_chunk' })).rejects.toThrow();
  expect(api.sendCommand).toHaveBeenCalledTimes(1);
});
