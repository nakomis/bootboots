/** BootBoots resources (catcam Thing, catadata table, images bucket, events API) only exist in sandbox. */
export type DeployEnv = 'sandbox';

export const ACCOUNTS: Record<DeployEnv, { account: string; region: string }> = {
  sandbox: { account: '975050268859', region: 'eu-west-2' },
};

/** Resources owned by the bootboots repo (infra/lib/api-gateway.ts) that the panel uses. */
export const BOOTBOOTS = {
  /** Labelling data: partition key imageName, sort key uuid. */
  catadataTable: 'catadata',
  imagesBucket: (account: string, region: string) => `bootboots-images-${account}-${region}`,
  /** Training images read by the labelling and bounding pages. */
  trainingPrefix: 'catcam-training/',
  /** "BootBootsInference API" (api.bootboots.sandbox.nakomis.com); not exported by the bootboots stack. */
  apiId: 'yn8jvnh99j',
};

export const GITHUB_OWNER = { name: 'nakomis', id: '1488244' };
export const REPO = { name: 'nakomis-bootboots', id: '1414620899' };

export const githubSubjects = (repo: { name: string; id: string }) => [
  `repo:${GITHUB_OWNER.name}@${GITHUB_OWNER.id}/${repo.name}@${repo.id}:*`,
  `repo:${GITHUB_OWNER.name}/${repo.name}:*`,
];
