/// <reference types="web-bluetooth" />
import Alert from '@mui/material/Alert';
import Button from '@mui/material/Button';
import Paper from '@mui/material/Paper';
import Typography from '@mui/material/Typography';
import { useCallback, useEffect, useRef, useState } from 'react';
import { useBootboots } from '../BootbootsContext';
import { DeviceConsole } from '../components/DeviceConsole';
import { FleetLink } from '../components/FleetLink';
import {
  BleConnection,
  bluetoothAvailable,
  GATT_TIMEOUT,
  pairedDevices,
  requestDevice,
} from '../device/bleTransport';
import { useDeviceSession } from '../device/useDeviceSession';

type Status = 'Disconnected' | 'Connecting...' | 'Reconnecting...' | 'Connected';

const STALE_PROFILE =
  'Service discovery timed out — the device profile was stale after a firmware update. The device has been forgotten; please reconnect.';

/** The catcam over Web Bluetooth (Chrome or Edge, within range of the device). */
export function DevicePage() {
  const { config } = useBootboots();
  const [connection, setConnection] = useState<BleConnection | null>(null);
  const connectionRef = useRef<BleConnection | null>(null);
  const [status, setStatus] = useState<Status>('Disconnected');
  const [paired, setPaired] = useState<BluetoothDevice[]>([]);
  const [error, setError] = useState<string | null>(null);
  const session = useDeviceSession(connection);
  const { receive, receiveStatus, reset, actions } = session;

  useEffect(() => {
    void pairedDevices().then(setPaired);
  }, []);

  const dropConnection = useCallback(() => {
    connectionRef.current = null;
    setConnection(null);
    setStatus('Disconnected');
    reset();
  }, [reset]);

  // Disconnect when leaving the page.
  useEffect(() => () => connectionRef.current?.disconnect(), []);

  const forget = useCallback(async (device: BluetoothDevice) => {
    try {
      await device.forget();
    } catch {
      // forget() isn't available in every browser
    }
    setPaired((prev) => prev.filter((d) => d.id !== device.id));
  }, []);

  const open = useCallback(
    async (pick: () => Promise<BluetoothDevice>, reconnecting: boolean) => {
      setError(null);
      setStatus(reconnecting ? 'Reconnecting...' : 'Connecting...');
      let device: BluetoothDevice | undefined;
      try {
        device = await pick();
        reset();
        const conn = await BleConnection.connect(device, {
          onResponse: (response) => {
            receive(response);
            // The device restarts after acknowledging; don't wait for the GATT timeout.
            if ((response as { type?: string }).type === 'reboot_ack') {
              connectionRef.current?.disconnect();
              dropConnection();
            }
          },
          onStatus: receiveStatus,
          onDisconnect: dropConnection,
        });
        connectionRef.current = conn;
        setConnection(conn);
        setStatus('Connected');
        setPaired((prev) => (prev.some((d) => d.id === conn.device.id) ? prev : [...prev, conn.device]));
      } catch (err) {
        console.error('Bluetooth connection failed:', err);
        if (err instanceof Error && err.message === GATT_TIMEOUT && device) {
          await forget(device);
          setError(STALE_PROFILE);
        } else {
          setError(`${reconnecting ? 'Reconnection' : 'Connection'} failed: ${err}`);
        }
        setStatus('Disconnected');
      }
    },
    [dropConnection, forget, receive, receiveStatus, reset],
  );

  // Fetch settings and version once connected (after a short pause for the link to settle).
  useEffect(() => {
    if (!connection) return;
    const timer = setTimeout(() => {
      void actions.requestSettings();
      void actions.getVersion();
    }, 500);
    return () => clearTimeout(timer);
  }, [connection, actions]);

  const disconnect = () => {
    connectionRef.current?.disconnect();
    dropConnection();
  };

  const busy = status === 'Connecting...' || status === 'Reconnecting...';

  return (
    <div>
      <Typography variant="h5">Bluetooth</Typography>
      <Typography variant="body2" color="text.secondary" gutterBottom>
        Talk to the catcam directly over Web Bluetooth. Firmware updates are in{' '}
        <FleetLink deviceId={config.catcamThing}>Fleet</FleetLink>.
      </Typography>

      {!bluetoothAvailable() && (
        <Alert severity="info" sx={{ my: 2 }}>
          Web Bluetooth isn't available in this browser. Use Chrome or Edge, or the Remote tab.
        </Alert>
      )}

      <Paper variant="outlined" sx={{ p: 2, my: 2 }}>
        <Typography>
          <strong>Status:</strong> {status}
        </Typography>
        {connection && (
          <Typography>
            <strong>Device:</strong> {connection.device.name} ({connection.device.id})
          </Typography>
        )}
        <div style={{ display: 'flex', gap: 8, flexWrap: 'wrap', marginTop: 12 }}>
          {connection ? (
            <Button variant="outlined" onClick={disconnect}>
              Disconnect
            </Button>
          ) : (
            <Button
              variant="contained"
              disabled={busy || !bluetoothAvailable()}
              onClick={() => open(requestDevice, false)}
            >
              {status === 'Connecting...' ? 'Connecting...' : 'Connect to BootBoots'}
            </Button>
          )}
        </div>
        {!connection && paired.length > 0 && (
          <div style={{ marginTop: 16 }}>
            <Typography variant="body2" color="text.secondary" gutterBottom>
              Previously paired devices:
            </Typography>
            {paired.map((device) => (
              <span
                key={device.id}
                style={{ display: 'inline-flex', gap: 4, marginRight: 12, marginBottom: 6 }}
              >
                <Button variant="outlined" disabled={busy} onClick={() => open(async () => device, true)}>
                  {status === 'Reconnecting...' ? 'Reconnecting...' : `Reconnect to ${device.name}`}
                </Button>
                <Button
                  color="inherit"
                  disabled={busy}
                  onClick={() => forget(device)}
                  title="Forget device — clears the cached GATT profile. Use this if reconnection hangs after a firmware update."
                >
                  Forget
                </Button>
              </span>
            ))}
          </div>
        )}
        {error && (
          <Alert severity="error" onClose={() => setError(null)} sx={{ mt: 2 }}>
            {error}
          </Alert>
        )}
      </Paper>

      {connection && <DeviceConsole state={session.state} actions={actions} />}
    </div>
  );
}
