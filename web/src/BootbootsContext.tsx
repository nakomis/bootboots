import { createContext, type ReactNode, useContext, useMemo } from 'react';
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
  const value = useMemo<BootbootsContextValue>(() => {
    const config = overrides?.config ?? configFor(shell.env);
    return {
      shell,
      config,
      aws: overrides?.aws ?? createAwsServices(config, shellCredentials(shell, config.identityPoolId)),
      fleet: overrides?.fleet ?? createFleetApi(config.fleetApiUrl, shell.getAccessToken),
      live: overrides?.live ?? new LiveUpdates(config.fleetWsUrl, shell.getAccessToken),
      username: overrides?.username ?? (shell.user.username || shell.user.sub),
    };
  }, [shell, overrides]);
  return <Ctx.Provider value={value}>{children}</Ctx.Provider>;
}

export function useBootboots() {
  const ctx = useContext(Ctx);
  if (!ctx) throw new Error('useBootboots must be used inside BootbootsProvider');
  return ctx;
}
