# BootBoots catcam device protocol

This is the reference for talking to the catcam (ESP32-S3 firmware in `embedded/catcam/`). It was written from the code, not from older documentation, and file:line references are given so that it can be checked. Where the firmware, the web panel and the tooling disagree, the disagreement is recorded in [Known mismatches](#known-mismatches) rather than papered over.

Sources read:

- Firmware: `embedded/catcam/src/main.cpp`, `lib/CommandDispatcher`, `lib/BluetoothService`, `lib/MqttService`, `lib/MqttOTA`, `lib/BluetoothOTA`, `lib/SDLogger`, `lib/SystemManager` (firmware version in `include/version.h` was 1.0.332 when this was written).
- Web panel: `web/src/device/{protocol,types,bleTransport,fleetTransport,useDeviceSession}.ts`, `web/src/services/{fleet,liveUpdates}.ts`, `web/src/pages/{DevicePage,RemotePage}.tsx`.
- Tooling and infrastructure: `embedded/catcam/scripts/bbdevice.py`, `infra/lib/iot-device.ts`.

## 1. Overview

Every message in either direction is a single JSON object, UTF-8, one message per BLE write/notification or MQTT publish. A command is `{"command": "<name>", ...parameters}`. A response is an object with a `type` field naming what it is (the one exception is the BLE status characteristic, see [section 4](#4-ble)).

There are two transports. Both end up in the same `CommandDispatcher` (`lib/CommandDispatcher/src/CommandDispatcher.cpp`), which is why most commands behave identically on both.

| Transport | Used by | Command goes to | Responses come back on |
|---|---|---|---|
| BLE (direct) | The web panel's Bluetooth page (`DevicePage.tsx`), `catcam_logs.py` | Write to the command characteristic | Notifications on the same command characteristic |
| MQTT via AWS IoT Core | Fleet, and so the web panel's Remote page (`RemotePage.tsx`) | Publish to the device's `commands` topic | The device publishes to its `responses` topic; Fleet forwards these to browsers over its WebSocket |

How a command is processed:

1. **BLE.** `onWrite` copies the bytes into a single 512-byte pending buffer (`BluetoothService.cpp:206-218`). The Arduino main loop picks it up in `handle()` (`:182-191`), and `processCommand` (`:233`) first checks for the BLE-only commands (`get_logs`, `request_logs`, `list_images`, `list_logs`, `get_log_file`, `get_image`, `get_image_metadata`) and handles those itself. Everything else goes to `CommandDispatcher::processCommand`.
2. **MQTT.** `MqttService::onMessage` hands any message on the command topic straight to `CommandDispatcher::processCommand` (`MqttService.cpp:191-209`).
3. **Dispatcher.** It parses the JSON (2048-byte document), rejects the command with an `error` response if `command` is missing, rejects any `CHUNKED_COMMANDS` entry if the sender does not support chunking (that is, on MQTT), then looks up the handler by name; an unknown name yields `{"type":"error","message":"Unknown command: <name>"}` (`CommandDispatcher.cpp:48-91`).

There is no request ID or correlation field. Responses are matched to commands only by `type`, so a client should not have two commands of the same kind in flight. A command can produce several messages (for example `take_photo` produces `photo_started` then `photo_complete`; list and file transfers produce many chunks and a completion message).

### Chunking and MTU

BLE notifications carry at most ATT_MTU minus 3 bytes. The firmware never sets a local MTU (there is no `setMTU` call); the central negotiates it. The chunked transfers produce notifications of roughly 450 bytes (image chunks: 300 raw bytes as 400 base64 characters plus JSON framing, `BluetoothService.cpp:439`) up to about 512 bytes (log lines, `:656`), so a client wants an MTU of about 515. `catcam_logs.py` ran at MTU 515. If the MTU is lower the stack truncates the notification, and the result is invalid JSON (the web panel logs "Received non-JSON response", `bleTransport.ts:123`; `catcam_logs.py` writes it out as an `_unparsed` marker).

"Chunked" in this firmware means the device splits a large result into many small JSON messages and streams them as notifications, with a small delay between each (50 ms per log line, 30 ms per list or image chunk, `BluetoothService.cpp:272,393,510,631,667`). The whole thing runs synchronously inside the main loop, so the device does nothing else while a transfer is in progress, and a large log file takes minutes.

MQTT has no such mechanism: `MqttResponseSender::supportsChunking()` is false (`MqttService.h:20`), and a message is a single publish into a 2048-byte PubSubClient buffer (`MqttService.cpp:93`). That is why the commands in `CHUNKED_COMMANDS` are BLE-only.

## 2. Commands

"Transports" says where the command works. "BLE-only" means the firmware rejects it on MQTT with `{"type":"error","message":"Command requires chunked transfer (use Bluetooth)"}` (`CommandDispatcher.cpp:73-78`). The firmware's `CHUNKED_COMMANDS` list (`CommandDispatcher.cpp:7-15`) is `get_image`, `get_logs`, `request_logs`, `list_images`, `list_logs`, `get_log_file`. There is no `CHUNKED_COMMANDS` constant in the web code; the web has no list of BLE-only commands at all (see mismatches), and `get_image_metadata` is BLE-only in practice although it is not on the firmware list.

### Dispatcher commands (BLE and MQTT)

| Command | Parameters | Response `type`(s) | Notes |
|---|---|---|---|
| `ping` | none | `pong` (`timestamp` = device millis) | `CommandDispatcher.cpp:103` |
| `get_status` | none | `status` | Live state. Full shape in [section 5](#5-response-and-push-message-types). Use this, not the BLE status characteristic (BOOT-7, [section 4](#4-ble)). `:116` |
| `get_settings` | none | `settings` (`training_mode`, `trigger_threshold`, `dry_run`, `claude_infer`) | No `camera` object, see mismatches. `:160` |
| `get_camera_settings` | none | `camera_settings` (`camera`: 25 sensor fields) | `:181` |
| `set_setting` | `setting` (string), `value` | `setting_updated` (`setting`, `value`) | Only `training_mode` (bool) and `camera_<name>` (`<name>` one of the 25 keys in `camera_settings`; bool or int by key) are accepted. Anything else gives `error`. Persisted via the registered callbacks. `:226` |
| `take_photo` | none | `photo_started`, then `photo_complete` (`filename`) | Blocks while capturing. `:318` |
| `reboot` | none | `reboot_ack`, then the device restarts 500 ms later | `:349` |
| `get_version` | none | `version` (`version`, `project`) | `:371` |
| `set_trigger_threshold` | `value` (0.0 to 1.0, clamped) | `setting_updated` (`setting:"trigger_threshold"`, `value`) | Registered in `main.cpp:158`; stored in NVS |
| `set_dry_run` | `enabled` (bool) | `setting_updated` (`setting:"dry_run"`, `value`) | `main.cpp:179`; stored in NVS |
| `set_claude_infer` | `enabled` (bool) | `setting_updated` (`setting:"claude_infer"`, `value`) | `main.cpp:198`; stored in NVS |
| `set_peripheral` | `peripheral` (`flash_led`, `led_strip` or `spray`), `state` (bool) | `peripheral_updated` (`peripheral`, `state`, `ok`) or `error` | `main.cpp:218`. Needs the PCF8574. Spray switches itself off after 5 s. |
| `simulate_detection` | none | `simulation_started`, then `simulation_complete` | `main.cpp:290`. Runs the full deterrent sequence with a synthetic Boots result and blocks for about 10 s. Respects `dry_run`. |
| `ota_update` | `url`, optional `version` | `ota_progress` (`progress`, `status`, `version`) or `ota_error` | `MqttOTA.cpp:33`. See OTA note below. |
| `url_chunk` | `chunk_index`, `total_chunks` (max 10), `chunk_data`, optional `version` | `ota_progress` (acknowledging each chunk) or `ota_error` | For URLs too long for one message. `MqttOTA.cpp:55` |
| `ota_cancel` | none | `ota_progress` (`status:"Update cancelled"`) | `MqttOTA.cpp:115` |

OTA note: when an OTA download starts, the firmware pauses MQTT and shuts down the BLE stack to free memory for TLS (`MqttOTA.cpp:164-186`). After the first `ota_progress` acknowledgement no further responses can be sent on MQTT, and the device reboots when the download finishes. Fleet refuses `ota_update` and `url_chunk` on its `/command` endpoint (it has its own OTA flow); the web panel's `FLEET_RESERVED` set (`fleetTransport.ts:5`) mirrors that, and no web code sends these commands over BLE either.

### BLE-only commands

All handled directly in `BootBootsBluetoothService::processCommand` (`BluetoothService.cpp:233-341`). They write their output straight to the command characteristic. On MQTT the first six are refused as above; `get_image_metadata` is not on the firmware's list and is not registered with the dispatcher, so on MQTT it returns `Unknown command: get_image_metadata`.

| Command | Parameters | Response `type`(s) | Notes |
|---|---|---|---|
| `request_logs` | optional `entries` (last N lines; default all) | `log_chunk` (1-based `chunk`, `data`) repeated, then `logs_complete` (`total_chunks`) | The current boot's log only. `:248` |
| `get_logs` | as `request_logs` | as `request_logs` | Alias for `request_logs`; the web only ever sends `request_logs`. `:248` |
| `list_logs` | none | optional `log_list_debug`, `log_list_chunk` repeated, `log_list_complete` | See [section 6](#6-ble-log-commands). `:287` |
| `get_log_file` | `filename`, optional `entries` | `log_file_chunk` repeated, `log_file_complete`; `error` if `filename` missing | See [section 6](#6-ble-log-commands). `:290` |
| `list_images` | none | `image_list_chunk` (0-based `chunk`, `total`, `filename`) repeated, then `image_list_complete` (`count`) | `.jpg` files in `/images`, sorted by name. `:284,375` |
| `get_image` | `filename` | `image_start` (`filename`, `size`), `image_chunk` (0-based `chunk`, `total`, base64 `data`) repeated, `image_complete` (`filename`, `chunks`); `error` ("File not found" or "Missing filename parameter") | 300 raw bytes per chunk. The LED strobes green and blue during the transfer. `:305,408` |
| `get_image_metadata` | `filename` | `metadata_result` (`filename`, `found`, `content`) | Reads the `.txt` file beside the image. `content` is the file as a string. `:319,532` |

## 3. MQTT

| Topic | Direction (device) | Purpose | QoS |
|---|---|---|---|
| `bootboots/BootBootsThing/commands` | subscribe | Commands in; payload is the command JSON | 0 (`subscribe(topic)` default, `MqttService.cpp:124`) |
| `bootboots/BootBootsThing/responses` | publish | Every dispatcher response | 0 (`publish(topic, payload)` default, `:23`) |
| `bootboots/BootBootsThing/status` | publish | Periodic status push, every 60 s | 0 (`:289`, interval `MqttService.h:119`) |

The topics are built as `bootboots/<thingName>/{commands,responses,status}` (`MqttService.cpp:106-110`). The Thing name is hard-coded as `BootBootsThing` and is also used as the MQTT client ID (`SystemManager.cpp:229`, `MqttService.cpp:120`). Broker: AWS IoT Core on port 8883 with mutual TLS (`MqttService.h:120`). The web panel's Remote page derives the same topic as `{project}/{deviceId}/commands` through Fleet, with `project = 'bootboots'` and `deviceId = 'BootBootsThing'` (`config.ts`, `fleet.ts:49`); Fleet pushes device publishes to browsers as `deviceMessage` events with `channel` set to `responses` or `status` (`fleet.ts:14-21`).

The periodic `status` push is a flat object, not the shape `get_status` returns:

```json
{"device":"BootBoots-CatCam","timestamp":123456,"uptime_seconds":123,
 "wifi_connected":true,"camera_ready":true,"training_mode":false,"total_detections":0}
```

(`MqttService.cpp:278-284`). It has no `type`, `system` or `statistics`.

The IoT policy in `infra/lib/iot-device.ts` permits `iot:Subscribe` on `topicfilter/catcam/*` and `iot:Publish`/`iot:Receive` on `topic/catcam/*` (`:114,119`) and `iot:Connect` only for the client ID equal to the Thing name. That does not match the `bootboots/...` prefix the firmware uses; see mismatches. I did not check the policy that is deployed in AWS.

MQTT is paused (disconnected) during an OTA download and during AWS credential refresh (`MqttService::pause`, `:211`), and reconnects every 5 s when down.

## 4. BLE

Device name: `BootBoots-CatCam` (`main.cpp:138`). Advertising includes the BootBoots service UUID and the OTA service UUID; the web panel filters on the BootBoots service UUID (`bleTransport.ts:55`).

### BootBoots service

Service UUID `bb00b007-5af3-41c3-9689-2fc7175c1ba8` (`BluetoothService.h:18`).

| Characteristic | UUID | Properties | Purpose |
|---|---|---|---|
| Status | `bb00b007-e90f-49fa-89c5-31e705b74d85` | read, notify | Intended: JSON system status. Currently never populated, see below. |
| Logs | `bb00b007-f1a2-49fa-89c5-31e705b74d86` | read | Legacy cached log text. Returns whatever `setLogData` last set; nothing calls `setLogData`, so it reads as empty. Logs are fetched through commands now. |
| Command | `bb00b007-c0de-49fa-89c5-31e705b74d87` | write, notify | Write a command JSON; responses arrive as notifications. Has a 2902 descriptor, so the client must enable notifications first. |

The command write is limited to 511 bytes (`BluetoothService.cpp:212`), and the JSON document it is parsed into has 512 bytes of capacity (`:234`). A write that is too long, or JSON that does not parse, is dropped and logged on the device with no response at all (unlike MQTT, which answers `Invalid JSON command`).

### The status characteristic is never refreshed (BOOT-7)

The web panel used to read the status characteristic to show device state. That does not work, and BOOT-7 changed the web to ask with the `get_status` command instead (`useDeviceSession.ts:88-90`). Confirmed against the firmware:

- `BootBootsBluetoothService::updateSystemStatus()` (`BluetoothService.cpp:105`) is the only thing that fills the cached `currentStatusJson` and notifies. A search of `src/` and `lib/` finds no caller. It was removed in commit `0522635` ("Disables auto status updates", Oct 2025). A 2-second broadcast from `SystemManager` was added in `4fa26ab` and reverted in `5fcff5b` (26 Mar 2026).
- `onRead` simply returns `currentStatusJson` (`:223-225`). That string is initialised empty and nothing else sets it, so a read yields a zero-length value, and `BleConnection.readStatus()` turns that into `null` (`bleTransport.ts:139-142`). The comment in `useDeviceSession.ts:88-89` says the characteristic "is only set at boot"; the code shows it is not set at all.
- Because nothing calls `updateSystemStatus`, the characteristic never sends a notification either. The web's `onStatusChanged` handler (`bleTransport.ts:111`) therefore never fires.
- The `get_status` command, in contrast, builds the response from live `SystemState` every time (`CommandDispatcher.cpp:116-158`).

Even if `updateSystemStatus` were called, its JSON (`formatSystemStatusJson`, `:114`) differs from the `get_status` response: it has no `type`, and it has a `timing` object that `get_status` lacks.

### OTA service (separate)

A second service on the same GATT server is used for BLE firmware updates. The web panel does not use it.

- Service `99db6ea6-27e4-434d-aafd-795cf95feb06`
- Command characteristic `1ac886a6-5fff-41ea-9b11-25a7dcb93a7e` (write, write without response)
- Status characteristic `5f5979f3-f1a6-4ce7-8360-e249c2e9333d` (read, notify)

Its protocol is different from the one above: commands use an `action` field (`ota_update` with `firmware_url` and `version`, `url_chunk`, `get_status`, `cancel_update`), several can be newline-joined in one write, and status notifications are `{"status","message","progress","version"}` (`BluetoothOTA.cpp:291-404`). Source: `lib/BluetoothOTA`.

## 5. Response and push message types

Every message the firmware sends in reply to a command, plus the pushes. "Source" gives the sending code.

| `type` | Fields | Sent by | Transport |
|---|---|---|---|
| `pong` | `timestamp` | `ping` | both |
| `status` | `device`, `timestamp`, `uptime_seconds`, `system` {`initialized`, `camera_ready`, `wifi_connected`, `sd_card_ready`, `i2c_ready`, `pcf8574_ready`, `atomizer_enabled`, `training_mode`}, `statistics` {`total_detections`, `boots_detections`, `atomizer_activations`, `false_positives_avoided`}, `peripherals` {`pir_active`, `flash_led_on`, `led_strip_on`, `spray_on`} | `get_status` | both |
| `settings` | `training_mode`, `trigger_threshold`, `dry_run`, `claude_infer` | `get_settings` | both |
| `camera_settings` | `camera` {25 keys} | `get_camera_settings` | both |
| `setting_updated` | `setting`, `value` | `set_setting`, `set_trigger_threshold`, `set_dry_run`, `set_claude_infer` | both |
| `peripheral_updated` | `peripheral`, `state`, `ok` | `set_peripheral` | both |
| `photo_started` | `message` | `take_photo` | both |
| `photo_complete` | `message`, `filename` | `take_photo` | both |
| `simulation_started`, `simulation_complete` | none | `simulate_detection` | both |
| `reboot_ack` | `message` | `reboot` | both |
| `version` | `version`, `project` | `get_version` | both |
| `error` | `message` | any failure | both |
| `ota_progress` | `progress`, `status`, optional `version` | `ota_update`, `url_chunk`, `ota_cancel` | both |
| `ota_error` | `message` | `ota_update`, `url_chunk` | both |
| `log_chunk`, `logs_complete` | `chunk` (1-based), `data`; `total_chunks` | `request_logs`, `get_logs` | BLE |
| `log_list_debug` | `message` | `list_logs` when no `.log` files are found | BLE |
| `log_list_chunk`, `log_list_complete` | `chunk` (0-based), `total`, `filename`; `count` | `list_logs` | BLE |
| `log_file_chunk`, `log_file_complete` | `chunk` (1-based), `data`; `filename`, `total_chunks` | `get_log_file` | BLE |
| `image_list_chunk`, `image_list_complete` | `chunk` (0-based), `total`, `filename`; `count` | `list_images` | BLE |
| `image_start`, `image_chunk`, `image_complete` | `filename`, `size`; `chunk` (0-based), `total`, `data` (base64); `filename`, `chunks` | `get_image` | BLE |
| `metadata_result` | `filename`, `found`, `content` | `get_image_metadata` | BLE |
| (no `type`) status push | flat: `device`, `timestamp`, `uptime_seconds`, `wifi_connected`, `camera_ready`, `training_mode`, `total_detections` | every 60 s on the `status` topic | MQTT |
| (no `type`) status characteristic | `device`, `timestamp`, `uptime_seconds`, `system`, `statistics`, `timing`, `peripherals` | `updateSystemStatus` (never called) | BLE |

The web reducer (`protocol.ts:193-406`) additionally handles `image_list` (a whole list in one message) and a bare JSON array of log lines; the current firmware never sends either (see mismatches). It does not handle `ota_progress` or `ota_error`.

## 6. BLE log commands

Log files live on the SD card in `/logs`, named by boot number and date; `list_logs` returns every `*.log` file sorted by name. Both commands are BLE-only and run synchronously in the main loop.

### `list_logs`

Request: `{"command":"list_logs"}`

Response sequence on the command characteristic (`BluetoothService.cpp:577-644`):

1. Only if no `.log` files were found: one `{"type":"log_list_debug","message":"No .log files found. ..."}` describing what the device saw in `/logs`.
2. For each file, 30 ms apart: `{"type":"log_list_chunk","chunk":<0-based>,"total":<n>,"filename":"<name>"}`.
3. Finally `{"type":"log_list_complete","count":<n>}`.

`log_list_debug` can therefore arrive before the completion with zero chunks; clients must not treat it as terminal.

### `get_log_file`

Request: `{"command":"get_log_file","filename":"<name>","entries":<N>}`. `entries` is optional; if present the device sends only the last N non-empty lines, otherwise the whole file. If `filename` is empty the device answers `{"type":"error","message":"Missing filename parameter"}` (`:290-304`). The filename is appended to `/logs/` unchecked (`SDLogger.cpp:675`), so it should be a name returned by `list_logs`.

Response sequence (`BluetoothService.cpp:646-681`):

1. For each non-empty line, 50 ms apart: `{"type":"log_file_chunk","chunk":<1-based line count>,"data":"<line>"}`.
2. `{"type":"log_file_complete","filename":"<name>","total_chunks":<lines sent>}`.

If the file cannot be opened the device sends a single `log_file_chunk` whose `data` is `error - Failed to open log file`, followed by `log_file_complete` with `total_chunks` 1. A client cannot tell this apart from a one-line log except by reading the text.

Two things to know when reading `data`:

- Backslashes and double quotes in each log line are escaped by the logger (`SDLogger.cpp:689-690`, `713-714`) and then escaped again when the message is serialised. After parsing the JSON, `data` still contains `\"` for each `"` in the original line and `\\` for each `\`. Neither the web panel nor `catcam_logs.py` undoes this.
- Each chunk message is built in a 512-byte JSON document (`:656`). A very long log line probably exceeds that, in which case ArduinoJson drops the `data` key and the chunk arrives without it. The web panel tolerates this (`str(r.data) ?? ''`); `catcam_logs.py` does `m["data"]` and would raise `KeyError`. I have not seen this happen.

`request_logs`/`get_logs` work the same way for the current boot's log, using `log_chunk` and `logs_complete`.

## 7. `embedded/catcam/scripts/catcam_logs.py`

A small command-line tool that pulls log files off the catcam's SD card over BLE, using `list_logs` and `get_log_file`. It scans for a device called `BootBoots-CatCam`, connects, enables notifications on the command characteristic (`bb00b007-c0de-49fa-89c5-31e705b74d87`), lists the log files, then downloads each one. Each file is written as `OUT_DIR/<filename>.partial` and renamed to `OUT_DIR/<filename>` only when `log_file_complete` arrives, so an interrupted run leaves no half-written file with a real name. Progress is printed every 500 lines.

Requirements:

- Python 3 with [`bleak`](https://github.com/hbldh/bleak). Use a virtual environment: `python3 -m venv .venv && .venv/bin/pip install bleak`.
- **Disconnect the browser first.** The device accepts one BLE connection at a time, so if the web panel (or any other client) is connected, the scan does not find the device and the script exits with "BootBoots-CatCam not found (is the browser still connected to it?)".

Usage:

```
python -I catcam_logs.py OUT_DIR [--list] [FILENAME ...]
```

- `OUT_DIR` is created if it does not exist.
- `--list` lists the files and exits without downloading.
- With no `FILENAME` arguments, every file the device lists is downloaded. Give one or more names (as printed by `--list`) to fetch only those.

`IDLE_TIMEOUT` is 180 seconds: the script gives up on a file if no notification arrives for that long. It was originally 60 seconds; on one run a single file stalled at 60 s and a retry went through cleanly, so the longer value gives a slow transfer more room. Because the device sends one line every 50 ms, a big file takes a while, and the firmware does nothing else during the transfer.

Worked example: on 29 Oct 2026 it fetched 10 files at MTU 515.

Limitations: it does not unescape the doubled backslash and quote escaping described in [section 6](#6-ble-log-commands), and it has no retry logic (a timeout raises and aborts the run, leaving a `.partial` file).

## Known mismatches

Where the firmware, the web panel, the tooling and the infrastructure disagree, or where something does not behave as its comment says. Each is cited so it can be checked or fixed.

1. **MQTT topic prefix: firmware `bootboots/...`, IoT policy and `bbdevice.py` `catcam/...`.** The firmware builds `bootboots/<thing>/commands|responses|status` (`embedded/catcam/lib/MqttService/src/MqttService.cpp:107-109`), and Fleet and the web use `{project}/{deviceId}/commands` with `project = 'bootboots'` (`web/src/config.ts`, `web/src/services/fleet.ts:49`). But the IoT policy created by the CDK allows only `catcam/*` (`infra/lib/iot-device.ts:114,119`), `embedded/catcam/scripts/bbdevice.py:29-30` is hard-coded to `catcam/BootBootsThing/...` (it only publishes, via the IoT data plane; it never reads responses), and the header comment of the MQTT service still documents `catcam/{thingName}/...` (`MqttService.h:35-37`). As written in the repo the policy would not authorise the topics the firmware uses, and `bbdevice.py` talks to topics the firmware never subscribes to. The deployed policy may have been changed outside this repo; I did not check it.
2. **BLE status characteristic is never populated or notified (BOOT-7).** `updateSystemStatus` has no caller (`BluetoothService.cpp:105`, no uses in `src/` or `lib/`), so `currentStatusJson` stays empty (`:82`, `:223-225`) and a read returns a zero-length value, which `readStatus()` converts to `null` (`web/src/device/bleTransport.ts:139-142`). The comment "only set at boot (firmware 1.0.331 never refreshes it)" (`web/src/device/useDeviceSession.ts:88-89`) is not accurate: it is never set at all, and the firmware is now 1.0.332 (`embedded/catcam/include/version.h`). `DeviceTransport.readStatus` (`web/src/device/types.ts:110-111`) and the `onStatusChanged` listener (`bleTransport.ts:111`) are therefore dead on the current firmware. `lib/BluetoothService/README.md` still describes the characteristic as automatically notifying.
3. **Remote page sends `list_images` over MQTT, which always fails.** `web/src/pages/RemotePage.tsx:90` sends `list_images` after selecting a device. The firmware rejects it on MQTT with "Command requires chunked transfer (use Bluetooth)" (`CommandDispatcher.cpp:73-78`), and the reducer turns every `error` into a visible error banner and clears loading flags (`web/src/device/protocol.ts:314-326`). `RemotePage.test.tsx:50` asserts that `list_images` is sent, so the test enshrines this. The web has no notion of which commands are BLE-only; `web/src/device/types.ts:1-2` claims "the same commands and responses apply to either transport", which is untrue for the six `CHUNKED_COMMANDS`.
4. **The periodic MQTT `status` push is ignored by the Remote page.** The push is flat (`MqttService.cpp:278-284`: `wifi_connected`, `camera_ready`, `training_mode`, `total_detections` at top level, no `type`, `system`, `statistics` or `peripherals`). `RemotePage.tsx:72-73` only applies messages on the `status` channel when `isSystemStatus` passes, which requires `system` and `statistics` keys (`web/src/device/types.ts:114-115`). So every push is silently discarded; live state on the Remote page comes only from `get_status` responses.
5. **`get_status` response has no `timing`, but the web type requires it.** `BootBootsSystemStatus.timing` is non-optional (`types.ts:90-93`). The `get_status` command response omits `timing` (`CommandDispatcher.cpp:116-158`), whereas the (never-sent) BLE characteristic format includes it (`BluetoothService.cpp:137-138`). The web does not read `timing`, so nothing breaks today, but the type is wrong for the data actually received. The two status formats also differ in `type` (present only in `get_status`).
6. **`get_image_metadata` is BLE-only in practice but not declared so.** It is absent from `CHUNKED_COMMANDS` (`CommandDispatcher.cpp:7-15`) and not registered with the dispatcher, so on MQTT it returns `Unknown command: get_image_metadata` instead of the chunking message. It is handled only in the BLE service (`BluetoothService.cpp:319`).
7. **Web handles `image_list`, firmware never sends it.** `web/src/device/protocol.ts:244-249` expects a single `image_list` message with an `images` array. The firmware only sends `image_list_chunk` and `image_list_complete` (`BluetoothService.cpp:382-403`). Likewise the "legacy firmware sends the whole log as one JSON array" branch (`protocol.ts:198-201`) is not produced by any current command.
8. **Web reads `camera` from the `settings` response; the firmware does not send it.** `protocol.ts:361` merges `r.camera` from a `settings` message, but `handleGetSettings` sends only `training_mode`, `trigger_threshold`, `dry_run` and `claude_infer` (`CommandDispatcher.cpp:166-171`). Camera settings only arrive in `camera_settings` (`:187-216`), which is why `requestSettings` sends both commands (`useDeviceSession.ts:91-94`). Dead code rather than a bug.
9. **Log lines are double-escaped.** `SDLogger::processLogFile` and its siblings escape `\` and `"` (`SDLogger.cpp:465-466`, `689-690`, `713-714`, also `390-391`, `427-428`, `494-495`), then ArduinoJson escapes the string again when serialising (`BluetoothService.cpp:659,263`). Clients that treat `data` as the plain log line (`protocol.ts:211`, `catcam_logs.py`) therefore show `\"` and `\\` where the log has `"` and `\`.
10. **Chunk numbering is inconsistent.** `log_chunk` and `log_file_chunk` number from 1 (`BluetoothService.cpp:258-263,653-658`, the counter is incremented before use), while `log_list_chunk`, `image_list_chunk` and `image_chunk` number from 0 (`:383-384,620-623,497-498`). The web copes because it keys chunks by index and sorts (`protocol.ts:109-114`), so it does not matter for correctness, but any client that assumes a start index will be off by one for logs.
11. **BLE failures are silent; MQTT failures are reported.** An over-long command (more than 511 bytes, `BluetoothService.cpp:212`) or invalid JSON (`:239-242`) over BLE produces no response, whereas the same input over MQTT gets an `error` (`CommandDispatcher.cpp:57-61`). A web action that sets a loading flag would wait for ever.
12. **Possible dropped back-to-back BLE commands.** The BLE service has a single pending-command slot (`BluetoothService.cpp:85-89,212-216`) which is only drained once per main-loop pass, and the loop sleeps 100 ms per pass (`main.cpp:418`). The web's `BleConnection` queue waits only for each GATT write to be acknowledged, not for the device to process it (`bleTransport.ts:105-109,133-137`). `DevicePage.tsx:100-102` and `useDeviceSession.ts:91-94` send two or three commands back to back, so a later one could overwrite an earlier one before it is processed. I have not reproduced this; it is inferred from the code.
13. **`catcam_logs.py` can raise on an over-long log line.** Each log chunk is serialised from a 512-byte document (`BluetoothService.cpp:656`); if a line overflows it, `data` may be missing and the script's `m["data"]` raises `KeyError`. Also unverified on hardware.
14. **Web `FLEET_RESERVED` and the firmware's OTA commands.** The firmware registers `ota_update`, `url_chunk` and `ota_cancel` on the shared dispatcher (`MqttOTA.cpp:19-28`), so they are reachable over MQTT and over BLE (via the dispatcher fallback at `BluetoothService.cpp:334-336`; note the BLE OTA service is a separate characteristic with a different `action`-based protocol, `BluetoothOTA.cpp:291-372`). The web blocks `ota_update` and `url_chunk` on its Fleet transport (`fleetTransport.ts:5`) but not `ota_cancel`, and never sends any of them over BLE. The web reducer has no case for `ota_progress` or `ota_error`.
15. **Dead BLE commands from the web's point of view.** The firmware accepts `get_logs` and `ota_cancel`, which the web never sends; the web never uses the Logs characteristic or the OTA service. Not a fault, but anyone tidying the protocol should know they exist.
16. **`get_log_file` takes an unchecked filename.** `SDLogger::processLogFile` builds `/logs/<filename>` without rejecting `/` or `..` (`SDLogger.cpp:675`). Any BLE client can name a path under the SD card root. Noted for completeness rather than as a protocol mismatch.
