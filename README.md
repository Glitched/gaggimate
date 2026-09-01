<h1 align="center">Exhalation</h1>

<p align="center">
<em>Espresso machine control firmware — pressure, flow and temperature profiling.</em>
</p>

> [!NOTE]
> **Exhalation is an unofficial fork** of [GaggiMate](https://github.com/jniebuhr/gaggimate) by
> [@jniebuhr](https://github.com/jniebuhr), modified and maintained independently. It is not
> affiliated with or endorsed by the upstream project, and upstream cannot support it. For
> hardware kits, assembly guides and community, see [gaggimate.eu](https://gaggimate.eu/).

Exhalation retrofits a Gaggia espresso machine with a controller board and a touchscreen
display. The controller owns the heater, pump, valve and sensors; the display owns the UI,
Wi-Fi, profiles, shot history and an embedded web app. The two talk over BLE.

The name comes from _Exhalation_, the Ted Chiang short story about a world that runs
entirely on a difference in air pressure. So does espresso.

## Features

- **Profiles** — multi-phase brew profiles with pressure or flow targets, timed, volumetric
  and pumped-volume phase exits, and instant, linear or eased transitions between them.
- **Modes** — brew, steam, hot water and grind, each with its own process and targets.
- **Shot history** — every shot is logged on the device with notes, a rating and dose
  in/out, and can be browsed, exported or copied out of the web UI.
- **Web UI** — a Preact app served from the display over Wi-Fi: live status, profile
  editor, shot history, settings and OTA updates.
- **Bluetooth scales** — volumetric phase exits and brew-by-weight with supported scales.
- **Integrations** — MQTT with Home Assistant discovery, HomeKit, mDNS and Improv Wi-Fi
  provisioning.
- **Autotune** — SIMC PID autotuner for the boiler.
- **Safety** — thermal-runaway shutdown on the controller and a ping watchdog that kills
  the outputs if the display goes quiet.
- **OTA** — display updates from GitHub releases, controller updates pushed over BLE, and
  raw-body firmware upload over HTTP for cable-free flashing.

## Changes in this fork

- **Token-efficient shot export** — a _Copy for LLM_ action in the shot history that emits
  YAML frontmatter plus a CSV body: roughly 6% the tokens of the JSON export with no samples
  dropped, and carrying the recorded phase exit reasons and planned-vs-actual durations that
  the JSON export leaves out.
- **`npm run scrape`** — pulls shots off a running machine over HTTP and formats them through
  the same parsers and analyzer the web UI uses, so output cannot drift from what the UI shows.
- **`CLAUDE.md`** — build commands and architecture notes for working in this repository.
- **Device UI icons** — the on-device icon set is [Phosphor Icons](https://phosphoricons.com)
  (Regular weight), regenerated from the mapping in `scripts/phosphor_icons.py`.
- **HTTP API for everything** — every command and query (`POST /api/mode`, `/api/process/...`,
  `/api/targets/...`, `/api/ota`, profile and shot-history CRUD) is a plain HTTP route documented
  in `docs/http-api.yaml`; the WebSocket carries only the device's `evt:*` pushes. Firmware
  images upload as a raw body to `/api/ota/upload`.

See the git history for the complete record of modifications.

## Building

Firmware builds with [PlatformIO](https://platformio.org/); the web UI needs Node 22.

```shell
pio run -e display                      # display unit (LilyGo T-RGB touchscreen)
pio run -e controller                   # controller board
scripts/build_webui.sh                  # build the web UI and embed it for the display build
pio run -e display-sim -t run           # run the display firmware natively against a mocked controller
```

`CLAUDE.md` covers the rest: every environment, the simulator, the test suites, formatting
and the architecture of both firmwares.

## Documentation

- [`docs/http-api.yaml`](docs/http-api.yaml) — OpenAPI description of the HTTP API: commands,
  queries, settings, profiles, shot history and OTA.
- [`docs/websocket-api.yaml`](docs/websocket-api.yaml) — AsyncAPI description of the `evt:*`
  messages the device pushes over `/ws`.
- [`schema/`](schema/) — JSON schemas for profiles, shot history and shot notes.
- [`sim/README.md`](sim/README.md) — the native simulator; [`sim/tests/README.md`](sim/tests/README.md)
  — end-to-end API suites that run against it.
- [`CLAUDE.md`](CLAUDE.md) — build commands, architecture and conventions.

## Attribution and License

This repository is a **modified derivative** of
[GaggiMate](https://github.com/jniebuhr/gaggimate), created by
[@jniebuhr](https://github.com/jniebuhr) and its contributors.

The original work is licensed under [CC BY-NC-SA 4.0][cc-by-nc-sa]. This fork is distributed
under that same license, and has been changed from the original as described in
[Changes in this fork](#changes-in-this-fork).

Non-commercial use only. "GaggiMate" and "Gaggia" are marks of their respective owners; they
are not licensed under CC BY-NC-SA and are used here solely to identify the upstream project
and the hardware this software runs on.

The on-device icons are [Phosphor Icons](https://phosphoricons.com) by Helena Zhang and Tobias
Fried, used under the [MIT License](https://github.com/phosphor-icons/core/blob/main/LICENSE).

Upstream community and hardware kits: [gaggimate.eu](https://gaggimate.eu/) ·
[upstream Discord](https://discord.gg/APw7rgPGPf) — please don't raise issues about this fork there.

[cc-by-nc-sa]: http://creativecommons.org/licenses/by-nc-sa/4.0/
