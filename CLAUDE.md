# BootBoots Project

All of BootBoots lives in this repo: firmware, a single CDK app, and the web panel (BOOT-8 folded
`nakomis/nakomis-bootboots` in, with its history). Each subproject has its own CLAUDE.md. **Only
read these when the conversation turns to that area** - defer loading them to preserve context space.

## Subproject CLAUDE.md Files

- **Embedded firmware** (ESP32-S3, Arduino, BLE, OTA, camera): `embedded/CLAUDE.md`
- **Infrastructure** (single CDK app: API, IoT, SageMaker, panel, CI role): `infra/CLAUDE.md`
- **Web panel** (nakomis-shell remote at app.sandbox.nakomis.com/bootboots; Web Bluetooth and Fleet): `web/CLAUDE.md`
- **Device protocol** (commands, response types, MQTT topics, BLE UUIDs): `docs/protocol.md`

## Environments and deploys

- Two environments, `sandbox` (account from profile `nakom.is-sandbox`) and `prod` (`nakom.is-prod`). Per-environment settings live in `infra/lib/env-config.ts`; never hard-code accounts, domains or ARNs.
- CI (`.github/workflows/ci.yml`): PRs build, test and synth both environments; merges to `main` deploy to sandbox. The aggregate check is `CI Status`.
- By hand: `cd infra && pnpm diff-sandbox` then `pnpm deploy-sandbox` (every stack), or `pnpm deploy-stack-sandbox <Stack>` for one. Always diff first.
- The images bucket, `catcam-events` and the model/training buckets are retained if a stack goes; `catadata` isn't managed by any stack.
- Merges are merge commits (the repo has squash and rebase off), so imported history survives.

## Always Apply

- AWS profile: `AWS_PROFILE=nakom.is-sandbox`
- Never open the catcam's USB serial port casually: it resets the ESP32-S3.
