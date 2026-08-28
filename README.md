<h1 align="center">Exhalation</h1>

<p align="center">
<em>Espresso machine control firmware — pressure, flow and temperature profiling.</em>
<br /><br />

[![CC BY-NC-SA 4.0][cc-by-nc-sa-shield]][cc-by-nc-sa]

</p>

> [!NOTE]
> **Exhalation is an unofficial fork** of [GaggiMate](https://github.com/jniebuhr/gaggimate) by
> [@jniebuhr](https://github.com/jniebuhr), modified and maintained independently. It is not
> affiliated with or endorsed by the upstream project, and upstream cannot support it.
>
> For the original project — hardware kits, assembly docs and community — see
> [gaggimate.eu](https://gaggimate.eu/). What differs here is listed under
> [Changes in this fork](#changes-in-this-fork).

This project upgrades a Gaggia espresso machine with smart controls to improve your coffee-making experience. By adding a display and custom electronics, you can monitor and control the machine more easily.

<img src="docs/assets/gaggimate_poster.jpg" alt="Gaggia Classic Installation" width="500" />

## Features

- **Temperature Control**: Monitor the boiler temperature to ensure optimal brewing conditions.
- **Brew timer**: Set a target duration and run the brewing for the specific time.
- **Steam and Hot Water mode**: Control the pump and valve to run the respective task.
- **Safety Features**: Automatic shutoff if the system becomes unresponsive or overheats.
- **User Interface**: Simple, intuitive display to control and monitor the machine.

## Screenshots and Images

<img src="docs/assets/standby-screen.png" alt="Standby Screen" width="300px" />
<img src="docs/assets/brew-screen.png" alt="Brew Screen" width="300px" />
<img src="docs/assets/pcb_render.png" alt="PCB Render" width="300px" />

### How to buy

You can buy your kit on https://shop.gaggimate.eu/

## How It Works

The display allows you to control the espresso machine and see live temperature updates. If the machine becomes unresponsive or the temperature goes too high, it will automatically turn off for safety.

## Docs

The docs were moved to [https://gaggimate.eu/](https://gaggimate.eu/). You can find all sourcing and assembly information there.
Additional documentation for the WebSocket API can be found in [docs/websocket-api.yaml](docs/websocket-api.yaml).

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

See the git history for the complete record of modifications.

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
[cc-by-nc-sa-image]: https://licensebuttons.net/l/by-nc-sa/4.0/88x31.png
[cc-by-nc-sa-shield]: https://img.shields.io/badge/License-CC%20BY--NC--SA%204.0-lightgrey.svg?style=for-the-badge
