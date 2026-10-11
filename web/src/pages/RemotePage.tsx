import Alert from '@mui/material/Alert';
import Button from '@mui/material/Button';
import Chip from '@mui/material/Chip';
import FormControl from '@mui/material/FormControl';
import InputLabel from '@mui/material/InputLabel';
import MenuItem from '@mui/material/MenuItem';
import Paper from '@mui/material/Paper';
import Select from '@mui/material/Select';
import Typography from '@mui/material/Typography';
import { useCallback, useEffect, useMemo, useState } from 'react';
import { useBootboots } from '../BootbootsContext';
import { DeviceConsole } from '../components/DeviceConsole';
import { FleetLink } from '../components/FleetLink';
import { FleetTransport } from '../device/fleetTransport';
import { isSystemStatus } from '../device/types';
import { useDeviceSession } from '../device/useDeviceSession';
import type { DeviceMessageEvent, FleetDevice } from '../services/fleet';
import type { LiveStatus } from '../services/liveUpdates';

const LIVE_LABEL = { live: 'Live', connecting: 'Connecting…', offline: 'Offline' } as const;
const LIVE_COLOUR = { live: 'success', connecting: 'default', offline: 'warning' } as const;

/** Remote control over MQTT: commands via Fleet's API, responses via Fleet's live WebSocket. */
export function RemotePage() {
  const { config, fleet, live } = useBootboots();
  const [devices, setDevices] = useState<FleetDevice[]>([]);
  const [loadingDevices, setLoadingDevices] = useState(true);
  const [loadError, setLoadError] = useState<string | null>(null);
  const [deviceId, setDeviceId] = useState('');
  const [liveStatus, setLiveStatus] = useState<LiveStatus>(live.status);

  const transport = useMemo(() => (deviceId ? new FleetTransport(fleet, deviceId) : null), [fleet, deviceId]);
  const session = useDeviceSession(transport);
  const { receive, receiveStatus, reset, actions } = session;

  const loadDevices = useCallback(async () => {
    setLoadingDevices(true);
    setLoadError(null);
    try {
      const all = await fleet.listDevices();
      const ours = all.filter((d) => d.project.toLowerCase() === config.project);
      setDevices(ours);
      setDeviceId(
        (current) =>
          current || (ours.find((d) => d.deviceId === config.catcamThing) ?? ours[0])?.deviceId || '',
      );
    } catch (err) {
      setLoadError(`Failed to load devices: ${err instanceof Error ? err.message : String(err)}`);
    } finally {
      setLoadingDevices(false);
    }
  }, [fleet, config]);

  useEffect(() => {
    void loadDevices();
  }, [loadDevices]);

  // Fleet's WebSocket runs while this page is open.
  useEffect(() => {
    const offStatus = live.onStatus(setLiveStatus);
    live.start();
    return () => {
      offStatus();
      live.stop();
    };
  }, [live]);

  useEffect(
    () =>
      live.subscribe((event: DeviceMessageEvent) => {
        if (event.deviceId !== deviceId) return;
        if (event.channel === 'status') {
          if (isSystemStatus(event.message)) receiveStatus(event.message);
        } else {
          receive(event.message);
        }
      }),
    [live, deviceId, receive, receiveStatus],
  );

  // On choosing a device (and once live), fetch its settings, status and images.
  const isLive = liveStatus === 'live';
  useEffect(() => {
    if (!transport || !isLive) return;
    reset();
    const timer = setTimeout(() => {
      void actions.requestSettings();
      void actions.requestStatus();
      void actions.getVersion();
      void actions.listImages();
    }, 500);
    return () => clearTimeout(timer);
  }, [transport, isLive, reset, actions]);

  const device = devices.find((d) => d.deviceId === deviceId);

  return (
    <div>
      <Typography variant="h5">Remote</Typography>
      <Typography variant="body2" color="text.secondary" gutterBottom>
        Control the catcam over MQTT, through Fleet. Firmware updates are in{' '}
        <FleetLink deviceId={deviceId || config.catcamThing}>Fleet</FleetLink>.
      </Typography>

      {loadError && (
        <Alert severity="error" sx={{ my: 2 }}>
          {loadError}
        </Alert>
      )}

      <Paper variant="outlined" sx={{ p: 2, my: 2 }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 12, flexWrap: 'wrap' }}>
          <FormControl size="small" sx={{ minWidth: 240 }}>
            <InputLabel id="device-label">Device</InputLabel>
            <Select
              labelId="device-label"
              label="Device"
              value={devices.some((d) => d.deviceId === deviceId) ? deviceId : ''}
              onChange={(e) => setDeviceId(String(e.target.value))}
            >
              {devices.map((d) => (
                <MenuItem key={d.deviceId} value={d.deviceId}>
                  {d.deviceId}
                </MenuItem>
              ))}
            </Select>
          </FormControl>
          <Button onClick={loadDevices} disabled={loadingDevices}>
            {loadingDevices ? 'Loading…' : 'Refresh'}
          </Button>
          <Chip
            size="small"
            label={LIVE_LABEL[liveStatus]}
            color={LIVE_COLOUR[liveStatus]}
            variant="outlined"
          />
        </div>
        {!loadingDevices && !loadError && devices.length === 0 && (
          <Typography color="text.secondary" sx={{ mt: 2 }}>
            No {config.project} devices in Fleet.
          </Typography>
        )}
        {device && (
          <div
            style={{
              display: 'grid',
              gridTemplateColumns: 'repeat(auto-fit, minmax(200px, 1fr))',
              gap: 8,
              marginTop: 12,
            }}
          >
            <Typography variant="body2">
              <strong>Project:</strong> {device.project}
            </Typography>
            <Typography variant="body2">
              <strong>Type:</strong> {device.deviceType ?? 'unknown'}
            </Typography>
            <Typography variant="body2">
              <strong>Firmware:</strong> {session.state.version ?? device.currentVersion ?? 'unknown'}
            </Typography>
            <Typography variant="body2">
              <strong>Last seen:</strong>{' '}
              {device.lastSeen ? new Date(device.lastSeen).toLocaleString('en-GB') : 'never'}
            </Typography>
          </div>
        )}
      </Paper>

      {transport && <DeviceConsole state={session.state} actions={actions} showResponseLog />}
    </div>
  );
}
