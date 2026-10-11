import type { FleetApi } from '../services/fleet';
import type { DeviceCommand, DeviceTransport } from './types';

/** Commands Fleet refuses on /command: firmware updates go through Fleet's own OTA flow. */
export const FLEET_RESERVED = new Set(['ota_update', 'url_chunk']);

/** Sends commands over MQTT via Fleet's POST /devices/{id}/command. Responses arrive on Fleet's WebSocket. */
export class FleetTransport implements DeviceTransport {
  constructor(
    private readonly api: Pick<FleetApi, 'sendCommand'>,
    readonly deviceId: string,
  ) {}

  async send(command: DeviceCommand): Promise<void> {
    if (FLEET_RESERVED.has(command.command))
      throw new Error(`${command.command} is only available through Fleet`);
    await this.api.sendCommand(this.deviceId, command);
  }
}
