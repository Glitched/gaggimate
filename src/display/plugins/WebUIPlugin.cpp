#include "WebUIPlugin.h"
#include <DNSServer.h>
#include <Update.h>
#include <LittleFS.h>
#include <SD_MMC.h>
#include <algorithm>
#include <display/core/Controller.h>
#include <display/core/HeapCheckpoints.h>
#include <display/core/ProfileManager.h>
#include <display/core/process/BrewProcess.h>
#include <display/core/process/GrindProcess.h>
#include <display/models/profile.h>
#include <display/plugins/BLEScalePlugin.h>
#include <display/plugins/ShotHistoryPlugin.h>
#include <display/util/PsramStlAllocator.h>
#include <display/util/PsramWsBuffer.h>
#include <display/util/mathutils.h>
#include <display/webassets/web_ui_manifest.h>

// Stands in for a credential the API declines to disclose. The settings form
// round-trips it unchanged, and the POST handler treats it as "leave alone".
static constexpr const char *PASSWORD_PLACEHOLDER = "---unchanged---";
#include <esp32-hal-psram.h>
#include <esp_core_dump.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_partition.h>
#include <mbedtls/platform.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <version.h>

static WebUIPlugin *g_webUIPlugin = nullptr;

// Serialize a JsonDocument straight into a PSRAM-backed WebSocket message
// buffer — one exact-sized allocation, off the internal heap. [GM-139]
static AsyncWebSocketSharedBuffer toWsBuffer(JsonDocument &doc) {
    const size_t len = measureJson(doc);
    auto buffer = makePsramWsBuffer(len);
    serializeJson(doc, buffer->data(), len);
    return buffer;
}

// Shared by the WebSocket and REST profile-save paths; string literal so
// ArduinoJson stores the pointer without copying.
static constexpr const char *PROFILE_VALIDATION_ERROR =
    "Invalid profile: 'label' (string), 'type' (string) and a non-empty 'phases' array are required";

// "scheme://host[:port]" against the Host header "host[:port]", case-insensitive. The
// scheme is irrelevant: the device only speaks plain HTTP.
static bool originMatchesHost(const String &origin, const String &host) {
    const int at = origin.indexOf("://");
    const String bare = at >= 0 ? origin.substring(at + 3) : origin;
    return !host.isEmpty() && bare.equalsIgnoreCase(host);
}

// Buffers a request body chunk by chunk into request->_tempObject. Ownership:
// the consuming handler takes the pointer (and frees it); if none does, the
// request destructor frees it. Oversized or inconsistent bodies are dropped,
// which the handler observes as a missing body.
static void bufferJsonBody(AsyncWebServerRequest *request, const uint8_t *data, size_t len, size_t index, size_t total,
                           size_t maxLen) {
    if (total == 0 || total > maxLen || index + len > total) {
        return;
    }
    // JSON routes read JSON only. A text/plain body is what a cross-site form or a
    // no-preflight fetch sends; leaving it unbuffered makes every JSON handler see
    // "no body" (400) instead of acting on it.
    if (index == 0 && !request->contentType().startsWith("application/json")) {
        return;
    }
    if (index == 0) {
        request->_tempObject = ps_malloc(total + 1);
    }
    auto *buf = static_cast<char *>(request->_tempObject);
    if (buf == nullptr) {
        return;
    }
    memcpy(buf + index, data, len);
    if (index + len == total) {
        buf[total] = '\0';
    }
}

// Route mbedTLS allocations to PSRAM.
static void *mbedtlsPsramCalloc(size_t n, size_t size) { // NOSONAR
    void *p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p == nullptr) {
        p = heap_caps_calloc(n, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return p;
}
static void mbedtlsPsramFree(void *p) { heap_caps_free(p); } // NOSONAR

WebUIPlugin::WebUIPlugin() : server(80), ws("/ws") { g_webUIPlugin = this; }

void WebUIPlugin::setup(Controller *_controller, PluginManager *_pluginManager) {
    // Redirect mbedTLS allocations to PSRAM before any TLS (OTA) handshake runs, so the
    // ~32 KB handshake buffers don't exhaust the scarce internal-DRAM pool. See mbedtlsPsramCalloc.
    (void)mbedtls_platform_set_calloc_free(mbedtlsPsramCalloc, mbedtlsPsramFree);
    this->controller = _controller;
    this->profileManager = _controller->getProfileManager();
    this->pluginManager = _pluginManager;
    this->ota = new GitHubOTA(
        BUILD_GIT_VERSION, controller->getSystemInfo().version,
        RELEASE_URL + (controller->getSettings().getOTAChannel() == "latest" ? "latest" : "tag/nightly"),
        [this](uint8_t phase) {
            pluginManager->trigger("ota:update:phase", "phase", phase);
            updateOTAProgress(phase, 0);
        },
        [this](uint8_t phase, int progress) {
            pluginManager->trigger("ota:update:progress", "progress", progress);
            updateOTAProgress(phase, progress);
        },
        "display-firmware.bin", "display-filesystem.bin", "board-firmware.bin");
    pluginManager->on("controller:wifi:connect", [this](Event const &event) {
        apMode = event.getInt("AP");
        start();
    });
    // Intentionally do NOT stop the server on a WiFi disconnect: the listen
    // socket survives a reconnect, and tearing it down only to rebind moments
    // later races AsyncTCP's async close (bind: -8) and churns sockets in the
    // recovery path. The server keeps listening; clients reconnect on their own.
    pluginManager->on("controller:wifi:disconnect", [this](Event const &) {
        ws.cleanupClients(); // drop dead websocket clients; keep the listener up
    });
    pluginManager->on("controller:ready", [this](Event const &) {
        ota->setControllerVersion(controller->getSystemInfo().version);
        ota->init(controller->getClientController()->getClient());
    });
    pluginManager->on("controller:autotune:result", [this](Event const &event) { sendAutotuneResult(); });
    pluginManager->on("controller:autotune:failed", [this](Event const &) { sendAutotuneFailed(); });

    // Forward shot history rebuild progress events to WebSocket clients
    pluginManager->on("evt:history-rebuild-progress", [this](Event const &event) {
        JsonDocument doc(&psramAllocator);
        doc["tp"] = "evt:history-rebuild-progress";
        doc["total"] = event.getInt("total");
        doc["current"] = event.getInt("current");
        doc["status"] = event.getString("status");
        broadcastJson(doc);
    });

    // Forward "shot saved to history" events to WebSocket clients, so the
    // dashboard can refetch the recent-shots buffer at the right time.
    pluginManager->on("evt:history-shot-saved", [this](Event const &event) {
        JsonDocument doc(&psramAllocator);
        doc["tp"] = "evt:history-shot-saved";
        doc["id"] = event.getInt("id");
        broadcastJson(doc);
    });

    // Forward live shot-finished stats (pressure/flow) to WebSocket clients, so
    // the dashboard's finished card can show them without waiting for the
    // history file write.
    pluginManager->on("evt:shot-finished-stats", [this](Event const &event) {
        JsonDocument doc(&psramAllocator);
        doc["tp"] = "evt:shot-finished-stats";
        doc["maxPressure"] = event.getFloat("maxPressure");
        doc["avgFlow"] = event.getFloat("avgFlow");
        broadcastJson(doc);
    });

    // Subscribe to Bluetooth scale weight updates
    pluginManager->on("controller:volumetric-measurement:bluetooth:change",
                      [this](Event const &event) { this->currentBluetoothWeight = event.getFloat("value"); });

    setupServer();
}

static constexpr unsigned long PANEL_STATS_PERIOD = 10000; // render-pipeline snapshot cadence, ms

void WebUIPlugin::loop() {
    if (restartPending != 0 && millis() >= restartPending) {
        ESP_LOGI("WebUIPlugin", "Rebooting (deferred restart)");
        delay(50);
        ESP.restart();
    }
    if (updating) {
        // Pass which component is being flashed: a controller update streams the
        // firmware over BLE (wants a low-latency link), a display update is over
        // Wi-Fi (wants BLE to stay out of the radio's way). "" = both.
        pluginManager->trigger("ota:update:start", "component", updateComponent);
        ota->update(updateComponent != "display", updateComponent != "controller");
        pluginManager->trigger("ota:update:end");
        updating = false;
    }
    if (!serverRunning) {
        return;
    }
    const unsigned long now = millis();
    if (now - lastPanelStats >= PANEL_STATS_PERIOD) {
        // Render-pipeline health: UI frames vs flushes vs panel refreshes, plus the heap floor. One line per 10 s.
        panelSnapshot = panelStats().snapshot(lastPanelStats == 0 ? 0 : now - lastPanelStats);
        lastPanelStats = now;
        ESP_LOGI("WebUIPlugin",
                 "render: ui %.1f fps, flush %.1f/s avg %lu us max %lu us, vsync %.1f Hz (%lu late); "
                 "heap free %u min %u largest %u",
                 panelSnapshot.uiFps, panelSnapshot.flushHz, static_cast<unsigned long>(panelSnapshot.flushAvgUs),
                 static_cast<unsigned long>(panelSnapshot.flushMaxUs), panelSnapshot.vsyncHz,
                 static_cast<unsigned long>(panelSnapshot.vsyncsLate),
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_DEFAULT | MALLOC_CAP_INTERNAL)),
                 static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT | MALLOC_CAP_INTERNAL)),
                 static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT | MALLOC_CAP_INTERNAL)));
    }
    // An upload that dies mid-stream -- client sleeps, Wi-Fi drops, browser tab
    // closes -- never reaches handleFirmwareUploadResult, so nothing clears
    // uploadInProgress and the Updater keeps the OTA slot open. Every later
    // attempt is then rejected as "another update is already running" until the
    // display is power-cycled. Reclaim it once the stream has clearly stopped.
    // uploadLastChunk is written by the receive callback on the AsyncTCP task; read millis() fresh and compare signed,
    // or a chunk landing between the `now` capture above and this check wraps to ~4.29e9 ms and aborts a healthy
    // upload ("no data for 4294967242 ms" seen on 2026-09-02).
    if (uploadInProgress && static_cast<long>(millis() - uploadLastChunk) > static_cast<long>(UPLOAD_STALL_TIMEOUT)) {
        const unsigned long stalledMs = millis() - uploadLastChunk;
        ESP_LOGW("WebUIPlugin", "Firmware upload stalled for %lums, aborting", stalledMs);
        uploadError = "no data for " + String(stalledMs) + " ms after " + String(uploadTotal) + " bytes";
        Update.abort();
        uploadInProgress = false;
        pluginManager->trigger("ota:update:end");
        pluginManager->trigger("ota:upload:failed");
    }
    // Skip the (blocking, TLS) update check while a process is active: a brew/steam/grind
    // must not have the control loop stalled for the duration of the handshake, nor compete
    // with it for memory. isActive() is the reliable "a process is running" signal. Subtraction
    // (not now > last + interval) keeps the interval check millis()-rollover-safe.
    if (!controller->isActive() && (lastUpdateCheck == 0 || now - lastUpdateCheck > UPDATE_CHECK_INTERVAL)) {
        ota->checkForUpdates();
        pluginManager->trigger("ota:update:status", "value", ota->isUpdateAvailable());
        lastUpdateCheck = now;
        updateOTAStatus();
    }
    if (now > lastStatus + STATUS_PERIOD && !ws.getClients().empty()) {
        lastStatus = now;
        statusDoc.clear();
        statusDoc["tp"] = "evt:status";
        statusDoc["ct"] = round_to(controller->getCurrentTemp(), 3);
        statusDoc["tt"] = controller->getTargetTemp();
        statusDoc["pr"] = round_to(controller->getCurrentPressure(), 3);
        statusDoc["fl"] = round_to(controller->getCurrentPumpFlow(), 3);
        statusDoc["pt"] = controller->getTargetPressure();
        statusDoc["m"] = controller->getMode();
        statusDoc["p"] = controller->getProfileManager()->getSelectedProfile().label;
        statusDoc["puid"] = controller->getProfileManager()->getSelectedProfile().id;
        statusDoc["cp"] = controller->getSystemInfo().capabilities.pressure;
        statusDoc["cd"] = controller->getSystemInfo().capabilities.dimming;
        statusDoc["gp"] = controller->getSystemInfo().capabilities.hasAddon(7);
        statusDoc["tw"] = profileManager->getSelectedProfile().getTotalVolume(); // total target weight for the process
        statusDoc["bta"] = controller->isVolumetricAvailable() ? 1 : 0;
        statusDoc["bt"] =
            controller->isVolumetricAvailable() && controller->getProfileManager()->getSelectedProfile().isVolumetric() ? 1 : 0;
        statusDoc["btd"] = profileManager->getSelectedProfile().getTotalDuration();
        statusDoc["led"] = controller->getSystemInfo().capabilities.ledControl;
        statusDoc["gtd"] = controller->getTargetGrindDuration();
        statusDoc["gtv"] = controller->getSettings().getTargetGrindVolume();
        statusDoc["gt"] = controller->isVolumetricAvailable() && controller->getSettings().isVolumetricTarget() ? 1 : 0;
        statusDoc["gact"] = controller->isGrindActive() ? 1 : 0;
        statusDoc["wl"] = controller->getWaterLevel();
        statusDoc["tof"] = controller->getTofDistance();
        statusDoc["rssi"] = 0;
        statusDoc["lat"] = -1; // BLE round-trip latency (ms); -1 = not yet measured
        statusDoc["pw"] = controller->getCurrentPumpPower();
        statusDoc["hp"] = round_to(controller->getCurrentHeaterPower(), 3);
        statusDoc["up"] = ota->isUpdateAvailable() || ota->isUpdateAvailable(true);

        if (controller->getClientController()->getClient()->isConnected()) {
            statusDoc["rssi"] = controller->getClientController()->getClient()->getRssi();
        }
        if (controller->getClientController()->hasLatency()) {
            statusDoc["lat"] = controller->getClientController()->getLatencyMs();
        }

        bool bleConnected = BLEScales.isConnected();
        // Add Bluetooth scale weight information
        statusDoc["bw"] = bleConnected ? this->currentBluetoothWeight : 0; // current bluetooth weight
        statusDoc["cw"] = bleConnected ? this->currentBluetoothWeight : 0; // Use 'currentWeight' for forward compatbility
        statusDoc["bc"] = bleConnected;                                    // bluetooth scale connected status
        // Scale battery — only surfaced when the driver reports one and the
        // value isn't the UNKNOWN sentinel (255). UI omits the battery pill
        // entirely when `sbat` is absent, so disconnected/unknown scales don't
        // render a stale stub.
        if (bleConnected && BLEScales.hasBatteryLevel()) {
            const uint8_t pct = BLEScales.getBatteryLevel();
            if (pct != REMOTE_SCALES_BATTERY_UNKNOWN) {
                statusDoc["sbat"] = pct;
            }
        }

        // Deref under the process lock — other tasks delete the process at any time (GM-147).
        // Released before broadcastJson so the ws send never runs under the lock.
        std::unique_lock<std::recursive_mutex> processGuard(controller->getProcessLock());
        Process *process = controller->getProcess();
        if (process == nullptr) {
            process = controller->getLastProcess();
        }
        if (process != nullptr) {
            auto pObj = statusDoc["process"].to<JsonObject>();
            pObj["a"] = controller->isActive() ? 1 : 0;
            statusDoc["pkr"] = round_to(controller->getCurrentPuckResistance(), 3);
            statusDoc["pf"] = round_to(controller->getCurrentPuckFlow(), 3);
            statusDoc["tf"] = controller->getTargetFlow();
            if (process->getType() == MODE_BREW) {
                auto *brew = static_cast<BrewProcess *>(process);
                unsigned long ts = brew->isActive() && controller->isActive() ? millis() : brew->finished;
                pObj["s"] = brew->currentPhase.phase == PhaseType::PHASE_TYPE_BREW ? "brew" : "infusion";
                pObj["l"] = brew->isActive() ? brew->currentPhase.name.c_str() : "Finished";
                pObj["e"] = ts - brew->processStarted;
                const bool isVolumetric = brew->target == ProcessTarget::VOLUMETRIC && brew->currentPhase.hasVolumetricTarget() &&
                                          controller->isVolumetricAvailable();
                pObj["tt"] = isVolumetric ? "volumetric" : "time";
                if (isVolumetric) {
                    Target t = brew->currentPhase.getVolumetricTarget();
                    pObj["pt"] = t.value;
                    pObj["pp"] = brew->currentVolume;
                } else {
                    pObj["pt"] = brew->getPhaseDuration();
                    pObj["pp"] = ts - brew->currentPhaseStarted;
                }
            } else if (process->getType() == MODE_GRIND) {
                auto *grind = static_cast<GrindProcess *>(process);
                unsigned long ts = grind->isActive() && controller->isActive() ? millis() : grind->finished;
                pObj["s"] = "grind";
                pObj["l"] = grind->isActive() ? "Grinding" : "Finished";
                pObj["e"] = ts - grind->started;
                const bool isVolumetric = grind->target == ProcessTarget::VOLUMETRIC && controller->isVolumetricAvailable();
                pObj["tt"] = isVolumetric ? "volumetric" : "time";
                if (isVolumetric) {
                    pObj["pt"] = grind->grindVolume;
                    pObj["pp"] = grind->currentVolume;
                } else {
                    pObj["pt"] = grind->time;
                    pObj["pp"] = ts - grind->started;
                }
            }
        }
        processGuard.unlock();

        broadcastJson(statusDoc);
    }
    if (now > lastCleanup + CLEANUP_PERIOD) {
        lastCleanup = now;
        ws.cleanupClients();
    }
    if (now > lastDns + DNS_PERIOD && dnsServer != nullptr) {
        lastDns = now;
        dnsServer->processNextRequest();
    }
}

// The bundle is pulled into flash by web_ui_blob.S via .incbin, a dependency the
// build system cannot see (scripts/check_webui_blob.py is the build-time guard).
// If a stale object from a stub build ever slips through, the linked blob is a
// single byte while the manifest still describes the full bundle — serving from
// it would hand out whatever rodata follows the symbol. Refuse instead. [GM-106]
static bool webBlobIsIntact() {
    return static_cast<size_t>(gWebUiBlobEnd - gWebUiBlobStart) >= WEB_UI_BLOB_SIZE;
}

// Linear lookup over the embedded asset table (~60 entries) — a couple of
// strcmps per request, negligible next to the network round-trip.
static const WebAsset *findWebAsset(const String &path) {
    for (size_t i = 0; i < WEB_ASSETS_COUNT; i++) {
        if (path == WEB_ASSETS[i].path) {
            return &WEB_ASSETS[i];
        }
    }
    return nullptr;
}

void WebUIPlugin::serveWebAsset(AsyncWebServerRequest *request) {
    if (!webBlobIsIntact()) {
        request->send(503, "text/plain", "Web UI bundle missing from this firmware image.");
        return;
    }

    String path = request->url();
    if (path.isEmpty() || path == "/") {
        path = WEB_UI_INDEX_PATH;
    }

    const WebAsset *asset = findWebAsset(path);
    if (asset == nullptr && !path.startsWith("/assets/")) {
        // SPA client-side routes (e.g. /settings, /profiles) aren't real files —
        // fall back to index.html. A miss under /assets/ is a genuine 404, not a
        // route, so it is not rewritten.
        asset = findWebAsset(WEB_UI_INDEX_PATH);
    }
    if (asset == nullptr) {
        request->send(404, "text/plain", "Not found");
        return;
    }

    // Serve straight from the memory-mapped flash blob — no copy into RAM, no
    // filesystem read. AsyncProgmemResponse streams from the pointer in chunks.
    AsyncWebServerResponse *response =
        request->beginResponse(200, asset->contentType, gWebUiBlobStart + asset->offset, asset->length);
    if (asset->gzip) {
        response->addHeader("Content-Encoding", "gzip");
    }
    // Content-hashed build assets (/assets/<hash>.js) never change for a given URL — cache them forever. index.html and
    // other unhashed files must revalidate so a new build is picked up after an update. [GM-83]
    if (path.startsWith("/assets/")) {
        response->addHeader("Cache-Control", "public, max-age=31536000, immutable");
    } else {
        response->addHeader("Cache-Control", "no-cache");
    }
    request->send(response);
}

void WebUIPlugin::setupServer() {
    // Cross-site writes. A browser adds an Origin header to every cross-origin
    // request and to same-origin POST/PUT/DELETE, so a write whose Origin is not
    // this device's own host came from some other page and is rejected. GETs stay
    // open (read-only), and requests with no Origin -- curl, scripts, the Vite dev
    // proxy (which strips it) -- pass. One check, ahead of every route.
    server.addMiddleware([](AsyncWebServerRequest *request, ArMiddlewareNext next) {
        const int method = request->method();
        if (method == HTTP_GET || method == HTTP_HEAD || method == HTTP_OPTIONS || !request->hasHeader("Origin")) {
            next();
            return;
        }
        const String origin = request->header("Origin");
        if (originMatchesHost(origin, request->host())) {
            next();
            return;
        }
        ESP_LOGW("WebUIPlugin", "Rejected cross-origin write to %s from %s", request->url().c_str(), origin.c_str());
        request->send(403, "application/json", "{\"error\":\"Cross-origin request rejected\"}");
    });

    server.on("/connecttest.txt", [](AsyncWebServerRequest *request) {
        request->redirect("http://logout.net");
    }); // windows 11 captive portal workaround
    server.on("/wpad.dat", [](AsyncWebServerRequest *request) {
        request->send(404);
    }); // Honestly don't understand what this is but a 404 stops win 10 keep calling this repeatedly and panicking the esp32
        // :)
    server.on("/generate_204",
              [](AsyncWebServerRequest *request) { request->redirect(LOCAL_URL); }); // android captive portal redirect
    server.on("/redirect", [](AsyncWebServerRequest *request) { request->redirect(LOCAL_URL); });            // microsoft redirect
    server.on("/hotspot-detect.html", [](AsyncWebServerRequest *request) { request->redirect(LOCAL_URL); }); // apple call home
    server.on("/canonical.html",
              [](AsyncWebServerRequest *request) { request->redirect(LOCAL_URL); });       // firefox captive portal call home
    server.on("/success.txt", [](AsyncWebServerRequest *request) { request->send(200); }); // firefox captive portal call home
    server.on("/ncsi.txt", [](AsyncWebServerRequest *request) { request->redirect(LOCAL_URL); }); // windows call home
    // Settings accepts a JSON body (preferred, partial update) or legacy form
    // args. The JSON body streams in through the body callback and is buffered
    // into _tempObject, which the request frees if we never consume it.
    server.on(
        "/api/settings", HTTP_ANY, [this](AsyncWebServerRequest *request) { handleSettings(request); }, nullptr,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            bufferJsonBody(request, data, len, index, total, 16 * 1024);
        });
    // REST profile API, used by the web UI and by external tools (scripts, agents)
    // alike. The plain-string route matches /api/profiles and all subpaths.
    server.on(
        "/api/profiles", HTTP_ANY, [this](AsyncWebServerRequest *request) { handleProfilesRest(request); }, nullptr,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            // Same cap as the WebSocket profile upload buffer.
            bufferJsonBody(request, data, len, index, total, 64 * 1024);
        });
    server.on("/api/status", [this](AsyncWebServerRequest *request) {
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        JsonDocument doc(&psramAllocator);
        doc["mode"] = controller->getMode();
        doc["tt"] = controller->getTargetTemp();
        doc["ct"] = controller->getCurrentTemp();
        serializeJson(doc, *response);
        request->send(response);
    });
    server.on("/api/scales/list", [this](AsyncWebServerRequest *request) { handleBLEScaleList(request); });
    server.on("/api/scales/connect", [this](AsyncWebServerRequest *request) { handleBLEScaleConnect(request); });
    server.on("/api/scales/scan", [this](AsyncWebServerRequest *request) { handleBLEScaleScan(request); });
    server.on("/api/scales/info", [this](AsyncWebServerRequest *request) { handleBLEScaleInfo(request); });
    FS *fs = &LittleFS;
    if (controller->isSDCard()) {
        fs = &SD_MMC;
    }
    server.on("/api/history/index.bin", HTTP_GET, [this, fs](AsyncWebServerRequest *request) {
        // Serve the binary index file directly
        if (fs->exists("/h/index.bin")) {
            request->send(*fs, "/h/index.bin", "application/octet-stream");
        } else {
            request->send(404, "text/plain", "Index not found");
        }
    });
    server.on("/api/history/recent.bin", HTTP_GET, [this](AsyncWebServerRequest *request) {
        // The most recent non-deleted shots, newest first, as a regular shot
        // index (SIDX header + entries) — same binary format as index.bin,
        // just truncated, so clients reuse the index.bin parser.
        constexpr long MAX_RECENT_LIMIT = 50;
        long limit = 8;
        if (request->hasArg("limit")) {
            limit = constrain(request->arg("limit").toInt(), 1L, MAX_RECENT_LIMIT);
        }

        auto *entries = static_cast<ShotIndexEntry *>(ps_malloc(limit * sizeof(ShotIndexEntry)));
        if (entries == nullptr) {
            request->send(500, "text/plain", "Out of memory");
            return;
        }
        size_t count = ShotHistory.readRecentEntries(entries, limit);

        ShotIndexHeader header{};
        header.magic = SHOT_INDEX_MAGIC;
        header.version = SHOT_INDEX_VERSION;
        header.entrySize = SHOT_INDEX_ENTRY_SIZE;
        header.entryCount = count;
        header.nextId = 0; // meaningless for a partial view

        AsyncResponseStream *response = request->beginResponseStream("application/octet-stream");
        response->addHeader("Cache-Control", "no-store");
        response->write(reinterpret_cast<const uint8_t *>(&header), sizeof(header));
        response->write(reinterpret_cast<const uint8_t *>(entries), count * sizeof(ShotIndexEntry));
        free(entries);
        request->send(response);
    });
    // After the explicit index.bin / recent.bin routes: the static handler probes for a
    // "<file>.gz" first, so registered ahead of them it cost every history load a flash stat
    // for /h/index.bin.gz and an error-level log line.
    server.serveStatic("/api/history/", *fs, "/h/").setCacheControl("no-store");
    server.on("/api/core-dump", HTTP_GET, [this](AsyncWebServerRequest *request) { handleCoreDumpDownload(request); });
    server.on("/api/debug/heap", HTTP_GET, [this](AsyncWebServerRequest *request) { handleHeapDebug(request); });
#ifndef GAGGIMATE_SIM
    // Direct firmware upload. The body handler streams straight into the
    // inactive OTA partition; the request handler runs once the body is done.
    server.on(
        "/api/ota/upload", HTTP_POST, [this](AsyncWebServerRequest *request) { handleFirmwareUploadResult(request); }, nullptr,
        // Raw body (Content-Type: application/octet-stream), not multipart: the
        // server's form-data parser walks the body one byte at a time and topped
        // out at ~26 KB/s, three minutes for an image. The body callback hands
        // over whole TCP segments, and Content-Length sizes the Updater up front.
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            handleFirmwareUpload(request, index, data, len, total);
        });
#endif
    // Command routes (mode, process, targets, OTA, history), so scripts and agents
    // can drive the machine with curl (docs/http-api.yaml). Plain-string routes match
    // the path and its subpaths; JSON bodies stream into _tempObject as for
    // /api/settings. Registered after /api/ota/upload: "/api/ota" is a prefix
    // route and would otherwise claim it. /api/history is registered per method so GETs never reach
    // it: the sim's server shim tries routes before static files, and a catch-all
    // here would shadow serveStatic("/api/history/") there.
    const auto jsonBody = [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        bufferJsonBody(request, data, len, index, total, 16 * 1024);
    };
    for (const char *route : {"/api/mode", "/api/process", "/api/grind", "/api/flush", "/api/autotune", "/api/targets"}) {
        server.on(
            route, HTTP_ANY, [this](AsyncWebServerRequest *request) { handleMachineRest(request); }, nullptr, jsonBody);
    }
    server.on(
        "/api/ota", HTTP_ANY, [this](AsyncWebServerRequest *request) { handleOtaRest(request); }, nullptr, jsonBody);
    server.on("/api/history/rebuild", HTTP_POST, [this](AsyncWebServerRequest *request) { handleHistoryRest(request); });
    server.on("/api/history", HTTP_DELETE, [this](AsyncWebServerRequest *request) { handleHistoryRest(request); });
    server.on(
        "/api/history", HTTP_PUT, [this](AsyncWebServerRequest *request) { handleHistoryRest(request); }, nullptr, jsonBody);
    // The web UI is embedded in firmware flash and served from the memory-mapped blob (see serveWebAsset). It is no
    // longer in LittleFS, so OTA never touches the partition holding profiles/shots. The catch-all onNotFound handles
    // every path not claimed by an explicit server.on()/api route above. [GM-106]
    server.onNotFound([this](AsyncWebServerRequest *request) { serveWebAsset(request); });
    ws.onEvent(
        [this](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
            (void)arg;
            (void)data;
            (void)len;
            if (type == WS_EVT_CONNECT) {
                // Close (and let the browser reconnect) a client whose send
                // queue backs up, instead of keeping it open. With it kept open
                // (false), a client that stalls under load — e.g. while the UI
                // is fetching many shot files for statistics — never has its
                // queued frames / AsyncTCP buffers reclaimed, so they accumulate
                // in internal DRAM until the whole IP stack starves (web + ICMP
                // die, no recovery). Reclaiming via close is the safer failure
                // mode. (Was the v1.8.1 behaviour.)
                client->setCloseClientOnQueueFull(true);
                ESP_LOGI("WebUIPlugin", "WebSocket client connected (%u open connections)",
                         static_cast<unsigned>(server->getClients().size()));
            } else if (type == WS_EVT_DISCONNECT) {
                ESP_LOGI("WebUIPlugin", "WebSocket client disconnected (%u open connections)",
                         static_cast<unsigned>(server->getClients().size()));
            }
            // The socket is push-only: every command and query is an HTTP route
            // (docs/http-api.yaml). Inbound frames are ignored.
        });
    server.addHandler(&ws);
}

void WebUIPlugin::start() {
    if (serverRunning) {
        // Already listening. The 0.0.0.0:80 listen socket survives a WiFi
        // reconnect, so re-running end()+begin() only races AsyncTCP's async
        // socket close and fails to rebind ("bind: -8, port in use"). A transient
        // STA reconnect needs nothing done here.
        return;
    }
    server.begin();
    ESP_LOGI("WebUIPlugin", "Started webserver");
    if (apMode) {
        dnsServer = new DNSServer();
        dnsServer->setTTL(3600);
        dnsServer->start(53, "*", WIFI_AP_IP);
        ESP_LOGI("WebUIPlugin", "Started catchall DNS for captive portal");
    }
    lastUpdateCheck = millis();
    serverRunning = true;
}

void WebUIPlugin::stop() {
    if (!serverRunning)
        return;
    ws.closeAll();
    server.end();
    if (dnsServer != nullptr) {
        dnsServer->stop();
        delete dnsServer;
        dnsServer = nullptr;
    }
    serverRunning = false;
    ESP_LOGI("WebUIPlugin", "WebUIPlugin stopped (wifi disconnected)");
}

namespace {
// Uniform view over the two /api/settings POST encodings: a JSON object body
// (preferred) or legacy form args. Only keys present in the request are
// applied, so a POST is a partial update in both encodings — omitting a key
// never resets it. Note this changes the historical form-checkbox contract:
// booleans were presence-based (an omitted checkbox cleared the flag), which
// forced clients to resend the whole settings payload on every save. Boolean
// form args now need an explicit value; anything except "0"/"false" is true.
struct SettingsPatch {
    AsyncWebServerRequest *request;
    JsonDocument *json;

    bool has(const char *key) const { return json != nullptr ? !(*json)[key].isNull() : request->hasArg(key); }
    String str(const char *key) const { return json != nullptr ? (*json)[key].as<String>() : request->arg(key); }
    int asInt(const char *key) const { return json != nullptr ? (*json)[key].as<int>() : request->arg(key).toInt(); }
    float asFloat(const char *key) const { return json != nullptr ? (*json)[key].as<float>() : request->arg(key).toFloat(); }
    double asDouble(const char *key) const { return json != nullptr ? (*json)[key].as<double>() : request->arg(key).toDouble(); }
    bool asBool(const char *key) const {
        if (json != nullptr) {
            return (*json)[key].as<bool>();
        }
        const String v = request->arg(key);
        return !(v == "0" || v == "false");
    }
};
} // namespace

void WebUIPlugin::handleSettings(AsyncWebServerRequest *request) {
    bool restartRequested = false;
    if (request->method() == HTTP_POST) {
        JsonDocument json(&psramAllocator);
        // Take ownership of the buffered body unconditionally so it can't
        // outlive this handler whatever the content type was.
        auto *body = static_cast<char *>(request->_tempObject);
        request->_tempObject = nullptr;
        const bool isJson = request->contentType().startsWith("application/json");
        if (isJson) {
            // const char* input makes ArduinoJson copy the strings instead of
            // zero-copy linking into the buffer we free right after.
            const DeserializationError err = deserializeJson(json, static_cast<const char *>(body != nullptr ? body : ""));
            free(body);
            body = nullptr;
            if (err != DeserializationError::Ok || !json.is<JsonObjectConst>()) {
                request->send(400, "application/json", R"({"error":"invalid JSON body"})");
                return;
            }
        } else {
            free(body);
            body = nullptr;
        }
        const SettingsPatch patch{request, isJson ? &json : nullptr};
        restartRequested = patch.has("restart") && patch.asBool("restart");
        controller->getSettings().batchUpdate([&patch](Settings *settings) {
            if (patch.has("startupMode"))
                settings->setStartupMode(patch.str("startupMode") == "brew" ? MODE_BREW : MODE_STANDBY);
            if (patch.has("startupProfile"))
                settings->setStartupProfile(patch.str("startupProfile"));
            if (patch.has("targetSteamTemp"))
                settings->setTargetSteamTemp(patch.asInt("targetSteamTemp"));
            if (patch.has("targetWaterTemp"))
                settings->setTargetWaterTemp(patch.asInt("targetWaterTemp"));
            if (patch.has("temperatureOffset"))
                settings->setTemperatureOffset(patch.asInt("temperatureOffset"));
            if (patch.has("pressureScaling"))
                settings->setPressureScaling(patch.asFloat("pressureScaling"));
            if (patch.has("pid"))
                settings->setPid(patch.str("pid"));
            if (patch.has("pumpModelCoeffs"))
                settings->setPumpModelCoeffs(patch.str("pumpModelCoeffs"));
            if (patch.has("pumpSlipCoeffs"))
                settings->setPumpSlipCoeffs(patch.str("pumpSlipCoeffs"));
            if (patch.has("wifiSsid"))
                settings->setWifiSsid(patch.str("wifiSsid"));
            if (patch.has("mdnsName"))
                settings->setMdnsName(patch.str("mdnsName"));
            if (patch.has("otaUploadToken"))
                settings->setOtaUploadToken(patch.str("otaUploadToken"));
            if (patch.has("wifiPassword") && patch.str("wifiPassword") != PASSWORD_PLACEHOLDER)
                settings->setWifiPassword(patch.str("wifiPassword"));
            // The placeholder check has to come first: it is longer than the
            // minimum length, so without it saving an unedited form would store
            // "---unchanged---" as the access point password and lock you out of
            // the hotspot.
            if (patch.has("apPassword") && patch.str("apPassword") != PASSWORD_PLACEHOLDER &&
                patch.str("apPassword").length() >= WIFI_AP_PASSWORD_MIN_LENGTH)
                settings->setWifiApPassword(patch.str("apPassword"));
            if (patch.has("homekit"))
                settings->setHomekit(patch.asBool("homekit"));
            if (patch.has("boilerFillActive"))
                settings->setBoilerFillActive(patch.asBool("boilerFillActive"));
            if (patch.has("startupFillTime"))
                settings->setStartupFillTime(patch.asInt("startupFillTime") * 1000);
            if (patch.has("steamFillTime"))
                settings->setSteamFillTime(patch.asInt("steamFillTime") * 1000);
            if (patch.has("smartGrindActive"))
                settings->setSmartGrindActive(patch.asBool("smartGrindActive"));
            if (patch.has("smartGrindIp"))
                settings->setSmartGrindIp(patch.str("smartGrindIp"));
            if (patch.has("smartGrindMode"))
                settings->setSmartGrindMode(patch.asInt("smartGrindMode"));
            if (patch.has("homeAssistant"))
                settings->setHomeAssistant(patch.asBool("homeAssistant"));
            if (patch.has("haUser"))
                settings->setHomeAssistantUser(patch.str("haUser"));
            if (patch.has("haPassword"))
                settings->setHomeAssistantPassword(patch.str("haPassword"));
            if (patch.has("haIP"))
                settings->setHomeAssistantIP(patch.str("haIP"));
            if (patch.has("haPort"))
                settings->setHomeAssistantPort(patch.asInt("haPort"));
            if (patch.has("haTopic"))
                settings->setHomeAssistantTopic(patch.str("haTopic"));
            if (patch.has("momentaryButtons"))
                settings->setMomentaryButtons(patch.asBool("momentaryButtons"));
            if (patch.has("delayAdjust"))
                settings->setDelayAdjust(patch.asBool("delayAdjust"));
            if (patch.has("brewDelay"))
                settings->setBrewDelay(patch.asDouble("brewDelay"));
            if (patch.has("grindDelay"))
                settings->setGrindDelay(patch.asDouble("grindDelay"));
            if (patch.has("timezone"))
                settings->setTimezone(patch.str("timezone"));
            if (patch.has("clock24hFormat"))
                settings->setClockFormat(patch.asBool("clock24hFormat"));
            if (patch.has("standbyTimeout"))
                settings->setStandbyTimeout(patch.asInt("standbyTimeout") * 1000);
            if (patch.has("mainBrightness"))
                settings->setMainBrightness(patch.asInt("mainBrightness"));
            if (patch.has("standbyBrightness"))
                settings->setStandbyBrightness(patch.asInt("standbyBrightness"));
            if (patch.has("standbyBrightnessTimeout"))
                settings->setStandbyBrightnessTimeout(patch.asInt("standbyBrightnessTimeout") * 1000);
            if (patch.has("steamPumpPercentage"))
                settings->setSteamPumpPercentage(patch.asFloat("steamPumpPercentage"));
            if (patch.has("steamPumpCutoff"))
                settings->setSteamPumpCutoff(patch.asFloat("steamPumpCutoff"));
            if (patch.has("themeMode"))
                settings->setThemeMode(patch.asInt("themeMode"));
            if (patch.has("sunriseIdle"))
                settings->setSunriseIdle(patch.str("sunriseIdle"));
            if (patch.has("sunriseActive"))
                settings->setSunriseActive(patch.str("sunriseActive"));
            if (patch.has("sunriseFinished"))
                settings->setSunriseFinished(patch.str("sunriseFinished"));
            if (patch.has("sunriseError"))
                settings->setSunriseError(patch.str("sunriseError"));
            if (patch.has("sunriseExtBrightness"))
                settings->setSunriseExtBrightness(patch.asInt("sunriseExtBrightness"));
            if (patch.has("emptyTankDistance"))
                settings->setEmptyTankDistance(patch.asInt("emptyTankDistance"));
            if (patch.has("fullTankDistance"))
                settings->setFullTankDistance(patch.asInt("fullTankDistance"));
            if (patch.has("altRelayFunction"))
                settings->setAltRelayFunction(patch.asInt("altRelayFunction"));
            if (patch.has("buttonBehavior"))
                settings->setButtonBehaviorList(explode(patch.str("buttonBehavior"), ','));
            if (patch.has("commutationGain"))
                settings->setCommutationGain(patch.asFloat("commutationGain"));
            if (patch.has("convergenceGain"))
                settings->setConvergenceGain(patch.asFloat("convergenceGain"));
            if (patch.has("integralGain"))
                settings->setIntegralGain(patch.asFloat("integralGain"));
            if (patch.has("maxPumpPower"))
                settings->setMaxPumpPower(patch.asFloat("maxPumpPower"));
            if (patch.has("savedScale"))
                settings->setSavedScale(patch.str("savedScale"));
            if (patch.has("autowakeupEnabled"))
                settings->setAutoWakeupEnabled(patch.asBool("autowakeupEnabled"));
            if (patch.has("autowakeupSchedules")) {
                // Handle schedule format with days
                String schedulesStr = patch.str("autowakeupSchedules");
                std::vector<AutoWakeupSchedule> schedules;

                if (schedulesStr.length() > 0) {
                    // Split semicolon-separated schedules
                    int start = 0;
                    int end = schedulesStr.indexOf(';');

                    while (end != -1 || start < schedulesStr.length()) {
                        String scheduleStr = (end != -1) ? schedulesStr.substring(start, end) : schedulesStr.substring(start);

                        int pipePos = scheduleStr.indexOf('|');
                        if (pipePos != -1) {
                            String timeStr = scheduleStr.substring(0, pipePos);
                            String daysStr = scheduleStr.substring(pipePos + 1);

                            AutoWakeupSchedule schedule;
                            schedule.time = timeStr;

                            if (daysStr.length() == 7) {
                                for (int i = 0; i < 7; i++) {
                                    schedule.days[i] = (daysStr.charAt(i) == '1');
                                }
                            }

                            schedules.push_back(schedule);
                        }

                        if (end == -1)
                            break;
                        start = end + 1;
                        end = schedulesStr.indexOf(';', start);
                    }
                }

                if (schedules.empty()) {
                    schedules.push_back(AutoWakeupSchedule("07:00")); // Default fallback
                }
                settings->setAutoWakeupSchedules(schedules);
            }
            settings->save(true);
        });
        pluginManager->trigger("settings:changed");
        controller->setTargetTemp(controller->getTargetTemp());
        controller->setPumpModelCoeffs();
    }

    AsyncResponseStream *response = request->beginResponseStream("application/json");
    JsonDocument doc(&psramAllocator);
    Settings const &settings = controller->getSettings();
    doc["startupMode"] = settings.getStartupMode() == MODE_BREW ? "brew" : "standby";
    doc["startupProfile"] = settings.getStartupProfile();
    doc["targetSteamTemp"] = settings.getTargetSteamTemp();
    doc["targetWaterTemp"] = settings.getTargetWaterTemp();
    doc["homekit"] = settings.isHomekit();
    doc["homeAssistant"] = settings.isHomeAssistant();
    doc["haUser"] = settings.getHomeAssistantUser();
    doc["haPassword"] = settings.getHomeAssistantPassword();
    doc["haIP"] = settings.getHomeAssistantIP();
    doc["haPort"] = settings.getHomeAssistantPort();
    doc["haTopic"] = settings.getHomeAssistantTopic();
    doc["pid"] = settings.getPid();
    doc["pumpModelCoeffs"] = settings.getPumpModelCoeffs();
    doc["pumpSlipCoeffs"] = settings.getPumpSlipCoeffs();
    doc["wifiSsid"] = settings.getWifiSsid();
    // Credentials are echoed back only over the device's own access point, where
    // the caller had to know the AP password to reach us at all. On a home
    // network this endpoint is unauthenticated and reachable by anything on the
    // LAN, so both are masked.
    //
    // The wifiPassword condition used to be inverted -- masked on the AP, sent
    // in clear text over the shared network -- and apPassword was never masked.
    // The AP password is still readable from the device's own screen, which is
    // the appropriate channel for it.
    const bool echoCredentials = apMode;
    doc["wifiPassword"] = echoCredentials ? settings.getWifiPassword() : PASSWORD_PLACEHOLDER;
    doc["apPassword"] = echoCredentials ? settings.getWifiApPassword() : PASSWORD_PLACEHOLDER;
    doc["mdnsName"] = settings.getMdnsName();
    // Report only whether upload is armed; never echo the secret back.
    doc["otaUploadEnabled"] = settings.getOtaUploadToken().length() > 0;
    doc["temperatureOffset"] = String(settings.getTemperatureOffset());
    doc["pressureScaling"] = String(settings.getPressureScaling());
    doc["boilerFillActive"] = settings.isBoilerFillActive();
    doc["startupFillTime"] = settings.getStartupFillTime() / 1000;
    doc["steamFillTime"] = settings.getSteamFillTime() / 1000;
    doc["smartGrindActive"] = settings.isSmartGrindActive();
    doc["smartGrindIp"] = settings.getSmartGrindIp();
    doc["smartGrindMode"] = settings.getSmartGrindMode();
    doc["momentaryButtons"] = settings.isMomentaryButtons();
    doc["brewDelay"] = settings.getBrewDelay();
    doc["grindDelay"] = settings.getGrindDelay();
    doc["delayAdjust"] = settings.isDelayAdjust();
    doc["timezone"] = settings.getTimezone();
    doc["clock24hFormat"] = settings.isClock24hFormat();
    doc["standbyTimeout"] = settings.getStandbyTimeout() / 1000;
    doc["mainBrightness"] = settings.getMainBrightness();
    doc["standbyBrightness"] = settings.getStandbyBrightness();
    doc["standbyBrightnessTimeout"] = settings.getStandbyBrightnessTimeout() / 1000;
    doc["steamPumpPercentage"] = settings.getSteamPumpPercentage();
    doc["steamPumpCutoff"] = settings.getSteamPumpCutoff();
    doc["themeMode"] = settings.getThemeMode();
    doc["sunriseIdle"] = settings.getSunriseIdle();
    doc["sunriseActive"] = settings.getSunriseActive();
    doc["sunriseFinished"] = settings.getSunriseFinished();
    doc["sunriseError"] = settings.getSunriseError();
    doc["sunriseExtBrightness"] = settings.getSunriseExtBrightness();
    doc["emptyTankDistance"] = settings.getEmptyTankDistance();
    doc["fullTankDistance"] = settings.getFullTankDistance();
    doc["altRelayFunction"] = settings.getAltRelayFunction();
    // Add auto-wakeup settings to response
    doc["autowakeupEnabled"] = settings.isAutoWakeupEnabled();
    doc["buttonBehavior"] = implode(settings.getButtonBehaviorList(), ",");
    doc["commutationGain"] = settings.getCommutationGain();
    doc["convergenceGain"] = settings.getConvergenceGain();
    doc["integralGain"] = settings.getIntegralGain();
    doc["maxPumpPower"] = settings.getMaxPumpPower();
    doc["savedScale"] = settings.getSavedScale();

    // Add schedule format with days
    std::vector<AutoWakeupSchedule> autowakeupSchedules = settings.getAutoWakeupSchedules();
    String schedulesStr = "";
    for (size_t i = 0; i < autowakeupSchedules.size(); i++) {
        if (i > 0)
            schedulesStr += ";";
        schedulesStr += autowakeupSchedules[i].time + "|";

        // Convert days array to 7-bit string
        for (int j = 0; j < 7; j++) {
            schedulesStr += autowakeupSchedules[i].days[j] ? "1" : "0";
        }
    }
    doc["autowakeupSchedules"] = schedulesStr;
    serializeJson(doc, *response);
    request->send(response);

    if (restartRequested) {
        restartPending = millis() + 1000; // loop() reboots once the response has left the socket
    }
}

// REST profile API: JSON in/out, full-document writes with save-echo.
//   GET    /api/profiles            list (?minimal=1: id, label, favorite, selected)
//   POST   /api/profiles            create (409 if the body's id already exists)
//   GET    /api/profiles/{id}       load
//   PUT    /api/profiles/{id}       replace (the path id wins over the body's)
//   DELETE /api/profiles/{id}       delete
//   POST   /api/profiles/{id}/select | /favorite | /unfavorite
// Invalid profile documents are refused with 422 — never partially stored.
std::shared_ptr<const WebUIPlugin::PsramString> WebUIPlugin::profileListJson(bool minimal) {
    const uint32_t revision = profileManager->getRevision();
    if (profileListCache.revision != revision) {
        profileListCache.full.reset();
        profileListCache.minimal.reset();
        profileListCache.revision = revision;
    }
    auto &slot = minimal ? profileListCache.minimal : profileListCache.full;
    if (!slot) {
        JsonDocument doc(&psramAllocator);
        auto arr = doc["profiles"].to<JsonArray>();
        for (auto const &profileId : profileManager->listProfiles()) {
            Profile profile{};
            if (!profileManager->loadProfile(profileId, profile)) {
                continue; // skip unreadable entries
            }
            auto p = arr.add<JsonObject>();
            if (minimal) { // enough for pickers and the favourites card without the phases
                p["id"] = profile.id;
                p["label"] = profile.label;
                p["favorite"] = profile.favorite;
                p["selected"] = profile.selected;
            } else {
                writeProfile(p, profile);
            }
        }
        auto out = std::make_shared<PsramString>();
        struct PsramWriter {
            PsramString &s;
            size_t write(uint8_t c) {
                s.push_back(static_cast<char>(c));
                return 1;
            }
            size_t write(const uint8_t *buf, size_t n) {
                s.append(reinterpret_cast<const char *>(buf), n);
                return n;
            }
        } writer{*out};
        serializeJson(doc, writer);
        slot = std::move(out);
    }
    return slot;
}

void WebUIPlugin::handleProfilesRest(AsyncWebServerRequest *request) {
    // Split "/api/profiles[/{id}[/{action}]]" into id + action.
    String rest = request->url().substring(String("/api/profiles").length());
    if (rest.startsWith("/"))
        rest = rest.substring(1);
    if (rest.endsWith("/"))
        rest = rest.substring(0, rest.length() - 1);
    String id = rest;
    String action = "";
    const int slash = rest.indexOf('/');
    if (slash >= 0) {
        id = rest.substring(0, slash);
        action = rest.substring(slash + 1);
    }

    const auto sendJson = [request](int code, JsonDocument &doc) {
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        response->setCode(code);
        serializeJson(doc, *response);
        request->send(response);
    };
    const auto sendError = [&sendJson](int code, const char *message) {
        JsonDocument doc(&psramAllocator);
        doc["error"] = message;
        sendJson(code, doc);
    };
    const auto sendProfile = [&sendJson](const Profile &profile, int code) {
        JsonDocument doc(&psramAllocator);
        auto obj = doc.to<JsonObject>();
        writeProfile(obj, profile);
        sendJson(code, doc);
    };
    const auto sendOk = [request]() { request->send(200, "application/json", R"({"ok":true})"); };

    // Take ownership of any buffered JSON body.
    auto *body = static_cast<char *>(request->_tempObject);
    request->_tempObject = nullptr;
    JsonDocument bodyDoc(&psramAllocator);
    bool bodyIsObject = false;
    if (body != nullptr) {
        bodyIsObject = deserializeJson(bodyDoc, static_cast<const char *>(body)) == DeserializationError::Ok &&
                       bodyDoc.is<JsonObjectConst>();
        free(body);
    }

    if (id.isEmpty()) { // Collection: /api/profiles
        if (request->method() == HTTP_GET) {
            // Served from the PSRAM cache in chunks: no 10 KB String in internal RAM per request.
            auto json = profileListJson(request->hasArg("minimal"));
            AsyncWebServerResponse *response =
                request->beginResponse("application/json", json->size(), [json](uint8_t *out, size_t maxLen, size_t index) -> size_t {
                    const size_t remaining = index < json->size() ? json->size() - index : 0;
                    const size_t n = remaining < maxLen ? remaining : maxLen;
                    memcpy(out, json->data() + index, n);
                    return n;
                });
            request->send(response);
            return;
        }
        if (request->method() == HTTP_POST) {
            if (!bodyIsObject) {
                return sendError(400, "Request body must be a JSON profile object");
            }
            Profile profile;
            auto obj = bodyDoc.as<JsonObject>();
            if (!parseProfile(obj, profile)) {
                return sendError(422, PROFILE_VALIDATION_ERROR);
            }
            if (!profile.id.isEmpty() && profileManager->profileExists(profile.id)) {
                return sendError(409, "Profile id already exists; use PUT /api/profiles/{id} to update");
            }
            if (!profileManager->saveProfile(profile)) {
                return sendError(500, "Save failed");
            }
            sendProfile(profile, 201);
            return;
        }
        return sendError(405, "Method not allowed");
    }

    if (rest == "reorder") { // POST /api/profiles/reorder {"ids": [...]} ("order" also accepted, as the socket)
        if (request->method() != HTTP_POST) {
            return sendError(405, "Method not allowed");
        }
        JsonArrayConst ids = bodyDoc["ids"].is<JsonArrayConst>() ? bodyDoc["ids"].as<JsonArrayConst>()
                                                                  : bodyDoc["order"].as<JsonArrayConst>();
        if (ids.isNull()) {
            return sendError(400, "Body must be {\"ids\": [profile ids in display order]}");
        }
        std::vector<String> order;
        for (JsonVariantConst v : ids) {
            order.emplace_back(v.as<String>());
        }
        controller->getSettings().setProfileOrder(order);
        profileManager->bumpRevision(); // the order is part of the list
        sendOk();
        return;
    }

    if (action.isEmpty()) { // Item: /api/profiles/{id}
        if (request->method() == HTTP_GET) {
            Profile profile;
            if (!profileManager->loadProfile(id, profile)) {
                return sendError(404, "Profile not found");
            }
            sendProfile(profile, 200);
            return;
        }
        if (request->method() == HTTP_PUT) {
            if (!profileManager->profileExists(id)) {
                return sendError(404, "Profile not found; use POST /api/profiles to create");
            }
            if (!bodyIsObject) {
                return sendError(400, "Request body must be a JSON profile object");
            }
            Profile profile;
            auto obj = bodyDoc.as<JsonObject>();
            if (!parseProfile(obj, profile)) {
                return sendError(422, PROFILE_VALIDATION_ERROR);
            }
            profile.id = id;
            if (!profileManager->saveProfile(profile)) {
                return sendError(500, "Save failed");
            }
            sendProfile(profile, 200);
            return;
        }
        if (request->method() == HTTP_DELETE) {
            if (!profileManager->profileExists(id)) {
                return sendError(404, "Profile not found");
            }
            if (!profileManager->deleteProfile(id)) {
                return sendError(500, "Delete failed");
            }
            sendOk();
            return;
        }
        return sendError(405, "Method not allowed");
    }

    // Action: /api/profiles/{id}/{action}
    if (request->method() != HTTP_POST) {
        return sendError(405, "Method not allowed");
    }
    if (!profileManager->profileExists(id)) {
        return sendError(404, "Profile not found");
    }
    if (action == "select") {
        profileManager->selectProfile(id);
        sendOk();
        return;
    }
    if (action == "favorite") {
        profileManager->addFavoritedProfile(id);
        sendOk();
        return;
    }
    if (action == "unfavorite") {
        profileManager->removeFavoritedProfile(id);
        sendOk();
        return;
    }
    sendError(404, "Unknown action; expected select, favorite, unfavorite or reorder");
}

// --- REST command surface -------------------------------------------------------------------
// The only command path into the machine: the WebSocket has been push-only (evt:* messages)
// since 2026-08-31. Responses: 200 {"ok":true} or a small JSON object; 4xx {"error": "..."}.

namespace {

bool takeJsonBody(AsyncWebServerRequest *request, JsonDocument &doc) {
    auto *body = static_cast<char *>(request->_tempObject);
    request->_tempObject = nullptr;
    if (body == nullptr) {
        return false;
    }
    const DeserializationError err = deserializeJson(doc, static_cast<const char *>(body));
    free(body);
    return !err;
}

void replyJson(AsyncWebServerRequest *request, int code, const JsonDocument &doc) {
    String body;
    serializeJson(doc, body);
    request->send(code, "application/json", body);
}

void replyOk(AsyncWebServerRequest *request) { request->send(200, "application/json", R"({"ok":true})"); }

void replyError(AsyncWebServerRequest *request, int code, const char *message) {
    JsonDocument doc(&psramAllocator);
    doc["error"] = message;
    replyJson(request, code, doc);
}

// 405 with the Allow header RFC 9110 requires on that status.
void replyMethodNotAllowed(AsyncWebServerRequest *request, const char *allow) {
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    response->setCode(405);
    response->addHeader("Allow", allow);
    JsonDocument doc(&psramAllocator);
    doc["error"] = "Method not allowed";
    serializeJson(doc, *response);
    request->send(response);
}

// Accepts the numeric mode or its name; returns -1 when neither.
int parseMode(JsonVariantConst v) {
    if (v.is<int>()) {
        const int m = v.as<int>();
        return (m >= MODE_STANDBY && m <= MODE_GRIND) ? m : -1;
    }
    if (v.is<const char *>()) {
        const String name = v.as<String>();
        static const char *const names[] = {"standby", "brew", "steam", "water", "grind"};
        for (int m = 0; m < 5; m++) {
            if (name.equalsIgnoreCase(names[m])) {
                return m;
            }
        }
    }
    return -1;
}

} // namespace

void WebUIPlugin::handleMachineRest(AsyncWebServerRequest *request) {
    const String url = request->url();
    const auto method = request->method();
    JsonDocument body(&psramAllocator);
    takeJsonBody(request, body); // optional for most routes

    if (url == "/api/mode") {
        if (method != HTTP_POST) {
            return replyError(request, 405, "POST {\"mode\": 0-4 | \"standby\"|\"brew\"|\"steam\"|\"water\"|\"grind\"}");
        }
        const int mode = parseMode(body["mode"]);
        if (mode < 0) {
            return replyError(request, 400, "mode must be 0-4 or one of standby, brew, steam, water, grind");
        }
        controller->deactivate();
        controller->clear();
        controller->setMode(mode);
        JsonDocument doc(&psramAllocator);
        doc["mode"] = mode;
        return replyJson(request, 200, doc);
    }
    if (url.startsWith("/api/process") || url.startsWith("/api/grind") || url == "/api/flush") {
        if (method != HTTP_POST) {
            return replyError(request, 405, "Method not allowed");
        }
        if (url == "/api/process/activate") {
            controller->activate();
        } else if (url == "/api/process/deactivate") {
            controller->deactivate();
            controller->clear();
        } else if (url == "/api/process/clear") {
            controller->clear();
        } else if (url == "/api/grind/activate") {
            controller->activateGrind();
        } else if (url == "/api/grind/deactivate") {
            controller->deactivateGrind();
        } else if (url == "/api/flush") {
            controller->onFlush();
        } else {
            return replyError(request, 404, "Unknown action");
        }
        return replyOk(request);
    }
    if (url == "/api/autotune") {
        if (method != HTTP_POST) {
            return replyError(request, 405, "POST {\"time\": s, \"samples\": n, \"wattage\": W}");
        }
        const int testTime = body["time"] | 0;
        const int samples = body["samples"] | 0;
        const int wattage = body["wattage"] | 0; // 0 = skip combinedKff derivation, as on the socket path
        if (testTime <= 0 || samples <= 0) {
            return replyError(request, 400, "time and samples must be positive");
        }
        controller->autotune(testTime, samples, wattage);
        JsonDocument doc(&psramAllocator);
        doc["status"] = "started";
        return replyJson(request, 202, doc);
    }
    if (url.startsWith("/api/targets/")) {
        // /api/targets/{temperature|brew|grind}/{raise|lower}  POST, one step
        // /api/targets/{temperature|grind}                     PUT {"value": n}, absolute
        // /api/targets/mode                                    PUT {"volumetric": bool}
        // brew has no absolute form: its target is the selected profile's duration or
        // volumetric target, which raiseBrewTarget()/lowerBrewTarget() step.
        const String rest = url.substring(String("/api/targets/").length());
        String which = rest, dir;
        const int slash = rest.indexOf('/');
        if (slash >= 0) {
            which = rest.substring(0, slash);
            dir = rest.substring(slash + 1);
        }
        if (method == HTTP_POST && (dir == "raise" || dir == "lower")) {
            const bool up = dir == "raise";
            if (which == "temperature") {
                up ? controller->raiseTemp() : controller->lowerTemp();
            } else if (which == "brew") {
                up ? controller->raiseBrewTarget() : controller->lowerBrewTarget();
            } else if (which == "grind") {
                up ? controller->raiseGrindTarget() : controller->lowerGrindTarget();
            } else {
                return replyError(request, 404, "Unknown target; expected temperature, brew or grind");
            }
            return replyOk(request);
        }
        if (method == HTTP_PUT && dir.isEmpty() && which == "mode") {
            // /api/targets/mode {"volumetric": bool}: brew/grind targets are time-based or
            // volumetric (weight, needs a scale). One setting covers both targets.
            if (!body["volumetric"].is<bool>()) {
                return replyError(request, 400, "Body must be {\"volumetric\": true|false}");
            }
            controller->getSettings().setVolumetricTarget(body["volumetric"].as<bool>());
            JsonDocument doc(&psramAllocator);
            doc["volumetric"] = controller->getSettings().isVolumetricTarget();
            return replyJson(request, 200, doc);
        }
        if (method == HTTP_PUT && dir.isEmpty()) {
            if (!body["value"].is<float>()) {
                return replyError(request, 400, "Body must be {\"value\": number}");
            }
            const float value = body["value"].as<float>();
            JsonDocument doc(&psramAllocator);
            if (which == "temperature") {
                const float t = constrain(value, static_cast<float>(MIN_TEMP), static_cast<float>(MAX_TEMP));
                controller->setTargetTemp(t);
                doc["temperature"] = t;
            } else if (which == "grind") {
                if (value <= 0) {
                    return replyError(request, 400, "grind target must be positive");
                }
                controller->getSettings().setTargetGrindVolume(value);
                doc["grind"] = value;
            } else {
                return replyError(request, 404, "Absolute value supported for temperature and grind only");
            }
            return replyJson(request, 200, doc);
        }
        return replyError(request, 405, "POST .../raise|lower or PUT {\"value\": n}");
    }
    replyError(request, 404, "Unknown route");
}

void WebUIPlugin::handleOtaRest(AsyncWebServerRequest *request) {
    const String url = request->url();
    const auto method = request->method();
    if (url == "/api/ota") {
        if (method == HTTP_GET) {
            JsonDocument doc(&psramAllocator);
            buildOTAStatus(doc);
            return replyJson(request, 200, doc);
        }
        if (method == HTTP_POST) {
            // Optionally switch the release channel, then schedule an update check.
            JsonDocument body(&psramAllocator);
            takeJsonBody(request, body);
            if (body["channel"].is<const char *>()) {
                const String channel = body["channel"].as<String>() == "latest" ? "latest" : "nightly";
                controller->getSettings().setOTAChannel(channel);
                ota->setReleaseUrl(RELEASE_URL + (channel == "latest" ? "latest" : "tag/nightly"));
            }
            lastUpdateCheck = 0; // loop() performs the (blocking, TLS) check
            JsonDocument doc(&psramAllocator);
            doc["status"] = "checking";
            doc["channel"] = controller->getSettings().getOTAChannel();
            return replyJson(request, 202, doc);
        }
        return replyError(request, 405, "GET for status, POST {\"channel\": \"latest\"|\"nightly\"} to re-check");
    }
    if (url == "/api/ota/start") {
        if (method != HTTP_POST) {
            return replyError(request, 405, "POST {\"component\": \"display\"|\"controller\"}");
        }
        JsonDocument body(&psramAllocator);
        takeJsonBody(request, body);
        const String component = body["component"].is<const char *>() ? body["component"].as<String>()
                                                                        : body["cp"].as<String>();
        updateComponent = component;
        updating = true;
        JsonDocument doc(&psramAllocator);
        doc["status"] = "started";
        doc["component"] = component;
        return replyJson(request, 202, doc);
    }
    replyError(request, 404, "Unknown route");
}

void WebUIPlugin::handleHistoryRest(AsyncWebServerRequest *request) {
    const String url = request->url();
    const auto method = request->method();
    if (url == "/api/history/rebuild") {
        if (method != HTTP_POST) {
            return replyError(request, 405, "Method not allowed");
        }
        ShotHistory.startAsyncRebuild(); // progress arrives as evt:history-rebuild-progress
        JsonDocument doc(&psramAllocator);
        doc["status"] = "rebuilding";
        return replyJson(request, 202, doc);
    }
    // DELETE /api/history/{id}      PUT /api/history/{id}.json (notes)
    String id = url.substring(String("/api/history/").length());
    const bool notes = id.endsWith(".json");
    if (notes) {
        id = id.substring(0, id.length() - 5);
    }
    if (id.isEmpty()) {
        return replyError(request, 404, "Unknown route");
    }
    for (size_t i = 0; i < id.length(); i++) {
        if (!isdigit(static_cast<unsigned char>(id[i]))) {
            return replyError(request, 400, "Shot id must be numeric");
        }
    }
    if (method == HTTP_DELETE && !notes) {
        ShotHistory.deleteShot(id);
        return replyOk(request);
    }
    if (method == HTTP_PUT && notes) {
        JsonDocument body(&psramAllocator);
        if (!takeJsonBody(request, body) || !body.is<JsonObject>()) {
            return replyError(request, 400, "Body must be a JSON notes object");
        }
        ShotHistory.saveShotNotes(id, body);
        return replyOk(request);
    }
    replyError(request, 405, "DELETE /api/history/{id} or PUT /api/history/{id}.json");
}

void WebUIPlugin::handleBLEScaleList(AsyncWebServerRequest *request) {
    JsonDocument doc(&psramAllocator);
    JsonArray scalesArray = doc.to<JsonArray>();
    const std::vector<DiscoveredDevice> devices = BLEScales.getDiscoveredScales(); // returned by value
    for (const DiscoveredDevice &device : devices) {
        JsonObject scale = scalesArray.add<JsonObject>();
        scale["uuid"] = device.getAddress().toString();
        scale["name"] = device.getName();
        scale["rssi"] = device.getRSSI();
    }
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    serializeJson(doc, *response);
    request->send(response);
}

void WebUIPlugin::handleBLEScaleScan(AsyncWebServerRequest *request) {
    if (request->method() != HTTP_POST) {
        return replyMethodNotAllowed(request, "POST");
    }
    BLEScales.scan();
    JsonDocument doc(&psramAllocator);
    doc["success"] = true;
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    serializeJson(doc, *response);
    request->send(response);
}

void WebUIPlugin::handleBLEScaleConnect(AsyncWebServerRequest *request) {
    if (request->method() != HTTP_POST) {
        return replyMethodNotAllowed(request, "POST");
    }
    const String uuid = request->arg("uuid"); // form field or query parameter
    if (uuid.isEmpty()) {
        return replyError(request, 400, "Missing uuid");
    }
    BLEScales.connect(uuid.c_str());
    JsonDocument doc(&psramAllocator);
    doc["success"] = true;
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    serializeJson(doc, *response);
    request->send(response);
}

void WebUIPlugin::handleBLEScaleInfo(AsyncWebServerRequest *request) {
    JsonDocument doc(&psramAllocator);
    doc["connected"] = BLEScales.isConnected();
    doc["name"] = BLEScales.getName();
    doc["uuid"] = BLEScales.getUUID();
    doc["rssi"] = BLEScales.getRSSI();
    doc["hasBattery"] = BLEScales.hasBatteryLevel();
    // Only surface the numeric when the scale reports one — a 255 sentinel
    // (REMOTE_SCALES_BATTERY_UNKNOWN) would otherwise render as a fake "255%".
    if (BLEScales.hasBatteryLevel()) {
        const uint8_t pct = BLEScales.getBatteryLevel();
        if (pct != REMOTE_SCALES_BATTERY_UNKNOWN) {
            doc["battery"] = pct;
        }
    }
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    serializeJson(doc, *response);
    request->send(response);
}

void WebUIPlugin::buildOTAStatus(JsonDocument &doc) const {
    Settings const &settings = controller->getSettings();
    doc["displayUpdateAvailable"] = ota->isUpdateAvailable(false);
    doc["controllerUpdateAvailable"] = ota->isUpdateAvailable(true);
    doc["displayVersion"] = BUILD_GIT_VERSION;
    doc["controllerVersion"] = controller->getSystemInfo().version;
    doc["hardware"] = controller->getSystemInfo().hardware;
    doc["latestVersion"] = ota->getCurrentVersion();
    doc["channel"] = settings.getOTAChannel();
    doc["updating"] = updating;
    // LittleFS usage metrics
    {
        size_t total = LittleFS.totalBytes();
        size_t used = LittleFS.usedBytes();
        size_t freeBytes = total > used ? (total - used) : 0;
        doc["spiffsTotal"] = static_cast<uint32_t>(total);
        doc["spiffsUsed"] = static_cast<uint32_t>(used);
        doc["spiffsFree"] = static_cast<uint32_t>(freeBytes);
        if (total > 0) {
            doc["spiffsUsedPct"] = static_cast<uint8_t>((used * 100) / total);
        }
    }
    // Memory usage metrics
    {
        size_t free = heap_caps_get_free_size(MALLOC_CAP_DEFAULT | MALLOC_CAP_INTERNAL);
        size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT | MALLOC_CAP_INTERNAL);
        size_t total = heap_caps_get_total_size(MALLOC_CAP_DEFAULT | MALLOC_CAP_INTERNAL);
        doc["heapFree"] = static_cast<uint32_t>(free);
        doc["heapLargest"] = static_cast<uint32_t>(largest);
        doc["heapTotal"] = static_cast<uint32_t>(total);
        // PSRAM is the other budget: JSON work, the LVGL buffers and response caches live there.
        doc["psramFree"] = static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        doc["psramLargest"] = static_cast<uint32_t>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
        doc["heapMinFree"] = static_cast<uint32_t>(heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT | MALLOC_CAP_INTERNAL));
    }
    // Render pipeline over the last PANEL_STATS_PERIOD window (see PanelStats.h): UI frames rendered, LVGL flushes into
    // the framebuffer, and RGB panel refreshes. Nominal on the T-RGB: ~40 fps while animating, ~23.5 Hz vsync.
    {
        doc["uiFps"] = panelSnapshot.uiFps;
        doc["flushAvgUs"] = panelSnapshot.flushAvgUs;
        doc["flushMaxUs"] = panelSnapshot.flushMaxUs;
        doc["panelVsyncHz"] = panelSnapshot.vsyncHz;
        doc["panelLateVsyncs"] = panelSnapshot.vsyncsLate;
    }
    doc["controllerTaskHealth"] = controller->isTaskHealthy();
#ifndef GAGGIMATE_HEADLESS
    doc["uiTaskHealth"] = controller->getUI()->isTaskHealthy();
#endif
    // Controller link health: counters since boot. Retransmits are normal under
    // Wi-Fi load; give-ups are commands the controller never received.
    {
        const GaggiMateClient *client = controller->getClientController();
        const auto s = client->getLinkStats(); // Endpoint::LinkStats; the sim mock mirrors the fields
        const Controller::LinkHealth h = controller->getLinkHealth();
        JsonObject link = doc["link"].to<JsonObject>();
        link["connected"] = client->isConnected();
        link["rttMs"] = client->hasLatency() ? static_cast<int>(client->getLatencyMs()) : -1;
        link["rttMaxMs"] = s.rttMaxMs;
        link["txFrames"] = s.txFrames;
        link["rxFrames"] = s.rxFrames;
        link["retransmits"] = s.retransmits;
        link["giveUps"] = s.giveUps;
        link["sendFailures"] = s.sendFailures;
        link["encodeFailures"] = s.encodeFailures;
        link["duplicates"] = s.duplicates;
        link["rxBackpressure"] = s.rxBackpressure;
        link["disconnects"] = h.disconnects;
        link["lastGapMs"] = h.lastGapMs;
        link["maxGapMs"] = h.maxGapMs;
        link["linkUptimeMs"] = h.connectedAt != 0 ? static_cast<uint32_t>(millis() - h.connectedAt) : 0;
    }
    if (controller->isSDCard()) {
        const uint64_t total = SD_MMC.cardSize();
        const uint64_t used = SD_MMC.usedBytes();
        const uint64_t freeBytes = total > used ? (total - used) : 0;
        doc["sdTotal"] = total;
        doc["sdUsed"] = used;
        doc["sdFree"] = freeBytes;
        if (total > 0) {
            // Provide integer percentage to avoid float JSON
            doc["sdUsedPct"] = static_cast<uint8_t>((used * 100) / total);
        }
    }
}

void WebUIPlugin::updateOTAStatus() {
    if (ws.getClients().empty()) {
        return;
    }
    JsonDocument doc(&psramAllocator);
    doc["tp"] = "evt:ota-status";
    buildOTAStatus(doc);
    broadcastJson(doc);
}

// Firmware upload is device-only: the simulator's ESPAsyncWebServer shim has no
// multipart upload support, so these are compiled out there rather than faked.
#ifndef GAGGIMATE_SIM
bool WebUIPlugin::isUploadAuthorized(AsyncWebServerRequest *request) const {
    const String expected = controller->getSettings().getOtaUploadToken();
    if (expected.isEmpty()) {
        return false; // fail closed: upload stays disabled until a token is set
    }
    String provided;
    if (request->hasHeader("X-OTA-Token")) {
        provided = request->getHeader("X-OTA-Token")->value();
    } else if (request->hasArg("token")) {
        provided = request->arg("token");
    }
    if (provided.length() != expected.length()) {
        return false;
    }
    // Constant-time compare so a wrong token cannot be recovered byte by byte.
    uint8_t diff = 0;
    for (size_t i = 0; i < expected.length(); i++) {
        diff |= static_cast<uint8_t>(provided[i]) ^ static_cast<uint8_t>(expected[i]);
    }
    return diff == 0;
}

void WebUIPlugin::handleFirmwareUpload(AsyncWebServerRequest *request, size_t index, uint8_t *data, size_t len,
                                       size_t total) {
    const bool final = index + len >= total;
    if (index == 0) {
        if (!isUploadAuthorized(request)) {
            ESP_LOGW("WebUIPlugin", "Rejected firmware upload: bad or missing token");
            return; // result handler turns this into a 401
        }
        if (uploadInProgress || Update.isRunning()) {
            ESP_LOGW("WebUIPlugin", "Rejected firmware upload: another update is already running");
            uploadError = "another update is already running";
            return;
        }
        uploadError = "";
        uploadTotal = 0;
        // ?target=fs writes the LittleFS image instead of the app. That is how a
        // filesystem backup is restored without a cable -- profiles and shot
        // history live there, and there is no other write path for .slog files.
        // Unlike the app, the filesystem partition is single-banked: a failed
        // write leaves it corrupt and the next mount reformats it
        // (Controller.cpp's LittleFS.begin(true)). Only ever push an image you
        // still hold a copy of.
        const bool toFilesystem = request->hasArg("target") && request->arg("target") == "fs";
        uploadCommand = toFilesystem ? U_SPIFFS : U_FLASH;
        // Content-Length sizes the update, so an image too big for the partition
        // is refused before a byte is written. For U_FLASH the Updater also
        // aborts on the first chunk if the ESP image magic byte is wrong, so a
        // stray file cannot be half-written; that check does not apply to
        // U_SPIFFS, which has no header to validate.
        if (!Update.begin(total > 0 ? total : UPDATE_SIZE_UNKNOWN, uploadCommand)) {
            uploadError = String("Update.begin: ") + Update.errorString();
            ESP_LOGE("WebUIPlugin", "Update.begin failed: %s", Update.errorString());
            return;
        }
        uploadInProgress = true;
        uploadLastPct = -1;
        uploadLastChunk = millis();
        // ota:upload:* has no listeners; ota:update:* has five, including the
        // one that hands the shared radio to Wi-Fi for a display update and the
        // one that puts an update screen on the panel. Streaming megabytes over
        // Wi-Fi without them left BLE fighting for the antenna, which is how a
        // controller link gets dropped mid-upload. component=display is what
        // tells Controller to leave BLE relaxed rather than preferring it.
        pluginManager->trigger("ota:update:start", "component", "display");
        pluginManager->trigger("ota:upload:start");
        ESP_LOGI("WebUIPlugin", "%s upload started", toFilesystem ? "Filesystem" : "Firmware");
    }
    if (!uploadInProgress) {
        return;
    }
    if (len && Update.write(data, len) != len) {
        // Capture before abort(): abort() overwrites the reason with "Aborted".
        uploadError = String("Update.write: ") + Update.errorString() + " at " + String(uploadTotal) + " bytes";
        ESP_LOGE("WebUIPlugin", "Update.write failed: %s", Update.errorString());
        Update.abort();
        uploadInProgress = false;
        pluginManager->trigger("ota:update:end");
        return;
    }
    uploadTotal += len;
    uploadLastChunk = millis();
    // Progress is reported against the partition size, which is the only bound
    // available while streaming; it is a lower bound on the real percentage.
    const size_t capacity = Update.size();
    if (capacity > 0) {
        const int pct = static_cast<int>((uploadTotal * 100) / capacity);
        if (pct != uploadLastPct) {
            uploadLastPct = pct;
            updateOTAProgress(PHASE_DISPLAY_FW, pct);
        }
    }
    if (final) {
        if (!Update.end(true)) {
            uploadError = String("Update.end: ") + Update.errorString() + " after " + String(uploadTotal) + " bytes";
            ESP_LOGE("WebUIPlugin", "Update.end failed: %s", Update.errorString());
            uploadInProgress = false;
            pluginManager->trigger("ota:update:end");
            return;
        }
        ESP_LOGI("WebUIPlugin", "Firmware upload complete: %u bytes", static_cast<unsigned>(uploadTotal));
    }
}

void WebUIPlugin::handleFirmwareUploadResult(AsyncWebServerRequest *request) {
    if (!isUploadAuthorized(request)) {
        request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
        return;
    }
    const bool ok = uploadInProgress && !Update.hasError() && Update.isFinished();
    uploadInProgress = false;
    if (!ok) {
        // uploadError names the real cause; the Updater's own string is only a fallback,
        // and reads "Aborted" whenever abort() has run.
        const String reason = uploadError.isEmpty() ? String(Update.errorString()) : uploadError;
        ESP_LOGE("WebUIPlugin", "Firmware upload failed: %s", reason.c_str());
        Update.abort();
        JsonDocument doc(&psramAllocator);
        doc["error"] = reason;
        doc["received"] = static_cast<uint32_t>(uploadTotal);
        String body;
        serializeJson(doc, body);
        request->send(400, "application/json", body);
        pluginManager->trigger("ota:update:end");
        pluginManager->trigger("ota:upload:failed");
        return;
    }
    AsyncWebServerResponse *response =
        request->beginResponse(200, "application/json", "{\"status\":\"ok\",\"restarting\":true}");
    response->addHeader("Connection", "close");
    request->send(response);
    updateOTAProgress(PHASE_FINISHED, 100);
    pluginManager->trigger("ota:update:end");
    pluginManager->trigger("ota:upload:finished");
    ESP_LOGI("WebUIPlugin", "Restarting into newly uploaded firmware");
    restartPending = millis() + 1000; // let the response flush before rebooting
}
#endif

void WebUIPlugin::updateOTAProgress(uint8_t phase, int progress) {
    if (ws.getClients().empty()) {
        return;
    }
    JsonDocument doc(&psramAllocator);
    doc["tp"] = "evt:ota-progress";
    doc["phase"] = phase;
    doc["progress"] = progress;
    broadcastJson(doc);
}

void WebUIPlugin::broadcastJson(JsonDocument &doc) {
    if (ws.getClients().empty()) {
        return;
    }
    ws.textAll(toWsBuffer(doc));
}

void WebUIPlugin::sendAutotuneResult() {
    JsonDocument doc(&psramAllocator);
    doc["tp"] = "evt:autotune-result";
    doc["pid"] = controller->getSettings().getPid();
    broadcastJson(doc);
}

void WebUIPlugin::sendAutotuneFailed() {
    // Distinct WS event — Autotune page renders "timed out" error card
    // instead of stuck spinner. Fires on ERROR_CODE_AUTOTUNE_TIMEOUT.
    JsonDocument doc(&psramAllocator);
    doc["tp"] = "evt:autotune-failed";
    broadcastJson(doc);
}

// GET /api/debug/heap: internal-heap attribution. `checkpoints` are the boot stages and brew events recorded by
// heapCheckpoint(); `tasks` lists every FreeRTOS task with its stack headroom (bytes) and cumulative CPU share since
// boot (needs CONFIG_FREERTOS_USE_TRACE_FACILITY + GENERATE_RUN_TIME_STATS, both on in the prebuilt config).
void WebUIPlugin::handleHeapDebug(AsyncWebServerRequest *request) {
    JsonDocument doc(&psramAllocator);
    const uint32_t caps = MALLOC_CAP_DEFAULT | MALLOC_CAP_INTERNAL;
    JsonObject now = doc["now"].to<JsonObject>();
    now["free"] = static_cast<uint32_t>(heap_caps_get_free_size(caps));
    now["largest"] = static_cast<uint32_t>(heap_caps_get_largest_free_block(caps));
    now["minFree"] = static_cast<uint32_t>(heap_caps_get_minimum_free_size(caps));
    now["total"] = static_cast<uint32_t>(heap_caps_get_total_size(caps));
    now["psramFree"] = static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    now["uptimeMs"] = static_cast<uint32_t>(millis());
    JsonArray cps = doc["checkpoints"].to<JsonArray>();
    size_t count = 0;
    const HeapCheckpoint *table = heapCheckpoints(count);
    for (size_t i = 0; i < count; i++) {
        JsonObject o = cps.add<JsonObject>();
        o["label"] = table[i].label;
        o["t"] = table[i].atMs;
        o["free"] = table[i].free;
        o["largest"] = table[i].largest;
        o["minFree"] = table[i].minFree;
    }
#ifndef GAGGIMATE_SIM
    JsonArray tasks = doc["tasks"].to<JsonArray>();
    const UBaseType_t n = uxTaskGetNumberOfTasks();
    auto *status = static_cast<TaskStatus_t *>(heap_caps_malloc(n * sizeof(TaskStatus_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (status != nullptr) {
        uint32_t totalRunTime = 0;
        const UBaseType_t got = uxTaskGetSystemState(status, n, &totalRunTime);
        for (UBaseType_t i = 0; i < got; i++) {
            JsonObject o = tasks.add<JsonObject>();
            o["name"] = status[i].pcTaskName;
            o["prio"] = status[i].uxCurrentPriority;
            o["stackFree"] = status[i].usStackHighWaterMark;
#if CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID
            o["core"] = status[i].xCoreID;
#endif
            if (totalRunTime > 0)
                o["cpuPct"] = static_cast<float>(status[i].ulRunTimeCounter) * 100.0f / static_cast<float>(totalRunTime);
        }
        heap_caps_free(status);
    }
#endif
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    serializeJson(doc, *response);
    request->send(response);
}

void WebUIPlugin::handleCoreDumpDownload(AsyncWebServerRequest *request) {
    // Check if core dump is available
    size_t coreAddr, coreSize;
    if (esp_core_dump_image_get(&coreAddr, &coreSize) != ESP_OK || coreSize == 0) {
        request->send(404, "text/plain", "No core dump available");
        return;
    }

    // Find the coredump partition
    const esp_partition_t *coredump_partition =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, NULL);
    if (coredump_partition == NULL) {
        request->send(500, "text/plain", "Core dump partition not found");
        return;
    }

    ESP_LOGI("WebUIPlugin", "Streaming core dump: %u bytes from 0x%lx", static_cast<unsigned>(coreSize),
             static_cast<unsigned long>(coreAddr));

    // Create a streaming response
    AsyncWebServerResponse *response =
        request->beginResponse("application/octet-stream", coreSize,
                               [coredump_partition, coreSize](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
                                   // Calculate how much to read
                                   size_t remaining = coreSize - index;
                                   size_t toRead = (remaining < maxLen) ? remaining : maxLen;

                                   if (toRead == 0)
                                       return 0;

                                   // Read from partition
                                   esp_err_t err = esp_partition_read(coredump_partition, index, buffer, toRead);
                                   if (err != ESP_OK) {
                                       ESP_LOGE("WebUIPlugin", "Failed to read core dump: %s", esp_err_to_name(err));
                                       return 0;
                                   }

                                   return toRead;
                               });

    // Set appropriate headers
    response->addHeader("Content-Disposition", "attachment; filename=\"coredump.bin\"");
    response->addHeader("Cache-Control", "no-cache");

    request->send(response);
}
