# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Firmware + web UI for GaggiMate, a smart-control retrofit for Gaggia espresso machines. Two ESP32 firmwares talk to each other over BLE: a **controller** board that owns the hardware (heater, pump, valve, sensors) and a **display** unit that owns the UI, Wi-Fi, profiles, shot history, and the web interface. A Preact web app is embedded into the display firmware's flash.

## Commands

### Firmware (PlatformIO)

PlatformIO is not bundled. Install it once with:

```shell
uv tool install --with pip platformio
```

The `--with pip` is required: PlatformIO's package manager shells out to
`python -m pip` to install `tool-esptoolpy`'s dependencies, and a bare `uv tool
install` creates a venv without pip. Without it the first build dies with
`MissingPackageManifestError` after a misleading `No module named pip`. If you
hit that, `rm -rf ~/.platformio/packages/tool-esptoolpy` before retrying — the
package is left half-installed.

The first `pio run` downloads the espressif32 toolchain (a large one-time fetch).

```shell
pio run -e display                      # display unit (LilyGo T-RGB touchscreen)
pio run -e display-headless             # display firmware with no panel/LVGL
pio run -e display-headless-8m          # ditto, seeed_xiao_esp32s3
pio run -e controller                   # controller board
pio run -e display -t upload -t monitor # flash + serial console
pio run -t buildfs -e display           # LittleFS image (seed profiles only)
```

### Web UI

```shell
cd web && npm install
npm run dev          # http://localhost:5173, proxies /api and /ws to localhost:8080
npm run build
npm run lint         # eslint --fix
npm run format       # prettier -w .
```

`scripts/build_webui.sh` is the firmware-facing build: `npm ci && npm run build`, gzips the assets, then `scripts/embed_webui.py` packs them into `src/display/webassets/` (git-ignored) for embedding into flash. Run it before `pio run -e display` if you changed the web UI. `scripts/embed_webui_pre.py` stubs an empty bundle when it hasn't been run, so a firmware build never hard-fails on a missing bundle — it just serves nothing.

The bundle reaches flash via `.incbin` in a `.S` file, which **no dependency
scanner can see**: the build decides whether to reassemble by hashing the `.S`,
and that doesn't change when the blob it pulls in does. A stale object therefore
survives every later build, and the firmware then serves every asset out of
whatever rodata follows the symbol — correct `Content-Length`, correct MIME type,
garbage body, no error anywhere. `embed_webui.py` stamps the blob's size and
digest into the `.S` to force a rebuild, `embed_webui_pre.py` drops objects older
than `web_ui.bin` (the simulator's hand-written stub has no stamp), and
`scripts/check_webui_blob.py` compares the linked bytes against `web_ui.bin`
post-link and fails the build. Don't remove those without replacing them.

Node 22 (`nvm use`, see `.nvmrc`).

### Simulator

Runs the real display firmware natively with the BLE controller mocked and an SDL window as the panel. Fastest way to check a UI or process change. Needs SDL2 (`brew install sdl2`).

```shell
pio run -e display-sim -t run                                  # build + launch
./.pio/build/display-sim/program --screenshot shot.bmp 4000    # render, screenshot, exit
```

WebUI is served at <http://localhost:8080/> while it runs — which is exactly what `npm run dev` proxies to, so you can run the Vite dev server against the simulator. State persists under `sim_data/`. See `sim/README.md` for what's compiled out (MQTT, HomeKit, mDNS, BLE scales, OTA, watchdogs).

Copy real profiles into `sim_data/littlefs/p/` and shots into `sim_data/littlefs/h/`
before reviewing UI work — empty states hide most layout problems.

For headless screenshots, don't use Chrome's `--screenshot` with
`--virtual-time-budget`: it fires before the WebSocket delivers anything, so
every page looks empty or shows the disconnected overlay, and the capture lies to
you. Drive Chrome over CDP (`--remote-debugging-port`) with real `sleep`s and
`Page.captureScreenshot` instead. Two related traps when scripting interaction:
`element.click()` from `Runtime.evaluate` grants **no** transient user activation
(so anything gated on it, e.g. the clipboard API, behaves differently than for a
real user — use `Input.dispatchMouseEvent`), and a build passing tells you
nothing about whether a component renders. A `ReferenceError` in a component body
is valid at parse time; load each route and watch for `Runtime.exceptionThrown`.

### Tests

```shell
pio test -e native_autotune              # host-side Unity tests for the SIMC autotuner
pio test -e native_autotune -f test_autotune_simc
```

Only `test/test_autotune_simc` exists. It direct-`#include`s `Autotune.cpp` so the native linker skips NayrodPID's Arduino-dependent siblings. There is no on-device test suite.

### Lint / format / static analysis

```shell
scripts/format.sh                        # clang-format over src, lib, sim
platformio check -e display              # cppcheck; CI uses --fail-on-defect=medium
platformio check -e controller
npx prettier -w <file>.md
```

`scripts/format.sh` deliberately skips `src/display/ui/**` and `src/display/drivers/**` — generated and vendored code. Don't reformat those.

`npm run lint` is not a reliable syntax gate — it has reported 0 errors on a
`.jsx` file that Vite then refused to parse. Use `npm run build` to confirm the
web UI compiles. Likewise don't pipe `npx prettier --write` to `/dev/null`: a
parse failure is reported there and nowhere else, and prettier leaves the file
unchanged when it can't parse it.

## Architecture

### Two firmwares, one protocol

`lib/NanoPbComm` defines the whole display↔controller link and is shared by both builds:

- `proto/gaggimate.proto` — nanopb schema. A `Frame` carries a sequence id, an ack, and one or more `Payload`s; `Payload` is a `oneof` tagged union dispatched by tag (no runtime type erasure). Generated `gaggimate.pb.{c,h}` are **not committed** — PlatformIO's nanopb integration regenerates them each build from `custom_nanopb_protos` in `platformio.ini`.
- `Endpoint.{h,cpp}` — transport-agnostic reliable session: coalescing priority send queue, one frame in flight until ACKed, id-based dedup of inbound frames, handlers dispatched on a dedicated task so slow app callbacks can't block the BLE host task.
- `ble/` (BLE client + server transports) and `uart/` (future/alternate transport) implement `Transport`.
- `GaggiMateClient` (display side) and `GaggiMateServer` (controller side) are the app-facing facades.

Protocol version mismatches and pre-framing controllers fall back to an OTA-recovery-only path (`Controller::onIncompatibleController`).

### Controller firmware (`src/controller/`, `lib/GaggiMateController/`)

Thin `main.cpp`; everything lives in `GaggiMateController`. It detects the board variant (`ControllerConfig`, `boards/*.json`) and addon hardware at boot, instantiates peripherals under `peripherals/` (`Heater`, `DimmedPump`/`SimplePump`, `SimpleRelay`, `Max31855Thermocouple`, `PressureSensor`, `FlowSensor`, `DistanceSensor`, `ADSAdc`, `LedController`), and owns the safety layer: thermal-runaway shutdown and a ping-timeout watchdog (`PING_TIMEOUT_SECONDS`) that kills outputs when the display goes quiet. Most peripherals run their own FreeRTOS task.

`lib/NayrodPID` holds the control math used here: `PressureController` (pressure/flow/power modes with a pump-flow + rotary-vane-slip model), Kalman filters, `HydraulicParameterEstimator`, and `Autotune` (SIMC integrator+lag rule).

### Display firmware (`src/display/`)

`core/Controller` is the hub. Task layout matters:

- Arduino `loop()` — housekeeping only, 50 ms cadence.
- `Controller::loopLogic` — own task pinned to core 0, priority 3 (`Controller::loopLogicTask`).
- `DefaultUI::loop` — own task pinned to core 1 (LVGL).
- `Settings::loop` — a task that batches NVS writes.
- AsyncTCP runs on core 0 (`CONFIG_ASYNC_TCP_RUNNING_CORE=0`) — see the comment in `platformio.ini`; moving it off core 0 wedged the IP stack.

Consequences to respect when editing:

- `currentProcess`/`lastProcess` are guarded by `Controller::processMutex` (recursive). Holding the pointer returned by `getProcess()` without the lock is a use-after-free. The `*Locked` helpers collect event ids and the public wrappers dispatch them **after** unlocking, so plugin handlers never run under the lock.
- Wi-Fi connect/disconnect is only _flagged_ from the Arduino Wi-Fi event task (`wifiConnectedPending`/`wifiDisconnectedPending`) and acted on in `loop()` — doing server/mDNS work in that small-stack callback corrupted the heap.
- `updateControl()` only transmits boiler/pump/relay components that changed since `lastBoiler`/`lastPump`/`lastRelay`; these reset on reconnect to force a full resend.

**Plugins and events.** `PluginManager` holds `Plugin`s (`setup()` + `loop()`) and a string-keyed event bus (`on(id, cb)` / `trigger(...)`, `Event` carries a small typed key/value list). Registration order is in `Controller::setup()` (`src/display/core/Controller.cpp` ~line 80). Existing plugins: `WebUIPlugin`, `ShotHistoryPlugin`, `BLEScalePlugin`, `MQTTPlugin` (Home Assistant), `HomekitPlugin`, `mDNSPlugin`, `BoilerFillPlugin`, `SmartGrindPlugin`, `LedControlPlugin`, `AutoWakeupPlugin`, `ImprovPlugin`, and two network watchdogs. New cross-cutting features belong here, not in `Controller`.

Event id conventions: `controller:*` for machine state, `evt:*` for things pushed to the web UI, `req:*`/`res:*` for the WebSocket request/response pairs.

**Processes.** `core/process/` — `BrewProcess`, `SteamProcess`, `PumpProcess`, `GrindProcess` implement `Process` (`getPumpValue()`, `isRelayActive()`, `progress()`, `isComplete()`, ...). Exactly one runs at a time via `Controller::startProcess`.

**Profiles.** `models/profile.h` defines the shape: a profile is a list of `Phase`s (preinfusion/brew), each with a pump target (pressure or flow), a `Transition` (instant/linear/ease*, over time/volumetric/pumped), and exit `Target`s. `ProfileManager` persists JSON under `/p` on LittleFS and handles schema migration. Favourites are the comma-separated id list in the `fp` setting, not the `favorite` field inside each profile file; the profile screen steps through `selected + favourites` in that order. `PhaseExitReason` values are persisted in shot logs — **never renumber them**.

**Storage.** Settings live in NVS via `Preferences` (`Settings.h`, key `controller`). LittleFS holds only `/p` (profiles) and `/h` (shot history `.slog` binary + `.json` notes), so OTA never touches user data. The web bundle is memory-mapped from firmware flash, not the filesystem.

**UI.** LVGL 8.4. `ui/default/DefaultUI` is hand-written; `ui/default/eez/` is generated by EEZ Studio from `eez-ui/gaggimate.eez-project` — regenerate, don't hand-edit. Bitmaps live inside the `.eez-project` as base64 PNGs and are compiled into `eez/images/ui_image_*.c`; to swap one without EEZ Studio, replace the base64 (and the image widget's `width`/`height` if the size changed) and run `scripts/lvgl_img.py <png> img_<name> <ui_image_<name>.c>` — it reproduces EEZ's converter exactly (`--check` verifies against every bitmap in the project). The standby wordmark is `img_logo`, a black-on-transparent alpha mask rasterized at its on-screen size (394×79, drawn at zoom 256 — never zoom at runtime, see Device performance) that LVGL recolors to the theme's foreground, so only its alpha channel matters. Rasterize SVGs with `uv run --with resvg-py` (no rsvg/ImageMagick on this machine). When `screens.c` does get hand-edited (EEZ Studio isn't installed here), mirror the change into the `.eez-project` so an export reproduces it — edit the JSON as text to keep the diff small, and verify by comparing `json.loads` of the result against the same edit applied to the parsed tree. Anything that needs a new compiled flow expression (a widget's Hidden flag, a binding) cannot be hand-written: the expression lives in `eez-flow.cpp`'s binary blob. Set it in the project and mirror it in `tick_screen_*` with a comment saying so (see the Brew screen's arc buttons). Presses on an EEZ screen land on its full-size containers, not the screen object, and only gestures bubble up — for screen-wide touch logic, poll the indev from `DefaultUI::loop()` (the profile dial) instead of adding a press handler to the screen. `drivers/` has the panel drivers (LilyGo T-RGB, Waveshare, Amoled) behind `Driver.h`. All of this compiles out under `GAGGIMATE_HEADLESS`.

### Web UI (`web/`)

Preact + Vite + Tailwind 4 / daisyUI, signals for state. `services/ApiService.js` owns the WebSocket to `/ws` (with reconnect/backoff) and exposes a `machine` signal; pages under `src/pages/`. All messages are JSON with a `tp` field — `req:` from client, `res:`/`evt:` from device. The contract is documented in `docs/websocket-api.yaml` (AsyncAPI); keep it in sync when adding a message type. The HTTP surface (settings partial update, REST profile CRUD, shot history downloads) is documented in `docs/http-api.yaml` (OpenAPI) — same rule. `sim/tests/` holds curl-based end-to-end suites for the HTTP API that run against the simulator. Bulk data (shot history index, `.slog` files) goes over plain HTTP under `/api/history/`.

daisyUI 5's `.input` is a flex **wrapper**, not a style for an `<input>`:
`display:inline-flex; position:relative`. Use `<label class="input">` around an
icon plus `<input class="grow">` (see `ProfileList`). Putting `.input` on the
`<input>` itself makes it a positioned element later in DOM order than any
absolutely-positioned sibling icon, so its background paints over the icon, and
`inline-flex` centres the placeholder.

Adding a theme takes four edits: the `@plugin "daisyui/theme"` block in
`src/style.css`, `AVAILABLE_THEMES` in `utils/themeManager.js`, `DARK_THEMES` in
`utils/chartTheme.js` (or charts render with light-theme colours), and the
hardcoded `<option>` list in `Settings/tabs/GeneralTab.jsx`. `getAvailableThemes()`
exists but nothing calls it, which is why the last one is needed.

`vite.config.js` forces hash-only asset filenames — the device's filesystem caps path length at 32 chars and silently drops longer paths. Don't restore default chunk names.

JSON schemas for profiles, shot history, and notes are in `schema/`.

### OTA (`lib/OTA`, `lib/ble_ota_dfu`)

`GitHubOTA` pulls display firmware from the update server / GitHub releases; `ControllerOTA` pushes the controller image over BLE DFU. Channels: `latest` / `nightly` (`DEFAULT_OTA_CHANNEL`).

`POST /api/ota/upload` (header `X-OTA-Token`, `?target=fs` for a LittleFS image) pushes an image without a cable, but slowly: ESPAsyncWebServer parses multipart bodies one byte at a time, so a 4.5 MB image takes ~3 minutes at ~26 KB/s on a good network, versus ~50 s over USB. Pass `-H "Expect:"` to curl — the `100-continue` dance stalled one upload for five minutes with nothing sent. A failure returns `{"error": "<stage>: <reason> at N bytes", "received": N}`; a bare `Aborted` means an older firmware that lost the reason. A 30 s stall aborts the upload server-side so a dead connection cannot hold the OTA slot.

## Conventions

- clang-format, LLVM base, 4-space indent, 130-col limit (`.clang-format`).
- `src/version.h` is generated at build time by `scripts/auto_firmware_version.py` from `git describe` — git-ignored, never commit it.
- **Do not create non-semver git tags.** `auto_firmware_version.py` runs `git describe --tags --dirty --exclude nightly --exclude db`, and whatever comes back becomes `BUILD_GIT_VERSION`. `lib/OTA`'s `from_string()` splits it on `.` — a string with fewer than three dot-separated parts used to throw `std::out_of_range` from `GitHubOTA`'s constructor during `Controller::setup()`, with no handler above it, so the display boot-looped and needed a USB reflash. That is why `nightly` and `db` are excluded. `from_string()` now returns 0.0.0 instead of throwing, but a device still running older firmware will brick, so prefer branches over tags for local bookmarks.
- Code comments reference Linear issue ids (`GM-106`, `GM-147`, ...). Follow that when a comment explains a non-obvious fix.
- Commits are Conventional-Commit-ish: `fix:`, `feat:`, `chore:`, often with a `(#PR)` suffix.
- Contributions require a signed CLA (see `CONTRIBUTING.md`). Note that `CONTRIBUTING.md` still refers to a `ui/` SquareLine Studio project — the current source is `eez-ui/` (EEZ Studio).
- `/api/settings` is unauthenticated. It echoes Wi-Fi and AP passwords back **only**
  in AP mode, where the caller had to know the AP password to reach the device;
  on a shared network both are replaced with `PASSWORD_PLACEHOLDER`
  (`---unchanged---`). Every POST handler for a credential must skip that
  sentinel, or saving an unedited settings form stores the placeholder as the
  password. Don't widen what this endpoint discloses.
- License: CC BY-NC-SA 4.0.

## Device performance

The sim shows motion faithfully but says nothing about frame time; the panel is
the only honest source. `DefaultUI` logs `standby exit: N frames in M ms` over
serial at 115200 after each transition — 7 frames for a 200 ms exit means the UI
is running at its 40 Hz cadence, 3–5 means something is stalling it. Things that
have stalled it:

- **A fixed sleep after each frame.** `DefaultUI::loopTask` used to `vTaskDelay(25)`
  _after_ `loop()`, so the frame period was 25 ms plus render time (~43 ms on the
  panel, and uneven). It now uses `vTaskDelayUntil` with a 25 ms period; the sim
  mirrors that cadence in `sim/main.cpp`. Don't reintroduce a trailing sleep.
- **Runtime image zoom.** `lv_img_set_zoom` != 256 sends every redraw through the
  software resampler. Pre-scale bitmaps to their on-screen size instead (the
  wordmark is rasterized at 394×79 for exactly this reason) and animate opacity,
  never zoom.
- **WebSocket client churn.** `DEFAULT_MAX_WS_CLIENTS=3` plus `ws.cleanupClients()`
  in `loop()` evicts the oldest client whenever a fourth connects; the evicted tab
  reconnects, evicts another, and each reconnect re-requests the profile list
  (~2 s of flash reads, which stall both cores' caches). Four browser tabs made
  the whole UI stutter. If the machine feels laggy, count `WebSocket client
connected` lines on serial before blaming the UI code — ~30 in 45 s means tabs.
  Closing tabs fixed it; evicting newcomers instead was rejected as worse UX.

The panel itself is the ceiling: `RGB_MAX_PIXEL_CLOCK_HZ` is 7 MHz in
`drivers/LilyGo-T-RGB/utilities.h`, and with the configured porches that is a
**23.5 Hz** physical refresh — the UI's 40 Hz loop already outruns it. Smoother
motion means a higher pixel clock (10 MHz ≈ 34 Hz, 12 ≈ 40), which risks drift
and tearing because the framebuffer is in PSRAM with no bounce buffers; bounce
buffers need ~20 KB of internal RAM, and the device had ~57 KB free (largest
block 39 KB) on 2026-08-31 — the same RAM the IP stack starves without. Untested.

`scripts/lvgl_img.py` and `scripts/phosphor_icons.py` regenerate bitmaps without
EEZ Studio; the sim's `--tap`, `--drag`, `--arc`, `--scale` and the `s` hotkey are
how screens and gestures are exercised headlessly (see `sim/README.md`).

## Working on this Mac

- zsh does not word-split an unquoted `$var`: `P="$bin $flags"; $P` passes one argument. Use an array (`W=(--scale --tap ...)`; `"${W[@]}"`). Quote globs meant for the tool, e.g. `--include='*.cpp'`, or zsh errors with "no matches found".
- There is no `timeout`; use `perl -e 'alarm 30; exec @ARGV' -- <cmd>`.
- The display's USB port renames between enumerations (`usbmodem1101` → `usbmodem101`). Resolve it with `ls /dev/cu.usbmodem*` every time; never hard-code it.
- Serial capture without PlatformIO's interactive monitor: `uv run --with pyserial python -c ...` at 115200, read lines for N seconds, write to a file.
- `screencapture` of another app's window fails ("could not create image") without Screen Recording permission for the terminal; capture the sim with its `s` hotkey instead.
- Never start a step that depends on the user doing something at the machine (tapping, plugging in) until they have said so in a message — turns are slow and they are often not watching.

## Debugging

Core dumps: download from the device web UI (System & Updates → Download Core Dump) or the `/api/core-dump` endpoint, then:

```shell
./scripts/analyze_coredump.sh <coredump_file> [display|controller|display-headless]
```

Serial monitor has `esp32_exception_decoder` enabled by default at 115200.
