import * as fs from 'fs';
import * as path from 'path';
import type { DeployEnv } from './env-config';

/**
 * Which trained model each environment's `bootboots` endpoint serves (BOOT-9). Committed in
 * infra/model.json, so a new model ships through a PR and a normal deploy, and CDK is the endpoint's
 * only owner. `local-training/deploy_model.sh` writes the sandbox entry; `scripts/promote-model.sh`
 * copies an artefact into another environment's bucket and writes that entry.
 */
export interface ModelSpec {
  /** Key of model.tar.gz in the environment's bootboots-models-{account} bucket. */
  artefact: string;
  /** Names the SageMaker model and endpoint config (they're immutable, so a new model gets new ones). */
  label: string;
}

export type ModelFile = Record<DeployEnv, ModelSpec | null>;

const ARTEFACT = /^[A-Za-z0-9._/-]+\/model\.tar\.gz$/;
// 'bootboots-config-' + label must fit SageMaker's 63-character names.
const LABEL = /^[A-Za-z0-9-]{1,40}$/;

export const MODEL_FILE = path.join(__dirname, '..', 'model.json');

export function parseModelFile(json: unknown): ModelFile {
  if (typeof json !== 'object' || json === null || Array.isArray(json)) {
    throw new Error('model.json: must be an object with "sandbox" and "prod" entries');
  }
  const file = json as Record<string, unknown>;
  const unknown = Object.keys(file).filter((k) => k !== 'sandbox' && k !== 'prod');
  if (unknown.length) throw new Error(`model.json: unknown environment(s) ${unknown.join(', ')}`);
  const result = {} as ModelFile;
  for (const env of ['sandbox', 'prod'] as const) {
    if (!(env in file)) throw new Error(`model.json: no "${env}" entry (use null for no endpoint)`);
    const spec = file[env] as ModelSpec | null;
    if (spec !== null) {
      if (typeof spec?.artefact !== 'string' || !ARTEFACT.test(spec.artefact)) {
        throw new Error(`model.json: ${env}.artefact must be a key ending in /model.tar.gz`);
      }
      if (typeof spec.label !== 'string' || !LABEL.test(spec.label)) {
        throw new Error(`model.json: ${env}.label must be 1-40 letters, digits or hyphens`);
      }
      // The SageMaker names come from the label alone, and CloudFormation can't replace a resource
      // in place under the same custom name: a new artefact must bring a new label.
      if (!spec.artefact.split('/').includes(spec.label)) {
        throw new Error(`model.json: ${env}.label must be a directory in its artefact path`);
      }
    }
    result[env] = spec;
  }
  return result;
}

/**
 * The model for an environment, or null for no endpoint. Sandbox must have one: the live endpoint
 * is there, and a missing entry would have CloudFormation remove it.
 */
export function modelFor(deployEnv: DeployEnv, file = MODEL_FILE): ModelSpec | null {
  const spec = parseModelFile(JSON.parse(fs.readFileSync(file, 'utf-8')))[deployEnv];
  if (spec === null && deployEnv === 'sandbox') {
    throw new Error('model.json: sandbox must name a model (it serves the live bootboots endpoint)');
  }
  return spec;
}
