# BootBoots iOS companion

An old iPhone that records a short video clip whenever the spray deterrent
fires, and uploads it to S3 alongside the ESP32's stills.

This is the **spike**: a thin vertical slice through the real architecture
(IoT MQTT push + Cognito IAM), proving the end-to-end loop. It will be fleshed
out into the full app.

## How it works

```
infer Lambda (Boots detected)         ┌─────────── iPhone (this app) ───────────┐
   │ publishes to MQTT topic          │ signed in via Cognito (PKCE)            │
   ▼                                  │   → identity pool → temp IAM creds      │
AWS IoT Core  ── bootboots/ios/trigger ──▶ MQTT over WebSocket (SigV4)         │
                                      │   → record ~5s clip (AVFoundation)      │
                                      │   → PUT s3://…/iOS/<timestamp>.mov       │
                                      └──────────────────────────────────────────┘
```

- **Auth**: Cognito hosted UI (PKCE) on the shared sandbox user pool
  (`login.sandbox.nakomis.com`). The id token is exchanged through the
  `bootboots_ios_sandbox` identity pool for temporary IAM credentials.
- **Trigger transport**: MQTT5 over WebSocket to the account IoT ATS endpoint,
  SigV4-signed with those credentials. Subscribes to `bootboots/ios/trigger`.
- **Upload**: clips go to `bootboots-images-975050268859-eu-west-2` under the
  `iOS/` prefix (the ESP32 stills live under `catcam-images/` / `catcam-videos/`).

### IoT authorisation (a gotcha)

AWS IoT authorises **MQTT** for Cognito identities via an **AWS IoT policy
attached to the identity** — *not* the IAM role policy. The IAM role being
allowed `iot:Connect` is necessary for the control plane but does **not**
authorise the MQTT connection; without the attached IoT policy the broker
returns a CONNACK `AUTHORIZATION_FAILURE`. So:

- the `bootboots-ios-trigger` IoT policy is defined in
  `infra/lib/ios-companion.ts`, and
- the app attaches it to its own Cognito identity on launch
  (`IoTPolicyAttacher`, idempotent), which the role is granted `iot:AttachPolicy`
  for. This covers fresh logins / reinstalls (each gets a new identity id).

Sandbox-only — there is no production BootBoots backend. The build carries a
green tint and a **SANDBOX** badge as a visible marker.

## Backend

Provisioned by `infra/lib/ios-companion.ts` (stack
`BootBootsIosCompanionStack`). Its outputs are baked into `project.yml`:

| Setting | Value |
|---|---|
| `BootBootsCognitoClientID` | `4138v6b3na6a19oslpk4070io0` |
| `BootBootsIdentityPoolID` | `eu-west-2:dba65f95-729f-4ac9-9432-94978da22d82` |
| `BootBootsUserPoolID` | `eu-west-2_SSghhLpgX` (shared) |
| `BootBootsIoTEndpoint` | `aj2wv2wcxpv9m-ats.iot.eu-west-2.amazonaws.com` |
| `BootBootsTriggerTopic` | `bootboots/ios/trigger` |

The authenticated IAM role (`bootboots-ios-authenticated`) is scoped to
`iot:Connect` (client id `bootboots-ios-*`), `iot:Subscribe`/`iot:Receive` on
the trigger topic, and `s3:PutObject` on `iOS/*` only.

## Build & run

Requires Xcode and an Apple developer team (`62YFUFBSFX`). The project is
**generated** from `project.yml` and not committed.

```bash
brew install xcodegen        # one-time
cd ios && xcodegen generate  # regenerate after any project.yml change
open BootBoots.xcodeproj
```

Pick the **BootBoots (Sandbox)** scheme and run on a real device (the camera
needs hardware; the simulator connects and receives triggers but records no
clip). Sign in, tap **Start Monitoring**.

> Deployment is Xcode-only — no fastlane, TestFlight or App Store.

## Bench-testing the trigger

No cat or firmware needed — publish a trigger by hand:

```bash
AWS_PROFILE=nakom.is-sandbox aws iot-data publish \
  --region eu-west-2 \
  --topic bootboots/ios/trigger \
  --cli-binary-format raw-in-base64-out \
  --payload '{"ts":"2026-06-17T11:00:00Z","reason":"bench test"}'
```

The app should log the trigger, record a clip and upload it under `iOS/`.
There is also a **Test Trigger** button in the app that simulates a trigger
locally (records + uploads without going through MQTT).

## Not yet wired (full-app work)

- The infer Lambda publishing the real trigger on Boots-detect
  (`iot:Publish` to `bootboots/ios/trigger`).
- Rolling pre-buffer so the clip captures the cat *arriving*, not just the spray.
- Notifications on the `iOS/` prefix; background/kiosk hardening.
