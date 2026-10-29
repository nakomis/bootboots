/**
 * Per-environment settings for every BootBoots stack: the infra 'sidecar'. Anything that differs
 * between sandbox and prod lives here; the account itself comes from the profile the deploy runs
 * under (CDK_DEFAULT_ACCOUNT), never from this file.
 */
export type DeployEnv = 'sandbox' | 'prod';

export function isDeployEnv(value: string | undefined): value is DeployEnv {
  return value === 'sandbox' || value === 'prod';
}

export interface EnvConfig {
  /** Every stack's region. Not CDK_DEFAULT_REGION: with no AWS config (CI) the CLI sets that to us-east-1. */
  region: string;
  /** Route 53 zone the API's custom domain is created in. */
  parentDomain: string;
  /** Custom domain of the BootBoots API (inference, video upload, events). */
  apiDomain: string;
  /** Browser origins allowed by the images bucket's CORS rules. */
  imagesBucketCorsOrigins: string[];
  /** Browser origins allowed by the API's CORS preflight and the events Lambda. */
  apiCorsOrigins: string[];
  /** Video-upload notification email (sender and recipient). */
  notificationEmail: string;
  /** Where firmware is published for Fleet OTA. */
  firmwareBucket: string;
}

const CONFIG: Record<DeployEnv, EnvConfig> = {
  sandbox: {
    region: 'eu-west-2',
    parentDomain: 'sandbox.nakomis.com',
    apiDomain: 'api.bootboots.sandbox.nakomis.com',
    imagesBucketCorsOrigins: ['https://sandbox.nakomis.com', 'https://app.sandbox.nakomis.com', 'http://localhost:3000'],
    apiCorsOrigins: [
      'https://sandbox.nakomis.com',
      'https://app.sandbox.nakomis.com',
      'http://localhost:3000',
      'http://localhost:3001',
    ],
    notificationEmail: 'bootboots@nakomis.com',
    firmwareBucket: 'nakomis-firmware-sandbox',
  },
  prod: {
    region: 'eu-west-2',
    parentDomain: 'nakomis.com',
    apiDomain: 'api.bootboots.nakomis.com',
    imagesBucketCorsOrigins: ['https://nakomis.com', 'https://app.nakomis.com'],
    apiCorsOrigins: ['https://nakomis.com', 'https://app.nakomis.com'],
    notificationEmail: 'bootboots@nakomis.com',
    firmwareBucket: 'nakomis-firmware-prod',
  },
};

export function envConfig(deployEnv: DeployEnv): EnvConfig {
  return CONFIG[deployEnv];
}

/** Resources the device stacks own and the panel uses. Names are the same in every environment. */
export const BOOTBOOTS = {
  /** Labelling data: partition key imageName, sort key uuid. Created by hand, not by any stack. */
  catadataTable: 'catadata',
  imagesBucket: (account: string, region: string) => `bootboots-images-${account}-${region}`,
  /** Training images read by the labelling and bounding pages. */
  trainingPrefix: 'catcam-training/',
  /** The catcam's IoT Thing. */
  thingName: 'BootBootsThing',
};

export const SSM = {
  /** The Anthropic API key the inference Lambda reads at runtime. */
  anthropicApiKey: '/bootboots/anthropic-api-key',
  /** Prefix of the catcam certificate parameters ThingWithCert writes (/BootsBoots/cert_pem, /BootsBoots/priv_key). */
  thingCertPrefix: 'BootsBoots',
  userPoolId: (env: DeployEnv) => `/nakomis-infra/${env}/cognito/user-pool-id`,
  shellClientId: (env: DeployEnv) => `/nakomis-shell/${env}/cognito/client-id`,
  shellBucketName: (env: DeployEnv) => `/nakomis-shell/${env}/bucket-name`,
  /** Prefix of Fleet's published parameters (api-url, ws-url) that the web panel's config.json copies. */
  fleetPrefix: (env: DeployEnv) => `/nakomis-fleet/${env}`,
  shellRemoteDeployerPolicyArn: (env: DeployEnv) => `/nakomis-shell/${env}/remote-deployer-policy-arn`,
  /** Everything the BootBoots repo publishes for its own scripts and the web panel's config.json. */
  bootbootsPrefix: (env: DeployEnv) => `/nakomis-bootboots/${env}`,
};

export const GITHUB_OWNER = { name: 'nakomis', id: '1488244' };
export const REPO = { name: 'bootboots', id: '1015458213' };
/** The panel's old home. Its CI role keeps trusting it until the repo is archived. */
export const LEGACY_PANEL_REPO = { name: 'nakomis-bootboots', id: '1414620899' };

/** GitHub OIDC subjects for a repo: the immutable-id form and the older name-only form. */
export const githubSubjects = (repo: { name: string; id: string }, context = '*') => [
  `repo:${GITHUB_OWNER.name}@${GITHUB_OWNER.id}/${repo.name}@${repo.id}:${context}`,
  `repo:${GITHUB_OWNER.name}/${repo.name}:${context}`,
];
