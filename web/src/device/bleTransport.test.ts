import { BleConnection, GATT_TIMEOUT, pairedDevices, requestDevice, withGattTimeout } from './bleTransport';

const enc = (v: unknown) => {
  const bytes = new TextEncoder().encode(typeof v === 'string' ? v : JSON.stringify(v));
  return new DataView(bytes.buffer);
};

class FakeChar extends EventTarget {
  value?: DataView;
  writes: string[] = [];
  readValue = vi.fn(async () => this.value as DataView);
  startNotifications = vi.fn(async () => this);
  writeValue = vi.fn(async (data: Uint8Array) => {
    this.writes.push(new TextDecoder().decode(data));
  });
  notify(v: unknown) {
    this.value = enc(v);
    this.dispatchEvent(new Event('characteristicvaluechanged'));
  }
}

function fakeDevice() {
  const status = new FakeChar();
  const command = new FakeChar();
  const service = {
    getCharacteristic: vi.fn(async (uuid: string) => (uuid.includes('e90f') ? status : command)),
  };
  const server = { connected: true, getPrimaryService: vi.fn(async () => service), disconnect: vi.fn() };
  const device = Object.assign(new EventTarget(), {
    id: 'dev1',
    name: 'BootBoots-1',
    gatt: { connect: vi.fn(async () => server) },
  });
  return { device: device as unknown as BluetoothDevice, server, status, command };
}

const flush = () => new Promise((r) => setTimeout(r, 0));

test('connects, relays responses and status, queues writes, and reports disconnects', async () => {
  const { device, server, status, command } = fakeDevice();
  const handlers = { onResponse: vi.fn(), onStatus: vi.fn(), onDisconnect: vi.fn() };
  const conn = await BleConnection.connect(device, handlers);

  await Promise.all([conn.send({ command: 'ping' }), conn.send({ command: 'get_settings' })]);
  expect(command.writes).toEqual(['{"command":"ping"}', '{"command":"get_settings"}']);

  command.notify({ type: 'pong' });
  expect(handlers.onResponse).toHaveBeenCalledWith({ type: 'pong' });
  const err = vi.spyOn(console, 'error').mockImplementation(() => {});
  command.notify('not json');
  expect(err).toHaveBeenCalled();

  status.notify({ device: 'BootBoots', system: {} });
  await flush();
  expect(handlers.onStatus).toHaveBeenCalledWith({ device: 'BootBoots', system: {} });
  expect(await conn.readStatus()).toEqual({ device: 'BootBoots', system: {} });

  device.dispatchEvent(new Event('gattserverdisconnected'));
  expect(handlers.onDisconnect).toHaveBeenCalled();
  await expect(conn.send({ command: 'ping' })).rejects.toThrow('Not connected');
  conn.disconnect();
  expect(server.disconnect).toHaveBeenCalled();
  err.mockRestore();
});

test('a failed write does not block the queue', async () => {
  const { device, command } = fakeDevice();
  const conn = await BleConnection.connect(device, {
    onResponse: vi.fn(),
    onStatus: vi.fn(),
    onDisconnect: vi.fn(),
  });
  command.writeValue.mockRejectedValueOnce(new Error('GATT busy'));
  await expect(conn.send({ command: 'a' })).rejects.toThrow('GATT busy');
  await conn.send({ command: 'b' });
  expect(command.writes).toEqual(['{"command":"b"}']);
});

test('withGattTimeout rejects hung GATT calls and passes results through', async () => {
  await expect(withGattTimeout(new Promise(() => {}), 5)).rejects.toThrow(GATT_TIMEOUT);
  await expect(withGattTimeout(Promise.resolve(1), 50)).resolves.toBe(1);
  await expect(withGattTimeout(Promise.reject(new Error('x')), 50)).rejects.toThrow('x');
});

test('rejects devices without GATT', async () => {
  await expect(
    BleConnection.connect({} as BluetoothDevice, {
      onResponse: vi.fn(),
      onStatus: vi.fn(),
      onDisconnect: vi.fn(),
    }),
  ).rejects.toThrow('GATT not available');
});

test('lists paired catcams and requests devices by service', async () => {
  expect(await pairedDevices()).toEqual([]);
  const bluetooth = {
    getDevices: vi.fn(async () => [{ name: 'BootBoots-1' }, { name: 'Kappa-Warmer' }]),
    requestDevice: vi.fn(async () => ({ name: 'BootBoots-1' })),
  };
  vi.stubGlobal('navigator', { bluetooth });
  expect(await pairedDevices()).toEqual([{ name: 'BootBoots-1' }]);
  await requestDevice();
  expect(bluetooth.requestDevice).toHaveBeenCalledWith(
    expect.objectContaining({ filters: [{ services: ['bb00b007-5af3-41c3-9689-2fc7175c1ba8'] }] }),
  );
  bluetooth.getDevices.mockRejectedValueOnce(new Error('nope'));
  vi.spyOn(console, 'log').mockImplementation(() => {});
  expect(await pairedDevices()).toEqual([]);
  vi.unstubAllGlobals();
});

test('a failed setup disconnects GATT and a drop during setup is reported', async () => {
  const { device, server, status } = fakeDevice();
  const handlers = { onResponse: vi.fn(), onStatus: vi.fn(), onDisconnect: vi.fn() };
  server.getPrimaryService.mockImplementationOnce(async () => {
    device.dispatchEvent(new Event('gattserverdisconnected'));
    throw new Error('NetworkError: GATT Server is disconnected.');
  });
  await expect(BleConnection.connect(device, handlers)).rejects.toThrow('GATT Server is disconnected');
  expect(handlers.onDisconnect).toHaveBeenCalledTimes(1);
  expect(server.disconnect).toHaveBeenCalledTimes(1);

  // A failure after the connection object exists also cleans up its listeners.
  status.startNotifications.mockRejectedValueOnce(new Error('notify failed'));
  await expect(BleConnection.connect(device, handlers)).rejects.toThrow('notify failed');
  expect(server.disconnect).toHaveBeenCalledTimes(2);
  device.dispatchEvent(new Event('gattserverdisconnected'));
  expect(handlers.onDisconnect).toHaveBeenCalledTimes(1);
});
