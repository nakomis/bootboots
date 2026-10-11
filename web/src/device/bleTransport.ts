/// <reference types="web-bluetooth" />
import type { BootBootsSystemStatus, DeviceCommand, DeviceTransport } from './types';

// BootBoots BLE service (lowercase, as Web Bluetooth requires).
export const BOOTBOOTS_SERVICE_UUID = 'bb00b007-5af3-41c3-9689-2fc7175c1ba8';
export const STATUS_CHARACTERISTIC_UUID = 'bb00b007-e90f-49fa-89c5-31e705b74d85';
export const COMMAND_CHARACTERISTIC_UUID = 'bb00b007-c0de-49fa-89c5-31e705b74d87';

export const GATT_TIMEOUT = 'gatt-timeout';

/**
 * Chrome caches GATT profiles and can hang for ever in getPrimaryService() after a firmware update changes
 * the profile. Timing out lets the caller forget the device so the next connection rediscovers it.
 */
export const withGattTimeout = <T>(promise: Promise<T>, timeoutMs = 10_000): Promise<T> =>
  new Promise<T>((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error(GATT_TIMEOUT)), timeoutMs);
    promise.then(
      (v) => {
        clearTimeout(timer);
        resolve(v);
      },
      (e) => {
        clearTimeout(timer);
        reject(e);
      },
    );
  });

export interface BleHandlers {
  onResponse(response: unknown): void;
  onStatus(status: BootBootsSystemStatus): void;
  onDisconnect(): void;
}

const decode = (value: DataView | undefined) => (value ? new TextDecoder().decode(value) : '');

export const bluetoothAvailable = () => typeof navigator !== 'undefined' && 'bluetooth' in navigator;

/** Previously paired catcams (navigator.bluetooth.getDevices is Chrome-only and experimental). */
export async function pairedDevices(): Promise<BluetoothDevice[]> {
  if (!bluetoothAvailable() || !('getDevices' in navigator.bluetooth)) return [];
  try {
    const devices = await navigator.bluetooth.getDevices();
    return devices.filter((d) => d.name?.startsWith('BootBoots'));
  } catch (err) {
    console.log('Could not get paired devices:', err);
    return [];
  }
}

/** Shows the browser's device picker, filtered to the BootBoots service. */
export const requestDevice = () =>
  navigator.bluetooth.requestDevice({
    filters: [{ services: [BOOTBOOTS_SERVICE_UUID] }],
    optionalServices: [BOOTBOOTS_SERVICE_UUID],
  });

/** A GATT connection to a catcam. Writes and reads are queued: GATT allows one operation at a time. */
export class BleConnection implements DeviceTransport {
  private queue: Promise<unknown> = Promise.resolve();
  private closed = false;

  private constructor(
    readonly device: BluetoothDevice,
    private readonly server: BluetoothRemoteGATTServer,
    private readonly statusChar: BluetoothRemoteGATTCharacteristic,
    private readonly commandChar: BluetoothRemoteGATTCharacteristic,
    private readonly handlers: BleHandlers,
  ) {}

  static async connect(device: BluetoothDevice, handlers: BleHandlers): Promise<BleConnection> {
    if (!device.gatt) throw new Error('GATT not available on this device');
    const server = await device.gatt.connect();
    const service = await withGattTimeout(server.getPrimaryService(BOOTBOOTS_SERVICE_UUID));
    const statusChar = await withGattTimeout(service.getCharacteristic(STATUS_CHARACTERISTIC_UUID));
    const commandChar = await withGattTimeout(service.getCharacteristic(COMMAND_CHARACTERISTIC_UUID));
    const conn = new BleConnection(device, server, statusChar, commandChar, handlers);

    await statusChar.startNotifications();
    statusChar.addEventListener('characteristicvaluechanged', conn.onStatusChanged);
    await commandChar.startNotifications();
    commandChar.addEventListener('characteristicvaluechanged', conn.onCommandResponse);
    device.addEventListener('gattserverdisconnected', conn.onGattDisconnected);
    return conn;
  }

  private enqueue<T>(op: () => Promise<T>): Promise<T> {
    const run = this.queue.then(op, op);
    this.queue = run.catch(() => undefined);
    return run;
  }

  private onStatusChanged = () => {
    // Re-read rather than trusting the notification payload, which is truncated to the MTU.
    this.readStatus()
      .then((status) => status && this.handlers.onStatus(status))
      .catch((err) => console.error('Error reading status update:', err));
  };

  private onCommandResponse = (event: Event) => {
    const text = decode((event.target as BluetoothRemoteGATTCharacteristic).value);
    if (!text) return;
    try {
      this.handlers.onResponse(JSON.parse(text));
    } catch {
      console.error('Received non-JSON response:', text);
    }
  };

  private onGattDisconnected = () => {
    this.cleanup();
    this.handlers.onDisconnect();
  };

  send(command: DeviceCommand): Promise<void> {
    if (this.closed) return Promise.reject(new Error('Not connected'));
    const data = new TextEncoder().encode(JSON.stringify(command));
    return this.enqueue(() => this.commandChar.writeValue(data));
  }

  async readStatus(): Promise<BootBootsSystemStatus | null> {
    const text = decode(await this.enqueue(() => this.statusChar.readValue()));
    return text ? (JSON.parse(text) as BootBootsSystemStatus) : null;
  }

  private cleanup() {
    if (this.closed) return;
    this.closed = true;
    this.statusChar.removeEventListener('characteristicvaluechanged', this.onStatusChanged);
    this.commandChar.removeEventListener('characteristicvaluechanged', this.onCommandResponse);
    this.device.removeEventListener('gattserverdisconnected', this.onGattDisconnected);
  }

  disconnect() {
    this.cleanup();
    if (this.server.connected) this.server.disconnect();
  }
}
