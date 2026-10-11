# nakomis-bootboots

The BootBoots panel: a Module Federation remote of `nakomis-shell`, mounted at `app.sandbox.nakomis.com/bootboots`.
Sandbox only — BootBoots resources don't exist in prod.

## AWS credentials

- Sandbox: `AWS_PROFILE=nakom.is-sandbox` (account 975050268859, eu-west-2). There is no prod deployment.

## Repository layout

- `web/` — remote (pnpm, Vite 8, React 19, MUI 7, React Router 7, Vitest, Biome). `src/shell-contract.ts` is a
  copy from nakomis-shell. `src/config.ts` holds per-env values; the identity pool id comes from SSM at build time
  (`pnpm build:sandbox`).
- `web/src/device/` — catcam protocol shared by the Bluetooth (`/device`) and Remote (`/remote`) tabs. Keep message
  shapes in step with bootboots `embedded/catcam` (`CommandDispatcher` and `main.cpp` handlers).
- `infra/` — CDK (pnpm, Jest): identity pool, its authenticated role, CI role, SSM export.

## Rules

- Device control goes through Fleet (`api.fleet.*` / `ws.fleet.*`). Never add OTA here: Fleet refuses
  `ota_update`/`url_chunk` on `/command`, and the panel links to Fleet for firmware.
- Keep the authenticated role minimal: only what the pages call (see README "AWS access").
- The bootboots events API and images bucket CORS live in the bootboots repo (`infra/lib/api-gateway.ts`); they must
  allow `https://app.sandbox.nakomis.com`.
- pnpm lockfiles only; never `package-lock.json`.

## Testing

```bash
cd infra && pnpm test                # ≥70% coverage
cd web && pnpm test && pnpm lint     # ≥70% lines
```

## Deploying

```bash
cd infra && NPM_ENVIRONMENT=sandbox pnpm cdk deploy --all --profile nakom.is-sandbox --require-approval never --output cdk-sb.out
```

CI deploys infra and publishes the remote on merge to `main` (`.github/workflows/ci.yml`).

## Architecture diagrams

Sources go in `docs/architecture/*.drawio` — SVGs are auto-regenerated on commit by `.githooks/pre-commit`.

```bash
git config core.hooksPath .githooks
```
