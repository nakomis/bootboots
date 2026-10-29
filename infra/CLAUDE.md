# Claude Code Guidelines for BootBoots Infrastructure

One CDK app (BOOT-8) holds every BootBoots stack, deployed per environment.

## Tooling

- **pnpm 11** (`packageManager` in `package.json`); `lambda/infer-lambda` is a workspace member with its own deps (it bundles `@anthropic-ai/sdk` 0.39). `pnpm install` from `infra/`.
- `aws-cdk-lib` 2.257 and a current `aws-cdk` CLI. esbuild is a local dev dependency and the training-script asset is bundled with the local `tar`, so nothing needs Docker (it's only a fallback).
- `pnpm build` type-checks, `pnpm test` runs the Jest suite (coverage gate 70%).

## Environments

- `NPM_ENVIRONMENT` must be `sandbox` or `prod`; the scripts set it. The **account comes from the profile** (`nakom.is-sandbox` / `nakom.is-prod`), never a literal.
- Everything that differs per environment is in `lib/env-config.ts` (domains, CORS origins, SSM paths, firmware bucket). Add new per-environment values there, not in stacks.
- `ci-synth-sandbox` / `ci-synth-prod` synthesise as account `000000000000` with no AWS access: `cdk.context.json` holds placeholder hosted-zone answers for that account (`CI-SYNTH-PLACEHOLDER`). A new context lookup needs a placeholder entry too, or CI synth fails.

## Stacks

| Stack | What |
|---|---|
| `BootBootsApiGatewayStack` | API (`api.bootboots.{sandbox.}nakomis.com`), inference/events/video Lambdas, images bucket, `catcam-events` |
| `BootBootsIoTStack` | `BootBootsThing`, its certificate (SSM `/BootsBoots/*`), role alias, IoT policies |
| `BootBootsAiTrainingStack` | The `bootboots` SageMaker endpoint (model from `model.json`), the models bucket and SageMaker role; in sandbox also the training pipeline |
| `NakomisBootbootsStack` | Web panel identity pool and role (`lib/panel-stack.ts`; the name predates the merge, and renaming it would recreate the identity pool), plus nakomis-bootboots' old CI role until that repo is archived |
| `BootBootsGithubCiStack` | `nakomis-bootboots-github-ci-{env}`: sandbox trusts any ref of `nakomis/bootboots`, prod only `main` |

The stacks publish what the panel and `scripts/publish-web.sh` read under `/nakomis-bootboots/{env}/` (`api-id`, `images-bucket`, `events-api-url`, `catcam-thing`, `catadata-table`, `identity-pool-id`). The panel stack reads `api-id`, so deploy the API stack first in a new environment.

No `MH-Version` tag: a per-release tag on every resource makes every deploy update everything (PIPE-8). `MH-Project=bootboots` only.

## Scripts

| Script | |
|---|---|
| `pnpm diff-sandbox [Stack]` / `pnpm diff-prod [Stack]` | Diff every stack, or one (always first) |
| `pnpm deploy-sandbox` / `pnpm deploy-prod` | Deploy every stack (`--all`) |
| `pnpm deploy-stack-sandbox <Stack>` / `pnpm deploy-stack-prod <Stack>` | Deploy one stack (and the stacks it depends on) |
| `pnpm synth-sandbox` / `pnpm synth-prod` | Synth everything with real credentials |
| `pnpm ci-synth-sandbox` / `pnpm ci-synth-prod` | Synth with no AWS access (CI) |
| `pnpm ci-deploy-sandbox` | What CI deploys on merge to main |

## Data

The images bucket, `catcam-events`, and the training and model buckets are `RETAIN`: deleting or replacing a stack never deletes them. `catadata` (labels) isn't in any stack. A full copy of the buckets and both tables as of 29 Oct 2026 is on Leia: `/Volumes/share/bootbootsbackup/bootboots-2026-10-29.zip`.

## The model and the endpoint (BOOT-9)

CDK is the endpoint's only owner. `model.json` names, per environment, the artefact (a key in that environment's `bootboots-models-{account}` bucket) and a label; `null` means no endpoint (sandbox must always have one, and the stack refuses to synth without it). The model and endpoint config are named `bootboots-<label>` / `bootboots-config-<label>`: a new label creates a new pair, the endpoint switches to it in place, and CloudFormation deletes the old pair. The endpoint has `DeletionPolicy: Retain`.

- **New model (sandbox):** `local-training/deploy_model.sh` exports, uploads and rewrites `model.json`'s sandbox entry. Commit it; CI deploys on merge (or `pnpm diff-sandbox BootBootsAiTrainingStack` then `pnpm deploy-stack-sandbox BootBootsAiTrainingStack`).
- **Promote to prod:** `scripts/promote-model.sh prod` copies the sandbox artefact into prod's bucket (never overwriting) and writes the prod entry. Commit, then deploy to prod.
- Never call SageMaker by hand to switch models: the next deploy would switch it back.
- Training runs only in sandbox; prod has the endpoint, the models bucket and the SageMaker role only.
- Verify a switch with a signed POST to `/infer` before and after (same image, same answer for the same model).

## IoT Resources

The `lib/iot-device.ts` stack uses `ThingWithCert` from `cdk-iot-core-certificates-v3` and creates:
- IoT Thing (`BootBootsThing`)
- Certificate (auto-generated, stored in SSM Parameter Store)
- IAM Role and Policy for device access
- Role Alias for credential provider
- IoT Policy for assuming role with certificate

## AI Training

- Training stack: `lib/ai-training-stack.ts`
- Training script: `training/train.py` - custom MobileNet v2 Keras script. CDK packages this directory as an S3 asset and uploads it automatically on every `cdk deploy`. The trigger Lambda reads the bucket/key from env vars and passes the S3 URI to SageMaker as `sagemaker_submit_directory`. **If you modify `train.py`, just run `cdk deploy BootBootsAiTrainingStack` - no manual packaging needed.**
- The `bootboots-trigger-training` Lambda's IAM policy requires `sagemaker:AddTags` (needed when `CreateTrainingJob` includes Tags) - this is already in the policy as of Feb 2026.
- Invoke the trigger Lambda with `--invocation-type Event` (async) - data prep takes ~90s and will cause CLI read timeout if invoked synchronously.
- **Old training images** (pre-2026, numeric filenames like `540.jpeg`) were migrated in Feb 2026 from `bootbootstraining` S3 bucket into `bootboots-images-{account}-{region}/catcam-training/`. They were truncated JPEGs (missing FF D9 EOI marker) — the data prep Lambda patches them on copy.
- **7-class classifier** (reverted from binary): model classifies Boots, Chi, Kappa, Mu, Tau, Wolf, NoCat. The binary Boots/NotBoots approach was abandoned — "NotBoots" was too vague a concept and the model struggled. The 7-class model runs at ~80–90% accuracy and collapses to a binary Boots/NotBoots decision at inference time for the spray pump trigger. The old binary classifier code is in git history; `multi-cat-recognition` tag predates it and has the earlier 7-class implementation.

## SSM Parameters

Credentials are stored in SSM Parameter Store with the `BootsBoots` prefix:
- `/BootsBoots/cert_pem` - Certificate PEM
- `/BootsBoots/priv_key` - Private key
