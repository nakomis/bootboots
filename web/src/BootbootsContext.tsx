import { createContext, type ReactNode, useContext, useMemo, useRef } from 'react';
import { shellCredentials } from './aws/credentials';
import { type BootbootsConfig, configFor } from './config';
import { type AwsServices, createAwsServices } from './services/aws';
import { createFleetApi, type FleetApi } from './services/fleet';
import { LiveUpdates } from './services/liveUpdates';
import type { ShellContext } from './shell-contract';

export interface BootbootsContextValue {
  shell: ShellContext;
  config: BootbootsConfig;
  aws: AwsServices;
  fleet: FleetApi;
  /** Fleet's live device messages; started by the pages that need it. */
  live: LiveUpdates;
  /** Recorded against labelling and bounding work. */
  username: string;
}

const Ctx = createContext<BootbootsContextValue | undefined>(undefined);

export function BootbootsProvider({
  shell,
  children,
  overrides,
}: {
  shell: ShellContext;
  children: ReactNode;
  overrides?: Partial<Omit<BootbootsContextValue, 'shell'>>;
}) {
  // The shell may pass a new props object on any render; only rebuild services when what they depend on
  // changes, and always call the latest token/credential functions.
  const shellRef = useRef(shell);
  shellRef.current = shell;
  const { env, region } = shell;
  const sub = shell.user.sub;
  const config = useMemo(() => overrides?.config ?? configFor(env), [overrides?.config, env]);
  const identityPoolId = config.identityPoolId;

  const services = useMemo(() => {
    // `sub` and `region` are keys only: a different user or region gets fresh clients and socket.
    void sub;
    void region;
    const getAccessToken = () => shellRef.current.getAccessToken();
    const credentialSource = {
      getAwsCredentials: (pool: string) => shellRef.current.getAwsCredentials(pool),
    };
    return {
      aws: createAwsServices(config, shellCredentials(credentialSource, identityPoolId)),
      fleet: createFleetApi(config.fleetApiUrl, getAccessToken),
      live: new LiveUpdates(config.fleetWsUrl, getAccessToken),
    };
  }, [config, identityPoolId, sub, region]);

  const username = shell.user.username || shell.user.sub;
  const value = useMemo<BootbootsContextValue>(
    () => ({
      shell: shellRef.current,
      config,
      aws: overrides?.aws ?? services.aws,
      fleet: overrides?.fleet ?? services.fleet,
      live: overrides?.live ?? services.live,
      username: overrides?.username ?? username,
    }),
    [config, services, overrides?.aws, overrides?.fleet, overrides?.live, overrides?.username, username],
  );
  return <Ctx.Provider value={value}>{children}</Ctx.Provider>;
}

export function useBootboots() {
  const ctx = useContext(Ctx);
  if (!ctx) throw new Error('useBootboots must be used inside BootbootsProvider');
  return ctx;
}
