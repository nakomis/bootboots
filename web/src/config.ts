import type { DeployEnv } from './shell-contract';

/** Per-environment settings. BootBoots resources only exist in the sandbox account (975050268859). */
export interface BootbootsConfig {
  region: string;
  /** Cognito identity pool `nakomis_bootboots_sandbox`, created by infra/ (NakomisBootbootsStack). */
  identityPoolId: string;
  imagesBucket: string;
  catadataTable: string;
  eventsApiUrl: string;
  /** Segment Anything server used by the bounding page. */
  samServerUrl: string;
  /** Optional local classifier (bootboots local-training/serve.py) shown on the labelling page. */
  localPredictUrl: string;
  fleetApiUrl: string;
  fleetWsUrl: string;
  /** The catcam's IoT Thing and MQTT project. */
  catcamThing: string;
  project: string;
}

const SANDBOX: BootbootsConfig = {
  region: 'eu-west-2',
  identityPoolId: import.meta.env.VITE_BOOTBOOTS_IDENTITY_POOL_ID ?? '',
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

export function configFor(env: DeployEnv): BootbootsConfig {
  if (env !== 'sandbox') throw new Error(`BootBoots is only available in sandbox (not ${env})`);
  return SANDBOX;
}
