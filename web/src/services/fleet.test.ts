import { createFleetApi } from './fleet';

test('lists devices and sends commands with the access token', async () => {
  const fetchImpl = vi.fn(
    async (_url: string, _init?: RequestInit) =>
      new Response(JSON.stringify({ devices: [{ deviceId: 'BootBootsThing' }] })),
  );
  const api = createFleetApi('https://api.fleet', async () => 'tok', fetchImpl as typeof fetch);
  expect(await api.listDevices()).toEqual([{ deviceId: 'BootBootsThing' }]);
  await api.sendCommand('Boots Thing', { command: 'ping' });
  const [url, init] = fetchImpl.mock.calls[1];
  expect(url).toBe('https://api.fleet/devices/Boots%20Thing/command');
  expect(init?.method).toBe('POST');
  expect(init?.headers).toEqual({ Authorization: 'Bearer tok', 'Content-Type': 'application/json' });
  expect(init?.body).toBe('{"command":"ping"}');
});

test('surfaces Fleet error messages', async () => {
  const fetchImpl = vi.fn(
    async () => new Response(JSON.stringify({ error: 'Use the OTA endpoint' }), { status: 400 }),
  );
  const api = createFleetApi('https://api.fleet', async () => 'tok', fetchImpl as typeof fetch);
  await expect(api.sendCommand('d', { command: 'ota_update' })).rejects.toThrow('Use the OTA endpoint');
  const empty = vi.fn(async () => new Response('', { status: 502 }));
  await expect(createFleetApi('x', async () => 't', empty as typeof fetch).listDevices()).rejects.toThrow(
    'GET /devices failed (502)',
  );
});
