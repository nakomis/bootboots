import { ThemeProvider } from '@mui/material/styles';
import { render } from '@testing-library/react';
import type { ReactElement } from 'react';
import { MemoryRouter } from 'react-router';
import { type BootbootsContextValue, BootbootsProvider } from '../BootbootsContext';
import type { BootbootsConfig } from '../config';
import { createAwsServices } from '../services/aws';
import type { DeviceMessageEvent, FleetApi } from '../services/fleet';
import type { LiveStatus, LiveUpdates } from '../services/liveUpdates';
import type { ShellContext } from '../shell-contract';
import { theme } from '../theme';

export const config: BootbootsConfig = {
  environment: 'sandbox',
  version: '1.2.3',
  region: 'eu-west-2',
  identityPoolId: 'eu-west-2:pool',
  imagesBucket: 'bootboots-images-975050268859-eu-west-2',
  catadataTable: 'catadata',
  eventsApiUrl: 'https://api.bootboots.sandbox.nakomis.com/events',
  samServerUrl: 'https://phi.nasbox.nakomis.com:7861',
  localPredictUrl: 'http://localhost:8765/predict',
  fleetApiUrl: 'https://api.fleet.sandbox.nakomis.com',
  fleetWsUrl: 'wss://ws.fleet.sandbox.nakomis.com',
  catcamThing: 'BootBootsThing',
  project: 'bootboots',
};

export const credentials = async () => ({
  accessKeyId: 'AKID',
  secretAccessKey: 'secret',
  sessionToken: 'token',
  expiration: new Date(Date.now() + 3600_000),
});

export const shell = {
  contractVersion: 1,
  env: 'sandbox',
  basePath: '/bootboots',
  region: 'eu-west-2',
  userPoolId: 'pool',
  user: { sub: 's', username: 'martin' },
  getAccessToken: async () => 'token',
  getIdToken: async () => 'id',
  getAwsCredentials: async () => ({
    accessKeyId: 'AKID',
    secretAccessKey: 'secret',
    sessionToken: 'token',
    expiration: new Date(Date.now() + 3600_000),
  }),
  signOut: () => {},
} as ShellContext;

export function fakeLive(initial: LiveStatus = 'live') {
  const listeners = new Set<(e: DeviceMessageEvent) => void>();
  const statusListeners = new Set<(s: LiveStatus) => void>();
  const live = {
    status: initial,
    start: vi.fn(),
    stop: vi.fn(),
    subscribe: (l: (e: DeviceMessageEvent) => void) => {
      listeners.add(l);
      return () => listeners.delete(l);
    },
    onStatus: (l: (s: LiveStatus) => void) => {
      statusListeners.add(l);
      return () => statusListeners.delete(l);
    },
  } as unknown as LiveUpdates;
  return {
    live,
    emit: (e: DeviceMessageEvent) => {
      for (const l of listeners) l(e);
    },
    setStatus: (s: LiveStatus) => {
      for (const l of statusListeners) l(s);
    },
  };
}

export function renderWithBootboots(
  ui: ReactElement,
  overrides: Partial<Omit<BootbootsContextValue, 'shell'>> & { path?: string } = {},
) {
  const { path, ...rest } = overrides;
  const value = {
    config,
    aws: createAwsServices(config, credentials),
    fleet: {
      listDevices: vi.fn(async () => []),
      sendCommand: vi.fn(async () => ({})),
    } as unknown as FleetApi,
    live: fakeLive().live,
    username: 'martin',
    ...rest,
  };
  return render(
    <ThemeProvider theme={theme}>
      <BootbootsProvider shell={shell} config={config} overrides={value}>
        <MemoryRouter initialEntries={[path ?? '/']}>{ui}</MemoryRouter>
      </BootbootsProvider>
    </ThemeProvider>,
  );
}
