# nakomis-bootboots — The BootBoots panel for nakomis-shell

![Logo](docs/logo-candidates/20261011_181335_Flat_vector_icon_square_no_text_no_lette.png)

## Support

If you find this useful, please consider buying me a coffee:

[![Donate with PayPal](https://www.paypalobjects.com/en_GB/i/btn/btn_donate_SM.gif)](https://www.paypal.com/donate?hosted_button_id=Q3BESC73EWVNN&custom=nakomis-bootboots)

## Table of Contents

<!-- toc -->

- [What it is](#what-it-is)
- [How it fits the shell](#how-it-fits-the-shell)
- [Repository Layout](#repository-layout)
- [Pages](#pages)
- [AWS access](#aws-access)
- [Local development](#local-development)
- [Deploying](#deploying)
- [Logo](#logo)
- [Licence](#licence)
- [Support](#support)

<!-- tocstop -->

## What it is

The web front end for [BootBoots](https://github.com/nakomis/bootboots), the catcam that spots Boots and
politely mists him. It is a Module Federation **remote** of
[nakomis-shell](https://github.com/nakomis/nakomis-shell), mounted at `app.sandbox.nakomis.com/bootboots`.
It replaces the BootBoots tabs of the old `sandbox.nakomis.com` app (sandboxsite).

BootBoots resources (the catcam Thing, the `catadata` table, the images bucket and the events API) only
exist in the sandbox account, so there is no production deployment and the prod shell doesn't list it.

## How it fits the shell

- `web/` builds `remoteEntry.js` exposing `./App` (`src/ShellApp.tsx`). The shell passes a `ShellContext`
  (user, tokens, `getAwsCredentials(identityPoolId)`); see `src/shell-contract.ts`, copied from nakomis-shell.
- Only `react` and `react-dom` are shared. The remote brings its own MUI theme (One Dark, like the shell)
  and its own `BrowserRouter` with `basename={shell.basePath}`.
- Published to `s3://nakomis-shell-sandbox/remotes/bootboots/` and served at `/remotes/bootboots/`.
- **Device control goes through [Fleet](https://github.com/nakomis/nakomis-fleet)**: commands via
  `POST https://api.fleet.sandbox.nakomis.com/devices/{id}/command`, responses via
  `wss://ws.fleet.sandbox.nakomis.com`. Firmware updates are Fleet's job, so this panel links to
  `/fleet/devices/BootBootsThing` instead of offering OTA.

## Repository Layout

- `web/` — the remote (pnpm, Vite 8, React 19, MUI 7, React Router 7, Vitest, Biome)
  - `src/pages/` — one component per tab
  - `src/device/` — the catcam protocol: a pure response reducer (`protocol.ts`), the shared session hook,
    and the Web Bluetooth and Fleet transports. The firmware's `CommandDispatcher` serves BLE and MQTT alike,
    so both tabs share one console (`src/components/DeviceConsole.tsx`).
  - `src/services/` — catadata (DynamoDB), training images (S3), events API (SigV4), SAM server, Fleet
  - `src/config.ts` — per-environment settings (sandbox only)
- `infra/` — CDK (pnpm, Jest): `NakomisBootbootsStack`
- `docs/` — logo candidates

## Pages

| Tab | Path | What it does | Talks to |
|---|---|---|---|
| Catches | `/catches` | Detections and near-misses with their frames | bootboots events API (IAM) |
| Labelling | `/labelling` | Claim an unlabelled training image and say which cat it is (keys M T C K B W N 2) | `catadata`, images bucket |
| Bounding | `/bounding` | Tap cat/background points, SAM draws the box, save it | `catadata`, images bucket, SAM server |
| Remote | `/remote` | Status, logs, images, photos, settings, peripherals over MQTT | Fleet |
| Bluetooth | `/device` | The same console over Web Bluetooth (Chrome/Edge, near the device) | the catcam |

## AWS access

`infra/` creates the identity pool `nakomis_bootboots_sandbox` on the shared `nakomis-infra` user pool (shell
app client, no guests). Its authenticated role may only:

- `dynamodb:Scan` / `dynamodb:UpdateItem` on `catadata`
- `s3:GetObject` on `bootboots-images-975050268859-eu-west-2/catcam-training/*`
- `execute-api:Invoke` on `GET /events` of the BootBoots API

The pool id is exported to SSM `/nakomis-bootboots/sandbox/identity-pool-id`; `pnpm build:sandbox` bakes it
into the build. The stack also creates the CI role `nakomis-nakomis-bootboots-github-ci-sandbox`.

## Local development

```bash
cd web && pnpm install && pnpm dev     # http://localhost:5175
pnpm test && pnpm lint
```

`pnpm dev` runs a standalone harness (`src/main.tsx`) with a pasted access token, so only the Fleet-backed
Remote tab works there; the AWS-backed tabs need the shell.

## Deploying

CI tests both packages on every PR. On merge to `main` it deploys `infra/` and publishes `web/` to sandbox. By
hand:

```bash
cd infra && pnpm deploy-sandbox
cd ../web && AWS_PROFILE=nakom.is-sandbox pnpm build:sandbox
aws s3 sync dist/assets s3://nakomis-shell-sandbox/remotes/bootboots/assets/ --cache-control "public,max-age=31536000,immutable"
aws s3 sync dist s3://nakomis-shell-sandbox/remotes/bootboots/ --exclude "assets/*" --cache-control no-cache
aws cloudfront create-invalidation --distribution-id E33S40PNNINT2G --paths "/remotes/bootboots/*"
```

## Logo

`docs/logo-candidates/` holds two generated candidates; the first is used above until one is chosen.

## Licence

CC0 1.0 Universal — see [`LICENSE`](LICENSE).

## Support

If you find this useful, please consider buying me a coffee:

[![Donate with PayPal](https://www.paypalobjects.com/en_GB/i/btn/btn_donate_SM.gif)](https://www.paypal.com/donate?hosted_button_id=Q3BESC73EWVNN&custom=nakomis-bootboots)
