# Bench scripts

Measurement tools for the display, written during the 2026-09 heap and panel work. They only
read the HTTP API (`docs/http-api.yaml`) plus, for flashing, USB. Every judgement in CLAUDE.md's
hardware findings came from these; keep using them the same way:

- Take the same probes **before and after** a change, on the same day, and interleave (A, B, A)
  when the Wi-Fi air varies: on 2026-09-04 the same firmware swung between 100 KB/s and 3 KB/s
  within twenty minutes.
- Judge the panel by `panelUnderruns` (0 at rest is the baseline), the heap by `heapMinFree`,
  sockets by `wsSkippedFrames`/`wsDisconnects`, the controller link by
  `link.retransmitsWindow`, never by eye or by feel.
- `GET /api/ota` walks LittleFS on every call; poll it once per 10 s window, not faster.

All scripts take the host as the first argument (default `gaggimate.local`; the IP is faster
from another VLAN, where mDNS goes through a reflector).

| script | what it does |
|---|---|
| `probe.sh [host]` | Wi-Fi path in one go: three bundle downloads, ten pings, a 512 KB upload, three status requests, a 30 s socket, link/RSSI. |
| `watch_shot.sh [host] [seconds] [interval]` | Heap, mode, panel counters and brew checkpoints through a shot (every 3 s, panel figures every 12 s). |
| `watch_health.sh [host] [minutes]` | One line a minute for hours: heap, largest block, clients, skipped frames, drops, RSSI, link retransmits, HTTP latency, top task. Arms CPU sampling (`?cpu=1`), which costs one shifted panel frame per 10 s while it runs; leave it off during panel measurements. |
| `flash_write_test.sh [host] [rounds]` | Creates and deletes a throwaway profile N times (LittleFS writes and erases) and reads the panel window: the shot stand-in for tearing during flash writes. |
| `ws_probe.py [host] [seconds]` | Opens a WebSocket and counts pushed frames; a server-side close shows as a close code. `uv run --with websockets`. |
| `link_watch.sh [host] [seconds]` | Per-window controller-link retransmits, RTT, RSSI, socket figures every 20 s. |
| `ota_push.sh <firmware.bin> [host]` | Raw-body OTA upload with the token from `$GM_OTA_TOKEN` (or `--token-file`), then waits for the device to come back. |
| `usb_flash.sh <firmware.bin>` | USB flash to app0 with the OTA record erased so app0 boots (after an OTA the device runs app1; see CLAUDE.md "OTA slots vs USB"). |
| `ab.sh <imageA.bin> <imageB.bin> [host]` | A, B, A over USB with `probe.sh` after each: the interleaved comparison. |
| `serial_capture.py [seconds] [file]` | Reads the USB-Serial-JTAG console at 115200 (the device only emits with DTR asserted). |
