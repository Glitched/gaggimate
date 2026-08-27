#ifndef WEBUIPLUGIN_H
#define WEBUIPLUGIN_H

#define ELEGANTOTA_USE_ASYNC_WEBSERVER 1

#include <Update.h> // U_FLASH / U_SPIFFS
#include <DNSServer.h>

#include "GitHubOTA.h"
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <display/core/Plugin.h>
#include <display/util/PsramAllocator.h>

constexpr size_t UPDATE_CHECK_INTERVAL = 30 * 60 * 1000;
constexpr size_t CLEANUP_PERIOD = 1000;
constexpr size_t STATUS_PERIOD = 500;
constexpr size_t DNS_PERIOD = 50;
// How long a firmware upload may go without a chunk before the Updater is
// reclaimed. Comfortably longer than any real network hiccup, short enough that
// a user who lost a transfer can just retry instead of power-cycling.
constexpr unsigned long UPLOAD_STALL_TIMEOUT = 30 * 1000;

const String LOCAL_URL = "http://4.4.4.1/";
const String RELEASE_URL = "https://github.com/Glitched/gaggimate/releases/";

class ProfileManager;

class WebUIPlugin : public Plugin {
  public:
    WebUIPlugin();
    void setup(Controller *controller, PluginManager *pluginManager) override;
    void loop() override;

  private:
    void setupServer();
    void start();
    void stop();

    // Websocket handlers
    void handleWebSocketData(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data,
                             size_t len);
    void handleOTASettings(uint32_t clientId, JsonDocument &request);
    void handleOTAStart(uint32_t clientId, JsonDocument &request);
    void handleAutotuneStart(uint32_t clientId, JsonDocument &request);
    void handleProfileRequest(uint32_t clientId, JsonDocument &request);
    void handleFlushStart(uint32_t clientId, JsonDocument &request);

    // HTTP handlers
    // Serves the web UI from the firmware-embedded, memory-mapped flash blob
    // (catch-all for any path not claimed by an explicit route). [GM-106]
    void serveWebAsset(AsyncWebServerRequest *request);
    void handleSettings(AsyncWebServerRequest *request) const;
    void handleProfilesRest(AsyncWebServerRequest *request) const;
    void handleBLEScaleList(AsyncWebServerRequest *request);
    void handleBLEScaleScan(AsyncWebServerRequest *request);
    void handleBLEScaleConnect(AsyncWebServerRequest *request);
    void handleBLEScaleInfo(AsyncWebServerRequest *request);
    void updateOTAStatus(const String &version);
    void updateOTAProgress(uint8_t phase, int progress);
    void sendAutotuneResult();
    void sendAutotuneFailed();

    void broadcastJson(JsonDocument &doc);

    // Core dump download
    void handleCoreDumpDownload(AsyncWebServerRequest *request);

    // Direct firmware upload (POST /api/ota/upload). Writes the streamed image
    // into the inactive OTA slot; the running app is never touched, so an
    // aborted upload simply leaves the device on its current firmware.
    // Requires a non-empty otaUploadToken setting -- fails closed when unset.
#ifndef GAGGIMATE_SIM
    bool isUploadAuthorized(AsyncWebServerRequest *request) const;
    void handleFirmwareUpload(AsyncWebServerRequest *request, size_t index, uint8_t *data, size_t len, bool final);
    void handleFirmwareUploadResult(AsyncWebServerRequest *request);
#endif

    // Guards against a second upload racing the first, and lets the completion
    // handler tell "we wrote an image" apart from "the body never arrived".
    // Reboot is deferred to loop() so the HTTP response leaves the socket
    // before the device resets. 0 = no reboot scheduled.
    unsigned long restartPending = 0;
    bool uploadInProgress = false;
    size_t uploadTotal = 0;
    int uploadLastPct = -1;
    unsigned long uploadLastChunk = 0;
    int uploadCommand = U_FLASH; // U_FLASH (app) or U_SPIFFS (LittleFS image)

    GitHubOTA *ota = nullptr;
    AsyncWebServer server;
    AsyncWebSocket ws;
    Controller *controller = nullptr;
    PluginManager *pluginManager = nullptr;
    DNSServer *dnsServer = nullptr;
    ProfileManager *profileManager = nullptr;

    long lastUpdateCheck = 0;
    long lastStatus = 0;
    long lastCleanup = 0;
    long lastDns = 0;
    bool updating = false;
    bool apMode = false;
    bool serverRunning = false;
    String updateComponent = "";
    float currentBluetoothWeight = 0.0f;
    // Reused for every 500ms status broadcast. Allocating a fresh JsonDocument
    // each tick was a major contributor to internal-heap fragmentation
    // (device reports 33%+ fragmentation, causing AsyncTCP buffer allocs to
    // stall mid-asset-serve). Keeping one doc lets its underlying pool grow
    // once and stay put.
    JsonDocument statusDoc{&psramAllocator};
};

#endif // WEBUIPLUGIN_H
