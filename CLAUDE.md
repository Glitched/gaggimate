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

The first `pio run` downloads the toolchain (a large one-time fetch).

The platform is **pioarduino** (`platform = https://github.com/pioarduino/platform-espressif32/...`,
55.03.311 = Arduino core 3.3.11 on ESP-IDF 5.5.5, GCC 14), migrated from
`espressif32@6.12.0` (Arduino 2.0.17 / IDF 4.4.7) on 2026-09-01. Two consequences:

- pioarduino replaces `~/.platformio/penv` with a uv-created venv that has **no pip**.
  nanopb's PlatformIO integration installs its generator deps with `python -m pip`,
  which then fails silently and the build dies with `No module named 'google'`.
  Fix once per machine (and it is what CI does after `pio pkg install`):
  `uv pip install --python ~/.platformio/penv/bin/python protobuf grpcio-tools`.
- `espressif32@6.12.0` and pioarduino both install `framework-arduinoespressif32` and
  `tool-esptoolpy` into the **same** `~/.platformio/packages/<name>` directory, with no
  version suffix, so each platform's first build re-downloads its own copy over the other's.
  Building a checkout on the old platform and this one alternately therefore re-installs
  packages every time and, if the two builds overlap, one of them fails half-way
  (`FRAMEWORK_DIR` None, `esptool.py` not found). One platform per machine, or serialize.
- Upstream (jniebuhr/gaggimate) is still on `espressif32@6.12.0`, so merges that touch
  Arduino-2-only APIs need porting: `WiFiClient(Secure)` -> `NetworkClient(Secure)`
  (`setCACertBundle` now takes a size), `ledcSetup/ledcAttachPin` -> `ledcAttach(pin, ...)`,
  `esp_adc_cal_*` -> `analogReadMilliVolts`, `sntp_*` -> `esp_sntp_*`,
  `esp_lcd_rgb_panel_config_t` lost `on_frame_trans_done`/`user_ctx` and renamed
  `psram_trans_align` -> `dma_burst_size` (field order matters: `disp_gpio_num` before
  `data_gpio_nums`), and core 3 no longer leaks `using namespace std` (write `std::vector`).
  GCC 14 also rejects `[[noreturn]]` on a definition whose declaration lacks it.
  Library floors that came with it: NimBLE-Arduino 2.5 (`NimBLEConnInfo&` callbacks,
  `NimBLEScanCallbacks`, `getVal()` instead of `getNative()`, times in ms), HomeSpan 2.1,
  SensorLib 0.4 (`IoExpanderXL9555` with integer pins, `IoExpanderSPI` for the bit-banged
  panel SPI, `getTouchPoints()`), Arduino_GFX 1.6 (`RGB565_BLACK`, `CO5300_TFTWIDTH`, no
  `ips` ctor arg) and esp-arduino-ble-scales v2.0.0. Don't reintroduce version guards for
  the old core; the branch requires the new versions.

**Hardware findings from the first flash of this branch (2026-09-01, T-RGB, rolled back
afterwards).** Read these before flashing it again:

- **Settings loaded as defaults.** `Controller controller;` is a global, so
  `Settings::Settings()` runs before `initArduino()` calls `nvs_flash_init()`. On core 2 that
  happened to work; on core 3 the first `Preferences::begin` fails with `NOT_INITIALIZED`
  (logged at 1 ms), every setting is its default, the SSID is empty and the device boots
  into AP mode -- while the BLE transport, which opens NVS seconds later, works. The fix is
  `nvs_flash_init()` in the constructor plus `Settings::reload()` at the top of
  `Controller::setup()`. The only setter that runs unconditionally on such a boot is the AP
  password generator, so a defaults boot regenerates the AP password and changes nothing
  else. Core 3's Preferences also logs `nvs_get_str len fail: <key> NOT_FOUND` for every key
  that was never persisted; that is noise, not corruption.
- **Internal heap: 81 KB free -> 14 KB free (largest block 70 KB -> 7.6 KB), fresh boot,
  same feature set.** pioarduino's prebuilt `qio_opi` sdkconfig has
  `CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_INTERNAL=y`, no `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP`,
  `CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC=y` and `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=0`, i.e.
  the NimBLE host, the Wi-Fi/LWIP buffers and TLS all live in internal RAM where the old
  Arduino 2 build put them in PSRAM. That is not enough headroom for a TLS OTA session or
  for LCD bounce buffers (~20 KB). **This is the blocker for merging the branch.** The fix
  is pioarduino's hybrid build: set `custom_sdkconfig` in `platformio.ini` (it pulls
  `framework-espidf`, cmake and ninja and rebuilds the Arduino libs; expect a long first
  build) with at least `CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL=y`,
  `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y`, `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y`,
  `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=32768`; consider
  `CONFIG_LCD_RGB_RESTART_IN_VSYNC=n` too (see next point). Re-measure with `GET /api/ota`.
  **Measured 2026-09-02 with exactly those four options:** 43 KB free / 32 KB largest block /
  308 KB total internal on a fresh boot (old firmware 81 / 70 / 294; prebuilt config 14 / 7.6 /
  276). Boot floor (`heapMinFree`) 29.7 KB. TLS no longer draws on internal RAM, so OTA is
  unblocked. **Attributed on 2026-09-02 with `GET /api/debug/heap`** (boot-stage checkpoints,
  fresh boot, internal heap free after each stage): setup start 171 KB; filesystem + panel
  -5.7 KB; UI object + profiles -10 KB; plugins constructed and set up -21.6 KB; UI init +
  logic task -14 KB; Wi-Fi driver -8.5 KB; **BLE init -48.2 KB**; Wi-Fi connected (web server,
  mDNS) -17 KB; steady state ~43 KB. The BLE cost is the controller-side library (always
  internal) sized by IDF's hub defaults, hence the `CONFIG_BT_CTRL_*` trims in the
  `custom_sdkconfig` block. Those trims (max activities 4, adv-report buffers 50, scan dup
  cache 30, NimBLE connections 2, esp_timer stack 4 KB) plus two of our task stacks brought
  steady state to **55 KB free / 32 KB largest / 40 KB boot floor** on 2026-09-02. A shot
  takes another ~14 KB, so the in-shot floor is now ~40 KB (it was 18.9 KB before the trims),
  which is the number to protect before spending internal RAM on anything (bounce buffers
  need ~19 KB). Both cores idle at 92-95 %. Left on the table: the BLE mesh duplicate-scan
  cache (~3 KB) cannot be disabled from the ini block (see the comment there); and the
  steady-state figure moves a few KB with what the web UI is doing, so compare the checkpoint
  table, not the `now` block, between builds.
- **The hybrid build is in place** (one `custom_sdkconfig` block in the `[custom_sdk]` section of
  `platformio.ini`, referenced by every ESP env, 2026-09-02): the
  first `pio run` downloads `framework-espidf`, cmake and ninja, links a dummy sketch to
  compile the IDF libraries with the overrides (about 3 minutes on this Mac), copies them
  into `framework-arduinoespressif32-libs`, then builds the real project. The rebuilt
  `sdkconfig.h` under that package is the place to confirm an option took. The dummy pass
  has no web bundle, so `scripts/check_webui_blob.py` skips itself when
  `ARDUINO_LIB_COMPILE_FLAG` is `Build`; keep that guard. The pass leaves `.dummy/`,
  `managed_components/`, `sdkconfig.defaults` and `sdkconfig.<env>` in the project root
  (git-ignored; `sdkconfig.defaults` carries the hash pioarduino uses to decide whether the
  cached libs match). The libs are keyed on the block's text plus the board's memory layout
  (not on anything else: a `components/` directory in the repo root is compiled into the
  library pass as an IDF project component, but changing it does not invalidate cached libs,
  and the app links such a component as an archive placed before the IDF libraries, so a symbol
  the IDF libraries need from it must also be compiled into the app as an object; see commit
  ccec8eac for a worked example). The pass leaves a generated `CMakeLists.txt` and
  `dependencies.lock` in the repo root, both git-ignored. Since 2026-09-02 `[env:display]`
  extends the shared block (PSRAM code/rodata, see The panel), so it owns its own lib set and CI
  recompiles the libraries once more per run:
  controller, display and display-headless (all qio_opi) share one set and were verified to
  build back to back without a recompile; display-headless-8m (qio_qspi) gets its own and
  re-triggers the ~3 min compile step whenever it is alternated with the others. An env
  _without_ the block reinstalls the stock libs, so never add an ESP env without
  `custom_sdkconfig = ${custom_sdk.custom_sdkconfig}`. CI inherits the download and the
  compile step on every run (the hash marker `sdkconfig.defaults` is not cached).
- **The panel needs bounce buffers on IDF 5.** The RGB driver resets the DMA at every VSYNC
  (`CONFIG_LCD_RGB_RESTART_IN_VSYNC=y`), so a DMA stall shows as a one-frame shift instead of
  a permanent tear. On every build since the migration the standby screen jumped every 1-5 s
  at rest: `panelUnderruns` 4-5 per 10 s window with nothing polling and no web client, and
  Ryan confirmed it never happened on the Arduino 2 firmware (2026-09-02). Instruction and
  data cache sizes, cache line size, flash (80 MHz DIO) and PSRAM (80 MHz octal) clocks are
  identical between the old and new images, so the difference is in the traffic on the shared
  flash/PSRAM bus: the hybrid build keeps the Wi-Fi, LWIP, NimBLE host and TLS buffers in
  PSRAM, and the LCD FIFO only has tens of microseconds of slack. Two bounce buffers
  (`PANEL_BOUNCE_LINES` in `LilyGo_RGBPanel.cpp`) took the count to 0 in every window at
  rest, with 10 lines (2 x 9.6 KB, ~0.7 ms of slack: 35 KB free / 18 KB boot floor / 20 KB
  largest) and with 5 lines (2 x 4.8 KB, ~0.34 ms: 45 / 23 / 32 KB; 52 / 40 / 32 without).
  5 is the setting; Ryan confirmed the standby screen steady on 10 and the counter judged 5.
  The refill is a CPU copy from PSRAM in the driver's EOF interrupt, so
  `CONFIG_LCD_RGB_ISR_IRAM_SAFE` and `CONFIG_GDMA_ISR_IRAM_SAFE` had to go: with the cache
  off during a flash write the copy would fault instead of waiting. While the cache is off
  the buffers are not refilled and the panel shows repeating rows instead of a shifted
  frame; an OTA write makes that continuous for a minute (Ryan saw it on 2026-09-02), and
  blanking the backlight for the write phase would have been the cosmetic fix. **Solved the
  same evening by running code and rodata from PSRAM** (`CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y` +
  `CONFIG_SPIRAM_RODATA=y` in the display env's block): with nothing fetched from flash, the
  flash driver takes a mutex instead of disabling the cache around erases and page writes
  (`spi_flash_os_func_app.c`, `SPI_FLASH_CACHE_NO_DISABLE`), and the IRAM-safe LCD/GDMA
  interrupts (back in the shared block) keep refilling the bounce buffers through the write.
  Write test (six profile creates and deletes in ~16 s, `panelUnderruns`/`panelLateVsyncs` in
  the overlapping window): 9/2 before; 0/0 with this. Costs ~4.4 MB of PSRAM (2.2 MB left),
  nothing internal, and code now runs from the faster octal PSRAM. The alternative,
  `CONFIG_SPI_FLASH_AUTO_SUSPEND`, was measured first (commit ccec8eac): it needs a chip
  whitelist (IDF 5.5 only lists the W25Q64; the T-RGB has a W25Q128, `esptool flash-id` says
  0xEF4018), an unlisted chip asserts at boot in `esp_flash_init_default_chip` (the bootloader
  rolls back), and even with a custom driver list it left 5 underruns per write window because
  every cache miss to flash parks the bus for a suspend handshake. The UI loop
  itself is fine: `standby exit: 8 frames in
188 ms`, 40 fps, a full-screen flush ~27 ms. `GET /api/ota` reports `uiFps`, `flushAvgUs`,
  `flushMaxUs`, `panelVsyncHz`, `panelLateVsyncs`, `panelUnderruns` and `heapMinFree` (10 s
  window, also one `render:` line per 10 s over serial), so none of this needs a cable.
- **OTA slots vs USB.** `POST /api/ota/upload` writes the _inactive_ app slot and flips
  `otadata`; `esptool write-flash 0x10000` only rewrites `app0`. After an OTA the device
  boots `app1`, so a USB flash of app0 changes nothing and you will debug a stale image
  (this cost an hour). Either erase `otadata` first (`esptool erase-region 0xe000 0x2000`,
  the bootloader then falls back to app0) or flash the active slot (`app1` is at 0x650000).
- **Serial on the T-RGB** is the S3's USB-Serial-JTAG (`ARDUINO_USB_MODE=1`): it only emits
  when the host asserts DTR, and RTS pulses EN. To capture a boot log open the port with
  DTR/RTS low, pulse RTS high for 150 ms, then set DTR high and read
  (`scripts/bench/boot_capture.py`, which restarts the display; `serial_capture.py` reads
  without resetting). Steady
  state is silent at `CORE_DEBUG_LEVEL=3`; a tap out of standby logs the frame timing.
- **`GET /api/debug/heap`** returns the checkpoint table (`heapCheckpoint("label")` calls in
  `Controller::setup()`, the wifi:connect dispatch and brew start/end), the current internal
  and PSRAM figures, and every FreeRTOS task with stack headroom and cumulative CPU share.
  Add a checkpoint around anything you suspect; the table holds 32. The OTA upload's stall
  check once aborted a healthy push with "no data for 4294967242 ms" (a millis() race across
  tasks); it now compares signed and fresh.
- **Static RAM is half the budget.** On the S3, IRAM and DRAM are carved from the same SRAM,
  so every byte of code the linker places in IRAM (except the first 16 KB, which sit in SRAM0
  next to the instruction cache) is a byte the heap never sees; the linker records the mirror
  as `.dram0.dummy`. On 2026-09-02 the display image had 105 KB of IRAM code (89.5 KB
  mirrored), 31.5 KB `.data` and 63 KB `.bss`: 184 KB decided at link time against a ~306 KB
  heap. `python3 scripts/ram_budget.py [firmware.map]` attributes all three to their archives
  and CI prints it after the display build. The big IRAM users are the BLE controller (20 KB),
  FreeRTOS (16 KB) and the flash driver (11 KB). **Moving code out of IRAM tears the panel.**
  `CONFIG_FREERTOS_PLACE_FUNCTIONS_INTO_FLASH=y` + `CONFIG_RINGBUF_PLACE_FUNCTIONS_INTO_FLASH=y`
  gave 12.5 KB of heap (IRAM 105.5 -> 93 KB, measured 319 KB total / 66 KB free) and the
  standby screen jittered and clipped at rest within minutes of the OTA on 2026-09-02; rolled
  back the same hour. Mechanism: flash and PSRAM sit behind the same cache and SPI bus, so
  kernel code that now misses the 16 KB icache is fetched over the bus the RGB DMA needs for
  the PSRAM framebuffer, and the DMA falls behind the pixel clock. Anything that adds flash
  fetches to hot paths (these options, `CONFIG_BT_CTRL_RUN_IN_FLASH_ONLY`, cutting the icache)
  is off the table on a PSRAM-framebuffer board; the lever that goes the other way,
  `CONFIG_SPIRAM_FETCH_INSTRUCTIONS` + `CONFIG_SPIRAM_RODATA` (code and rodata copied into
  PSRAM at boot, which also keeps the cache on during flash writes), is untested and is the
  next tearing experiment. Arduino's config already keeps the Wi-Fi and LWIP code out of IRAM
  (`ESP_WIFI_IRAM_OPT`, `LWIP_IRAM_OPTIMIZATION` unset). Fewer than 4 BLE activities breaks
  Improv advertising next to a scan and two connections. The Wi-Fi "cache TX" queue is not an
  internal-RAM lever either: it holds pointers to PSRAM packets waiting for one of the 8
  internal staging buffers, and 8 is already Arduino's floor.
- **After two days up, the web UI showed "Machine disconnected — reconnecting…" and nothing
  loaded (2026-09-04, 51 h uptime, PR #10 build).** The device was still running: HTTP answered
  in 0.5-1.6 s instead of ~90 ms, and the WebSocket library closed any client whose outbound
  queue reached 8 frames (`setCloseClientOnQueueFull(true)`, kept on purpose so a stalled
  client cannot pin internal RAM), which a fast client hit every 7-10 s because the display
  could not push two status frames a second. The browser reconnects on close, a fourth client
  evicts the oldest, and the tabs chase each other. With every tab closed a single client
  stayed up for 40 s and HTTP fell to 0.2 s, so the loop was the amplifier, not the cause.
  Underneath: 47 -> 39 KB free, largest block 32 -> 16 KB, heap floor 3.8 KB during shots
  (20 KB two days earlier), BLE link 15 % retransmits. The cause of the slow transmit path
  is unknown; the CPU table in `GET /api/debug/heap` could not say because FreeRTOS's run-time
  counter is 32-bit microseconds and wraps after 71 min, so shares since boot are noise on a
  device that is days old. Fixed the same day: status frames are skipped for a client that
  still has 2 queued (`WS_DROP_QUEUE_LEN`) instead of queued until the library closes it;
  `cpuPct` is now the share over the last 10 s window (two samples, wrap-safe); `GET /api/ota`
  carries `wsClients`, `wsSkippedFrames`, `wsDisconnects`, `wifiRssi` and per-window link
  `txFramesWindow`/`retransmitsWindow`, and the `render:` serial line carries the same. Next
  time it degrades, run `watch_shot.sh`-style polling for hours and read those. Ryan power
  cycles the display himself; do not restart it remotely.
- **The Wi-Fi trouble of 2026-09-04 was the air, measured.** A USB A/B the same evening
  (old Arduino 2 image, new stack, old again; three bundle downloads, ten pings, a 512 KB
  upload, a 30 s socket per round) put the old image at 4-25 KB/s and 105-236 ms pings and the
  new one at 7-83 KB/s and 99-329 ms: same air, same numbers, and the old image took over
  three minutes to join once. Three OTA uploads stalled at the 30 s abort that evening; the
  cable (`usb_flash.sh`-style: erase `otadata`, write app0) is the path when the air is like
  that. BLE retransmits at the Endpoint layer ran ~35 % on the new stack with the Wi-Fi radio
  awake and asleep alike (75 frames each way), ~25-30 % on the old image from smaller samples;
  keep an eye on `link.retransmitsWindow`, and treat give-ups, not retransmits, as the alarm.
  `WiFi.setSleep(false)` stays (mains power; ping floor 8-12 ms instead of 30-60 ms, no BLE
  cost measured). The Mac and the display sit on different UniFi VLANs, so `gaggimate.local`
  needs the mDNS reflector (3-35 s lookups here) and every request crosses the gateway; use
  the IP when measuring, and test from the display's own SSID before blaming the device.
- **`panelLateVsyncs` cannot see tearing; `panelUnderruns` can.** The LCD peripheral generates
  the panel timing on its own, so VSYNC keeps arriving at 23.5 Hz while the DMA starves; a
  late VSYNC only means the interrupt was held off. With `CONFIG_LCD_RGB_RESTART_IN_VSYNC`
  the driver resets the DMA at every VSYNC and reports a frame complete only when the DMA
  wrapped its descriptor link first, so VSYNCs minus completions per 10 s window is the number
  of frames the viewer saw shifted or torn. `GET /api/ota` reports it as `panelUnderruns`
  (also in the `render:` serial line). Nothing is consumed during the porches, so a DMA
  stall longer than the LCD FIFO's slack (tens of microseconds) can never be made up within
  the frame: the counter catches every frame with any shift, and says nothing about how big
  the shift was. Baseline measured 2026-09-02 on the T-RGB at rest with a web client
  connected: 2-5 per 10 s window (sub-line shifts from Wi-Fi/BLE/PSRAM traffic, not visible
  on the dark standby screen); hammering `/api/profiles` + `/api/status` for 15 s took a
  window to 10. Judge a config or driver experiment against those numbers, not against the
  panel by eye; the FreeRTOS-in-flash build that visibly jittered was never measured with it.
  `GET /api/ota` used to walk LittleFS (`usedBytes()`) on every call; with code in PSRAM that
  no longer switches the cache off, but the burst of flash reads still holds the SPI bus past
  the bounce slack, and the poller measured exactly one underrun per poll through a whole shot
  (2026-09-04). The figures are cached for 60 s now (`display/util/StorageStats`), also for the
  end-of-shot free-space check; nothing that is polled may walk the filesystem. The other
  once-per-window tear was the task sampler behind `cpuPct`: `uxTaskGetSystemState()` holds
  the kernel lock with interrupts off for longer than the 0.34 ms bounce slack, one shifted
  frame per sample, measured in silent windows with nothing polling. It now runs only for
  60 s after `GET /api/debug/heap?cpu=1`; anything that blocks interrupts on the LCD's core
  for more than the slack will show up the same way.
- NimBLE 2 logs `W NimBLEClient: unknown handle: 14` a few times while connecting to the
  controller, then connects and exchanges system info normally. Not yet investigated.
- **The controller image from this branch has not run on hardware.** It builds against the
  same rebuilt libraries as the display, so its BLE host now allocates from PSRAM (the
  board declares PSRAM). Nothing pushes it to the machine by itself: the wire protocol is
  unchanged, and the display only sends a controller update when the button is pressed. Do
  not apply a controller update from a nightly built off this branch until that image has
  been flashed once over USB and watched on serial (pairing, ping watchdog, a heat-up).

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
npm run dev          # http://localhost:5173, proxies /api and /ws to localhost:8080 (strips Origin)
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

WebUI is served at <http://localhost:8080/> while it runs — which is exactly what `npm run dev` proxies to, so you can run the Vite dev server against the simulator. State persists under `sim_data/`. See `sim/README.md` for what's compiled out (MQTT, HomeKit, mDNS, BLE scales, OTA upload, watchdogs).

Any edit to `platformio.ini` changes PlatformIO's project checksum and the next `pio run`
of **any** env wipes all of `.pio/build/`, including the sim binary. If the e2e suites
report "simulator not reachable" right after a dependency bump, rebuild the sim first.

Flags: `--scale` pretends a Bluetooth scale is connected (the weight row and volumetric UI only render with one); `--tap X,Y@MS`, `--drag X0,Y0>X1,Y1@T0~T1` and `--arc CX,CY,R,A0,A1@T0~T1` inject touches, so screens behind an interaction can be captured headlessly; pressing `s` in the window writes `sim_shot_N.bmp` of the live frame. The sim runs the UI loop at the device's 25 ms cadence, so frame counts match hardware; a `--screenshot` run additionally uses a hidden window and ignores the real mouse. Arc angles are screen-convention: 0 = right, clockwise positive, **270 = top**.

Copy real profiles into `sim_data/littlefs/p/` and shots into `sim_data/littlefs/h/`
before reviewing UI work — empty states hide most layout problems. Favourites and
the device theme are settings, not files: `fp` (comma-separated profile ids) and
`theme` (0 = dark) in `sim_data/nvs/controller.json`. Ryan only uses dark.

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

Only `test/test_autotune_simc` exists as a unit test. It direct-`#include`s `Autotune.cpp` so the native linker skips NayrodPID's Arduino-dependent siblings. There is no on-device test suite.

The HTTP API has black-box end-to-end suites that run against the simulator (`sim/tests/README.md`):

```shell
SDL_VIDEODRIVER=dummy ./.pio/build/display-sim/program --scale &   # headless sim
./sim/tests/test-machine-api.sh       # mode, process, targets, OTA status, history, reorder, cache
./sim/tests/test-profiles-api.sh      # profile CRUD, validation, the push-only socket contract
./sim/tests/test-settings-api.sh
```

Each suite waits for the port, snapshots what it changes (selected profile, favourites, the settings keys it touches, mode and target temperature) and restores it from an EXIT trap, so `sim_data/` is left as found — but a suite killed mid-run (SIGKILL, not Ctrl-C) skips the trap, so don't read the sim's state as ground truth right after an aborted run.

### Lint / format / static analysis

```shell
scripts/format.sh                        # clang-format over src, lib, sim
platformio check -e display              # cppcheck; CI uses --fail-on-defect=medium
platformio check -e controller
npx prettier -w <file>.md
```

Both `check` envs set `check_skip_packages = yes`: PlatformIO's bundled cppcheck (2.11) cannot
parse the GCC 14 libstdc++ headers in pioarduino's toolchain and otherwise reports a
"breaking" syntax error in every file. Keep that when touching the check configuration.

`scripts/format.sh` deliberately skips `src/display/ui/**` and `src/display/drivers/**` — generated and vendored code. Don't reformat those. The exclusions are `-path 'src/display/ui/*'` with no `./` prefix because `find src` prints paths without one; an earlier `./src/display/ui/**/*` pattern matched nothing and would have reformatted 81 files. `scripts/format.sh --check` is the CI form. `clang-format` is not installed on this Mac (`brew install clang-format` if you want the script to run); until then, match the style by hand and check `awk 'length > 130'` on touched files.

`npm run lint` once reported 0 errors on a `.jsx` file that Vite then refused
to parse: ESLint 8 given a directory only picks up `.js`, so until 2026-09-01
the lint scripts had never seen a component file. They now pass
`--ext .js,.jsx` (keep that if the scripts are touched). Lint still is not a
build; use `npm run build` to confirm the web UI compiles. Likewise don't pipe `npx prettier --write` to `/dev/null`: a
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

Thin `main.cpp`; everything lives in `GaggiMateController`. It detects the board variant and addon hardware at boot (the per-board pin and capability tables are C++ literals in `ControllerConfig.h`; `boards/*.json` are PlatformIO board definitions — partition table, PSRAM flags — not pin maps), instantiates peripherals under `peripherals/` (`Heater`, `DimmedPump`/`SimplePump`, `SimpleRelay`, `Max31855Thermocouple`, `PressureSensor`, `FlowSensor`, `DistanceSensor`, `ADSAdc`, `LedController`), and owns the safety layer: thermal-runaway shutdown and a ping-timeout watchdog (`PING_TIMEOUT_SECONDS`) that kills outputs when the display goes quiet. Most peripherals run their own FreeRTOS task.

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

**HomeKit.** HomeSpan 2.x declares a global `class Controller` (its paired-controller
record) that collides with ours, so `HomeSpan.h` is included from exactly one translation
unit, `plugins/HomekitBridge.cpp`, which never sees `Controller.h`; `HomekitPlugin`
talks to it through the plain-types `HomekitBridge` interface. Keep it that way (and keep
`HomekitBridge.cpp` in the sim's `build_src_filter` exclusions).

**Plugins and events.** `PluginManager` holds `Plugin`s (`setup()` + `loop()`) and a string-keyed event bus (`on(id, cb)` / `trigger(...)`, `Event` carries a small typed key/value list). Registration order is the sequence of `registerPlugin` calls in `Controller::setup()` (`src/display/core/Controller.cpp`). Existing plugins: `WebUIPlugin`, `ShotHistoryPlugin`, `BLEScalePlugin`, `MQTTPlugin` (Home Assistant), `HomekitPlugin`, `mDNSPlugin`, `BoilerFillPlugin`, `SmartGrindPlugin`, `LedControlPlugin`, `AutoWakeupPlugin`, `ImprovPlugin`, and two network watchdogs. New cross-cutting features belong here, not in `Controller`.

Event id conventions: `controller:*` for machine state, `evt:*` for things pushed to the web UI over the WebSocket. (There are no `req:*`/`res:*` pairs any more — commands and queries are HTTP routes.)

**Processes.** `core/process/` — `BrewProcess`, `SteamProcess`, `PumpProcess`, `GrindProcess` implement `Process` (`getPumpValue()`, `isRelayActive()`, `progress()`, `isComplete()`, ...). Exactly one runs at a time via `Controller::startProcess`.

**Profiles.** `models/profile.h` defines the shape: a profile is a list of `Phase`s (preinfusion/brew), each with a pump target (pressure or flow), a `Transition` (instant/linear/ease*, over time/volumetric/pumped), and exit `Target`s. `ProfileManager` persists JSON under `/p` on LittleFS and handles schema migration. Favourites are the comma-separated id list in the `fp` setting, not the `favorite` field inside each profile file; the profile screen steps through `selected + favourites` in that order. `PhaseExitReason` values are persisted in shot logs — **never renumber them**.

**Storage.** Settings live in NVS via `Preferences` (`Settings.h`, key `controller`). LittleFS holds only `/p` (profiles) and `/h` (shot history `.slog` binary + `.json` notes), so OTA never touches user data. The web bundle is memory-mapped from firmware flash, not the filesystem.

**UI.** LVGL 8.4. `ui/default/DefaultUI` is hand-written; `ui/default/eez/` is generated by EEZ Studio from `eez-ui/gaggimate.eez-project` — regenerate, don't hand-edit. Bitmaps live inside the `.eez-project` as base64 PNGs and are compiled into `eez/images/ui_image_*.c`; to swap one without EEZ Studio, replace the base64 (and the image widget's `width`/`height` if the size changed) and run `scripts/lvgl_img.py <png> img_<name> <ui_image_<name>.c>` — it reproduces EEZ's converter exactly (`--check` verifies against every bitmap in the project). The standby wordmark is `img_logo`, a black-on-transparent alpha mask rasterized at its on-screen size (394×79, drawn at zoom 256 — never zoom at runtime, see Device performance) that LVGL recolors to the theme's foreground, so only its alpha channel matters. Rasterize SVGs with `uv run --with resvg-py` (no rsvg/ImageMagick on this machine). When `screens.c` does get hand-edited (EEZ Studio isn't installed here), mirror the change into the `.eez-project` so an export reproduces it — edit the JSON as text to keep the diff small, and verify by comparing `json.loads` of the result against the same edit applied to the parsed tree. Anything that needs a new compiled flow expression (a widget's Hidden flag, a binding) cannot be hand-written: the expression lives in `eez-flow.cpp`'s binary blob. Set it in the project and mirror it in `tick_screen_*` with a comment saying so (see the Brew screen's arc buttons). Presses on an EEZ screen land on its full-size containers, not the screen object, and only gestures bubble up — for screen-wide touch logic, poll the indev from `DefaultUI::loop()` (the profile dial) instead of adding a press handler to the screen. `drivers/` has the panel drivers (LilyGo T-RGB, Waveshare, Amoled) behind `Driver.h`. All of this compiles out under `GAGGIMATE_HEADLESS`.

### Web UI (`web/`)

Preact + Vite + Tailwind 4 / daisyUI, signals for state. `services/ApiService.js` owns the WebSocket to `/ws` (with reconnect/backoff) and exposes a `machine` signal; pages under `src/pages/`. The socket is **push-only**: the device sends `evt:*` JSON messages (a `tp` field names the type) and ignores inbound frames; `docs/websocket-api.yaml` (AsyncAPI) lists them. Everything else — commands, queries, settings, profile CRUD, shot history, OTA — is an HTTP route documented in `docs/http-api.yaml` (OpenAPI); `web/src/services/api.js` is the client the pages use, and `curl` works the same way. Keep both documents in sync when adding a message or route. The `req:*`/`res:*` request-response messages were removed on 2026-08-31; do not reintroduce request handling on the socket. `sim/tests/*.sh` are curl-based end-to-end suites for the HTTP API that run against the simulator (see Tests).

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

`POST /api/ota/upload` (header `X-OTA-Token`, `?target=fs` for a LittleFS image) pushes an image without a cable. The image is the **raw request body** (`Content-Type: application/octet-stream`; `curl --data-binary @firmware.bin`), not a multipart form: ESPAsyncWebServer parses multipart one byte at a time and managed ~26 KB/s — three minutes per image, and two aborted attempts — before this was changed. Measured on 2026-09-01: a 4.4 MB image uploads in 85–105 s (42–51 KB/s), the device answers `{"status":"ok","restarting":true}` and is back on Wi-Fi ~35 s later; verified by pushing a `-dirty` build and reading its version back from `GET /api/ota`. Pass `-H "Expect:"` to curl; the `100-continue` handshake once stalled an upload with nothing sent. A failure returns `{"error": "<stage>: <reason> at N bytes", "received": N}`; a bare `Aborted` means firmware older than 2026-08-31 that lost the reason. A 30 s stall aborts the upload server-side so a dead connection cannot hold the OTA slot. USB (`pio run -e display -t upload`) is still ~50 s if the cable is in.

## Conventions

- clang-format, LLVM base, 4-space indent, 130-col limit (`.clang-format`).
- `src/version.h` is generated at build time by `scripts/auto_firmware_version.py` from `git describe` — git-ignored, never commit it.
- **Do not create non-semver git tags.** `auto_firmware_version.py` runs `git describe --tags --dirty --exclude nightly --exclude db`, and whatever comes back becomes `BUILD_GIT_VERSION`. `lib/OTA`'s `from_string()` splits it on `.` — a string with fewer than three dot-separated parts used to throw `std::out_of_range` from `GitHubOTA`'s constructor during `Controller::setup()`, with no handler above it, so the display boot-looped and needed a USB reflash. That is why `nightly` and `db` are excluded. `from_string()` now returns 0.0.0 instead of throwing, but a device still running older firmware will brick, so prefer branches over tags for local bookmarks.
- Code comments reference Linear issue ids (`GM-106`, `GM-147`, ...). Follow that when a comment explains a non-obvious fix.
- Commits are Conventional-Commit-ish: `fix:`, `feat:`, `chore:`, often with a `(#PR)` suffix.
- Contributions require a signed CLA (see `CONTRIBUTING.md`).
- `/api/settings` is unauthenticated. It echoes Wi-Fi and AP passwords back **only**
  in AP mode, where the caller had to know the AP password to reach the device;
  on a shared network both are replaced with `PASSWORD_PLACEHOLDER`
  (`---unchanged---`). Every POST handler for a credential must skip that
  sentinel, or saving an unedited settings form stores the placeholder as the
  password. Don't widen what this endpoint discloses.
- Cross-site protection is one middleware in `WebUIPlugin::setup`: a write
  (anything but GET) whose browser `Origin` does not match `Host` gets 403, and
  `bufferJsonBody` only buffers `application/json`. curl sends no Origin and
  passes; the Vite dev proxy strips the header (`web/vite.config.js`). The sim's
  server shim implements the same `addMiddleware` hook so the e2e suite covers
  it. There is still no authentication: a LAN client can set `otaUploadToken`
  through this endpoint and then upload firmware. Ryan chose not to add auth for
  now (2026-09-01); don't add it unasked, and don't remove the Origin check.
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
- **Listing profiles from flash.** `GET /api/profiles` read and parsed every profile
  file per request — ~0.9–2 s for ten on the panel, stalling both cores' caches.
  It is now served from a PSRAM cache keyed on `ProfileManager::getRevision()`;
  a cached list costs the same as `GET /api/status` (~90 ms, all Wi-Fi). Bump the
  revision from any new code path that changes a profile, the selection, the
  favourites or the order, or the list will be stale.
- **Runtime image zoom.** `lv_img_set_zoom` != 256 sends every redraw through the
  software resampler. Pre-scale bitmaps to their on-screen size instead (the
  wordmark is rasterized at 394×79 for exactly this reason) and animate opacity,
  never zoom.
- **WebSocket client churn.** `DEFAULT_MAX_WS_CLIENTS=3` plus `ws.cleanupClients()`
  in `loop()` evicts the oldest client whenever a fourth connects; the evicted tab
  reconnects and evicts another. With the old request-over-socket design every
  reconnect also re-fetched the profile list from flash, and four browser tabs
  made the whole UI stutter. The pages now fetch once over HTTP and the list is
  cached, so churn is cheap — but it still happens with more than three tabs. If
  the machine feels laggy, count `WebSocket client connected` lines on serial
  before blaming the UI code (~30 in 45 s means tabs). Evicting newcomers instead
  of the oldest was rejected as worse UX; closing tabs is the remedy.

The panel itself is the ceiling: `RGB_MAX_PIXEL_CLOCK_HZ` is 7 MHz in
`drivers/LilyGo-T-RGB/utilities.h`, and with the configured porches that is a
**23.5 Hz** physical refresh — the UI's 40 Hz loop already outruns it. Smoother
motion means a higher pixel clock (10 MHz ≈ 34 Hz, 12 ≈ 40), which risks drift
and tearing because the framebuffer is in PSRAM; the two 5-line bounce buffers
added on 2026-09-02 give the DMA ~0.34 ms of slack, not a faster panel. The device had ~57 KB free (largest block
39 KB) on 2026-08-31; removing the WebSocket request path raised that to ~82 KB
(largest 71 KB) on 2026-09-01, with ~6.7 MB of PSRAM free — `GET /api/ota`
reports all four figures. Untested.

`scripts/lvgl_img.py` and `scripts/phosphor_icons.py` regenerate bitmaps without
EEZ Studio (see the UI paragraph under Architecture).

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

### Bench scripts

`scripts/bench/` holds the measurement tools from the 2026-09 heap and panel work (Wi-Fi
probes, shot and health pollers, the flash-write panel test, socket and link watchers, OTA and
USB flashing, an interleaved A/B). Its README states the discipline: same probes before and
after, interleaved when the air varies, judged by the counters. Use them before trusting a
feeling about the device.

### Internal RAM

Three views, cheapest first:

- `python3 scripts/ram_budget.py` — the static side (IRAM mirror, `.data`, `.bss`) by
  contributor, from the linker map. No device needed; see the hardware findings above for
  what the numbers mean and which knobs remain.
- `GET /api/debug/heap` — the runtime side: boot-stage and brew checkpoints, current internal
  and PSRAM figures, every task with its stack headroom.
- **`pio run -e display-heaptrace`** — the display build with ESP-IDF's standalone heap tracer
  started from a global constructor, so every live allocation carries the call stack that
  made it. OTA it, use the machine (pull a shot, open the web UI), then
  `scripts/heap_trace_report.py --host 192.168.1.145 --elf .pio/build/display-heaptrace/firmware.elf`
  fetches `GET /api/debug/heap/trace` and prints live internal and PSRAM bytes per owning
  function and per source directory (`POST /api/debug/heap/trace/reset` clears the records to
  trace a single shot from a clean baseline). The env's sdkconfig block differs from the
  shared one, so alternating it with `display` re-triggers the ~3 min library compile each
  way, and every malloc pays a 12-frame backtrace while it runs, so never leave it on a
  device you judge UX with: the tracer's critical section plus its PSRAM writes starve the
  panel DMA (93 underruns per 10 s window at rest, 2026-09-05), which is expected and goes
  away with the stock image. It carries the same Origin check as every other write route.
  Three things it needs that took four flashes to find (2026-09-05): its routes are
  registered _before_ `/api/debug/heap`, because the server matches a registered uri
  against every `<uri>/<subpath>` too and the first handler wins; the constructor adds
  PSRAM to the heap itself (`psramAddToHeap()`), because Arduino core 3 does that from
  `initArduino()`, after every global constructor, so Arduino's own call then logs
  "PSRAM could not be added to the heap!" once at boot; and the env's `build_flags` carry
  `-Wl,--wrap=heap_caps_malloc_base` and its three siblings, because the tracer only sees
  allocations through those wraps and IDF's CMake applies them to its own dummy link, not
  to PlatformIO's. Without the wraps tracing starts and every dump says `allocs=0`.
  A dump mid-shot streams ~250 KB through the TCP stack and costs ~10 KB of internal RAM
  itself; read `heapMinFree` from a shot without a dump.
  **First session (2026-09-05, two puck-less shots, web UI open):** steady state attributes
  218 KB in 676 blocks: BLE controller + coex 42 KB, task stacks ~70 KB, the 32 KB DMA reserve
  pool, 62 FreeRTOS queues 9 KB, bounce buffers 9.6 KB; ours: profile phase vectors 5.1 KB,
  the web route table 3.7 KB (32 handlers), plugin listeners 3.1 KB (79). A shot sits ~13 KB
  below awake-idle while running (17 KB floor at the end); live at any instant: a 4 KB newlib
  stdio buffer for the open shot file (the Arduino `File` wrapper uses `fopen`, and the shot
  writer already batches into its own 4 KB buffer, so it is double-buffered; the index file
  at the end adds a second one), the 0.7 KB LittleFS file cache, 1.4 KB of process and
  profile copies; the rest is churn. **The standing finding is the scale scan:** awake with
  no scale connected, `BLEScalePlugin` runs a continuous active scan (interval 500, window
  100, duplicate filter off), and every advertisement in range costs an LRU entry in the
  scales library (100 addresses, `std::list` + `unordered_map` of `std::string`), a transient
  `NimBLEAdvertisedDevice` and a scan-response timer, all internal, 20-60 B each, allocated
  and freed all day. After one shot the largest free block had gone 31.7 -> 19.4 KB and stayed
  there; consistent with the 51 h incident (32 -> 16 KB). Ryan rejected duty-cycling the scan
  (a scale switched on mid-session must connect at once); the leverage left is in the
  library (an allocation-free seen-address ring, the controller-side duplicate filter).
  Raw dumps and reports from the session are in the session scratchpad
  (`trace-baseline.txt`, `trace-postshot.txt`, `trace-midshot.txt`, `trace-endshot.txt`).
