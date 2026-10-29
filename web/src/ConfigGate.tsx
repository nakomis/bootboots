import Alert from '@mui/material/Alert';
import CircularProgress from '@mui/material/CircularProgress';
import { type ReactNode, useEffect, useState } from 'react';
import { type BootbootsConfig, loadConfig } from './config';

type State =
  | { status: 'loading' }
  | { status: 'ready'; config: BootbootsConfig }
  | { status: 'error'; message: string };

/** Fetches `config.json` once, before the app renders, and hands the validated config to `children`. */
export function ConfigGate({ children }: { children: (config: BootbootsConfig) => ReactNode }) {
  const [state, setState] = useState<State>({ status: 'loading' });

  useEffect(() => {
    let cancelled = false;
    loadConfig().then(
      (config) => !cancelled && setState({ status: 'ready', config }),
      (e) => !cancelled && setState({ status: 'error', message: e instanceof Error ? e.message : String(e) }),
    );
    return () => {
      cancelled = true;
    };
  }, []);

  if (state.status === 'loading')
    return <CircularProgress aria-label="Loading configuration" sx={{ m: 2 }} />;
  if (state.status === 'error') {
    return (
      <Alert severity="error" sx={{ m: 2 }}>
        BootBoots could not load its configuration. {state.message}
      </Alert>
    );
  }
  return <>{children(state.config)}</>;
}
