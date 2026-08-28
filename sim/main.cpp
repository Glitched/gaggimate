// Desktop simulator entry point. Runs the real display Controller on a single
// cooperative loop on the main thread (LVGL/SDL must stay on the main thread on
// macOS), driving the firmware's loop methods directly since the FreeRTOS tasks
// are no-ops in the simulator.
#include "ESPAsyncWebServer.h"
#include "SdlDriver.h"
#include <Arduino.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <display/core/Controller.h>
#include <display/plugins/ShotHistoryPlugin.h>
#include <display/ui/default/DefaultUI.h>

// The generated UI event handlers reference this global (see main.h on device).
Controller controller;

// A scripted touch: press at (x, y) `atMs` after boot, release TAP_HOLD_MS later.
// LVGL samples the pointer every LV_INDEV_DEF_READ_PERIOD (30 ms), so the hold
// has to span several samples or the press is never seen.
struct ScriptedTap {
    int x, y;
    unsigned long atMs;
    bool pressed = false, done = false;
};
static constexpr unsigned long TAP_HOLD_MS = 120;

// A scripted drag along a circle: pressed at angle a0 when t0 arrives, swept to a1 by t1, then
// released. Degrees in screen convention (0 = right, clockwise positive). For the profile dial.
struct ScriptedArc {
    int cx, cy, r;
    float a0, a1;
    unsigned long t0, t1;
    bool done = false;
};

// A scripted straight drag from (x0, y0) at t0 to (x1, y1) at t1, then released.
struct ScriptedDrag {
    int x0, y0, x1, y1;
    unsigned long t0, t1;
    bool done = false;
};

int main(int argc, char **argv) {
    // Optional flags, all usable together:
    //   --screenshot <path> [delayMs]   render for a bit, save a BMP, exit
    //   --tap X,Y@MS                    synthetic touch at (X, Y) MS after boot; repeatable
    //   --drag X0,Y0>X1,Y1@T0~T1        straight drag from (X0, Y0) to (X1, Y1) between T0 and T1 ms
    //   --arc CX,CY,R,A0,A1@T0~T1       drag along a circle of radius R about (CX, CY) from angle
    //                                   A0 to A1 (degrees, clockwise) between T0 and T1 ms
    //   --scale                         pretend a Bluetooth scale is connected (BLE scales are
    //                                   compiled out of the sim), so the weight/volumetric UI shows
    // e.g. `--tap 240,240@3000 --screenshot shot.bmp 8000` wakes the standby screen
    // and captures what comes up. Screen coordinates are the panel's (480x480).
    const char *shotPath = nullptr;
    unsigned long shotDelayMs = 4000;
    std::vector<ScriptedTap> taps;
    std::vector<ScriptedArc> arcs;
    std::vector<ScriptedDrag> drags;
    bool fakeScale = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--scale") == 0) {
            fakeScale = true;
            continue;
        }
        if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            shotPath = argv[++i];
            if (i + 1 < argc)
                shotDelayMs = strtoul(argv[i + 1], nullptr, 10);
        } else if (strcmp(argv[i], "--drag") == 0 && i + 1 < argc) {
            ScriptedDrag d{};
            if (sscanf(argv[++i], "%d,%d>%d,%d@%lu~%lu", &d.x0, &d.y0, &d.x1, &d.y1, &d.t0, &d.t1) == 6 && d.t1 > d.t0) {
                drags.push_back(d);
            } else {
                fprintf(stderr, "bad --drag '%s', expected X0,Y0>X1,Y1@T0~T1\n", argv[i]);
                return 2;
            }
        } else if (strcmp(argv[i], "--arc") == 0 && i + 1 < argc) {
            ScriptedArc arc{};
            if (sscanf(argv[++i], "%d,%d,%d,%f,%f@%lu~%lu", &arc.cx, &arc.cy, &arc.r, &arc.a0, &arc.a1, &arc.t0, &arc.t1) == 7 &&
                arc.t1 > arc.t0) {
                arcs.push_back(arc);
            } else {
                fprintf(stderr, "bad --arc '%s', expected CX,CY,R,A0,A1@T0~T1\n", argv[i]);
                return 2;
            }
        } else if (strcmp(argv[i], "--tap") == 0 && i + 1 < argc) {
            ScriptedTap tap{};
            if (sscanf(argv[++i], "%d,%d@%lu", &tap.x, &tap.y, &tap.atMs) == 3) {
                taps.push_back(tap);
            } else {
                fprintf(stderr, "bad --tap '%s', expected X,Y@MS\n", argv[i]);
                return 2;
            }
        }
    }

    // A --screenshot run is unattended: render to a hidden window and ignore the
    // real mouse so nothing but --tap can touch the screen.
    SdlDriver::getInstance()->setScripted(shotPath != nullptr);

    controller.setup(); // builds the UI, installs the SDL driver, marks screen ready
    if (fakeScale) {
        // Same switch the web UI's "volumetric override" uses: isBluetoothScaleHealthy()
        // reports true, so every scale-dependent screen element appears.
        controller.setVolumetricOverride(true);
    }

    // The sim has a real network (the WebUI is reachable), so present as Wi-Fi
    // connected: seeding credentials sends setupWifi() down the STA path, and the
    // WiFi shim's begin() reports WL_CONNECTED. This makes the standby screen show
    // the clock and Wi-Fi icon (and avoids captive-portal AP mode). Seed only once.
    Settings &settings = controller.getSettings();
    if (settings.getWifiSsid().isEmpty())
        settings.setWifiSsid("GaggiMate-Sim");
    if (settings.getWifiPassword().isEmpty())
        settings.setWifiPassword("simulator");

    SdlDriver *drv = SdlDriver::getInstance();
    DefaultUI *ui = controller.getUI();
    const unsigned long start = millis();
    bool shotTaken = false;

    while (!drv->shouldQuit()) {
        controller.loop();      // connection lifecycle, comms pump, plugins
        controller.loopLogic(); // process + control logic (normally a FreeRTOS task)

        // Shot history sampling normally runs in its own FreeRTOS task (a no-op in
        // the sim), so drive record() here at its native cadence.
        {
            static unsigned long lastShotSample = 0;
            if (millis() - lastShotSample >= SHOT_LOG_SAMPLE_INTERVAL_MS) {
                lastShotSample = millis();
                ShotHistory.record();
            }
        }

        // On the device DefaultUI::loop runs in its own task with a 25 ms sleep between
        // iterations (DefaultUI::loopTask), which caps animations at ~40 fps. Match that
        // here so a transition gets the same number of frames in the sim as on hardware.
        {
            static unsigned long lastUiLoop = 0;
            if (ui && millis() - lastUiLoop >= 25) {
                lastUiLoop = millis();
                ui->loop();
                ui->loopProfiles();
            }
        }
        gm_web_pump(); // service the embedded WebUI HTTP/WS server

        // Settings persistence normally runs in a deferred save task (a no-op in the
        // sim), so flush dirty settings to NVS periodically. save(true) is a cheap
        // no-op when nothing changed.
        {
            static unsigned long lastSave = 0;
            if (millis() - lastSave >= 2000) {
                lastSave = millis();
                controller.getSettings().save(true);
            }
        }

        for (ScriptedTap &tap : taps) {
            if (tap.done)
                continue;
            const unsigned long elapsed = millis() - start;
            if (!tap.pressed && elapsed >= tap.atMs) {
                drv->injectPointer(tap.x, tap.y, true);
                tap.pressed = true;
            } else if (tap.pressed && elapsed >= tap.atMs + TAP_HOLD_MS) {
                drv->injectPointer(tap.x, tap.y, false);
                tap.done = true;
            }
        }

        for (ScriptedArc &arc : arcs) {
            if (arc.done)
                continue;
            const unsigned long elapsed = millis() - start;
            if (elapsed < arc.t0)
                continue;
            const float f = elapsed >= arc.t1 ? 1.f : static_cast<float>(elapsed - arc.t0) / (arc.t1 - arc.t0);
            const float a = (arc.a0 + (arc.a1 - arc.a0) * f) * static_cast<float>(M_PI) / 180.f;
            const int x = arc.cx + static_cast<int>(lroundf(arc.r * cosf(a)));
            const int y = arc.cy + static_cast<int>(lroundf(arc.r * sinf(a)));
            drv->injectPointer(x, y, elapsed < arc.t1);
            if (elapsed >= arc.t1)
                arc.done = true;
        }

        for (ScriptedDrag &d : drags) {
            if (d.done)
                continue;
            const unsigned long elapsed = millis() - start;
            if (elapsed < d.t0)
                continue;
            const float f = elapsed >= d.t1 ? 1.f : static_cast<float>(elapsed - d.t0) / (d.t1 - d.t0);
            drv->injectPointer(d.x0 + static_cast<int>(lroundf((d.x1 - d.x0) * f)),
                               d.y0 + static_cast<int>(lroundf((d.y1 - d.y0) * f)), elapsed < d.t1);
            if (elapsed >= d.t1)
                d.done = true;
        }

        drv->pumpAndRender();

        if (shotPath && !shotTaken && millis() - start >= shotDelayMs) {
            drv->screenshot(shotPath);
            shotTaken = true;
            break;
        }
        delay(5);
    }
    return 0;
}
