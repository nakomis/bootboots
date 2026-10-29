import type { ShellContext } from '../shell-contract';

/** The parts of Fleet's device registry entry this panel uses (see nakomis-fleet web/src/api/types.ts). */
export interface FleetDevice {
  deviceId: string;
  project: string;
  deviceType?: string;
  capabilities: string[];
  currentVersion?: string;
  lastSeen?: string;
  hasThing: boolean;
}

/** Pushed by wss://ws.fleet.* for every message a device publishes on {project}/{deviceId}/{channel}. */
export interface DeviceMessageEvent {
  type: 'deviceMessage';
  deviceId: string;
  project: string;
  channel: 'responses' | 'status';
  message: Record<string, unknown>;
}

export type FleetApi = ReturnType<typeof createFleetApi>;

/** Fleet's HTTP API (JWT auth with the shell's access token). */
export function createFleetApi(
  baseUrl: string,
  getAccessToken: ShellContext['getAccessToken'],
  fetchImpl: typeof fetch = fetch,
) {
  async function call<T>(method: string, path: string, body?: unknown): Promise<T> {
    const token = await getAccessToken();
    const res = await fetchImpl(`${baseUrl}${path}`, {
      method,
      headers: {
        Authorization: `Bearer ${token}`,
        ...(body === undefined ? {} : { 'Content-Type': 'application/json' }),
      },
      body: body === undefined ? undefined : JSON.stringify(body),
    });
    const text = await res.text();
    const data = text ? JSON.parse(text) : undefined;
    if (!res.ok) throw new Error(data?.error ?? data?.message ?? `${method} ${path} failed (${res.status})`);
    return data as T;
  }

  return {
    listDevices: () => call<{ devices: FleetDevice[] }>('GET', '/devices').then((r) => r.devices),
    /** Publishes to {project}/{deviceId}/commands. Fleet refuses ota_update and url_chunk (OTA is Fleet's job). */
    sendCommand: (deviceId: string, command: Record<string, unknown>) =>
      call<unknown>('POST', `/devices/${encodeURIComponent(deviceId)}/command`, command),
  };
}
