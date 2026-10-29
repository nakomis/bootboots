/**
 * Environment settings, read at runtime from the `config.json` sidecar that is published next to
 * `remoteEntry.js` (see scripts/publish-web.sh). Nothing environment-specific is baked into the build, so one
 * build can be deployed to any environment.
 */
export interface BootbootsConfig {
  /** Environment name this config was generated for, e.g. `sandbox`. */
  environment: string;
  /** Release version of the deployed build, shown in the footer. */
  version: string;
  region: string;
  /** Cognito identity pool created by infra/ (NakomisBootbootsStack). */
  identityPoolId: string;
  imagesBucket: string;
  catadataTable: string;
  eventsApiUrl: string;
  /** Segment Anything server used by the bounding page (developer tool; may be empty). */
  samServerUrl: string;
  /** Optional local classifier (bootboots local-training/serve.py) shown on the labelling page. */
  localPredictUrl: string;
  fleetApiUrl: string;
  fleetWsUrl: string;
  /** The catcam's IoT Thing and MQTT project. */
  catcamThing: string;
  project: string;
}

const REQUIRED = [
  'environment',
  'version',
  'region',
  'identityPoolId',
  'imagesBucket',
  'catadataTable',
  'eventsApiUrl',
  'fleetApiUrl',
  'fleetWsUrl',
  'catcamThing',
  'project',
] as const;

const DEFAULT_LOCAL_PREDICT_URL = 'http://localhost:8765/predict';

/** Validates the parsed `config.json`, applying defaults to the optional fields. */
export function parseConfig(raw: unknown): BootbootsConfig {
  if (typeof raw !== 'object' || raw === null || Array.isArray(raw)) {
    throw new Error('config.json must be a JSON object');
  }
  const values = raw as Record<string, unknown>;
  const bad = REQUIRED.filter((key) => typeof values[key] !== 'string' || values[key] === '');
  if (bad.length > 0) throw new Error(`config.json is missing required fields: ${bad.join(', ')}`);
  const optional = (key: string, fallback: string) =>
    typeof values[key] === 'string' && values[key] !== '' ? (values[key] as string) : fallback;
  return {
    ...(Object.fromEntries(REQUIRED.map((key) => [key, values[key]])) as Pick<
      BootbootsConfig,
      (typeof REQUIRED)[number]
    >),
    samServerUrl: optional('samServerUrl', ''),
    localPredictUrl: optional('localPredictUrl', DEFAULT_LOCAL_PREDICT_URL),
  };
}

/**
 * Where `config.json` lives, resolved against this module's own URL. The built chunks sit in `assets/`
 * (`/remotes/bootboots/assets/ShellApp-*.js`), so `../config.json` is `/remotes/bootboots/config.json`; under
 * the dev server this file is `/src/config.ts`, so it is `/config.json` (from `public/`). The path is held in
 * a variable so that Vite does not try to bundle it as an asset.
 */
export function configUrl(moduleUrl: string = import.meta.url): string {
  const relative = '../config.json';
  return new URL(relative, moduleUrl).href;
}

export async function loadConfig(url: string = configUrl()): Promise<BootbootsConfig> {
  let response: Response;
  try {
    response = await fetch(url, { cache: 'no-cache' });
  } catch (e) {
    throw new Error(`Could not fetch ${url}: ${e instanceof Error ? e.message : String(e)}`);
  }
  if (!response.ok) throw new Error(`Could not fetch ${url}: HTTP ${response.status}`);
  let raw: unknown;
  try {
    raw = await response.json();
  } catch {
    throw new Error(`${url} is not valid JSON`);
  }
  return parseConfig(raw);
}
