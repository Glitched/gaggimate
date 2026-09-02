#include "DefaultUI.h"

#include <WiFi.h>
#include <display/config.h>
#include <display/core/Controller.h>
#include <display/core/process/BrewProcess.h>
#include <display/core/process/Process.h>
#include <display/core/zones.h>
#ifndef GAGGIMATE_SIM // hardware panel drivers are device-only
#include <display/drivers/AmoledDisplayDriver.h>
#include <display/drivers/LilyGoDriver.h>
#include <display/drivers/WaveshareDriver.h>
#include <display/drivers/common/LV_Helper.h>
#endif
#include <display/main.h>
#include <display/ui/utils/effects.h>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <utility>

#include "esp_sntp.h"

#include <display/ui/default/eez/ui.h>

static EffectManager effect_mgr;

static constexpr uint32_t STARTUP_FADE_MS = 1000; // standby fade-in duration on power-up

static constexpr int32_t GAUGE_TICK_LONG = 25;      // meter tick length on most screens
static constexpr int32_t GAUGE_TICK_SHORT = 10;     // shortened tick length on profile / new-menu screens
static constexpr uint32_t GAUGE_TICK_ANIM_MS = 300; // tick length transition duration

// Profile dial. Presses further than DIAL_INNER_RADIUS from the screen centre belong to the
// dial (the outer ~90 px, where the gauge dots live); inside it, buttons and swipes work as
// before. One detent is a full lap divided by the number of favourites, clamped so a short
// list does not need a quarter turn per step and a long one does not fire on a wobble.
static constexpr float DIAL_INNER_RADIUS = 150.f;
static constexpr float DIAL_DETENT_MIN_DEG = 24.f;
static constexpr float DIAL_DETENT_MAX_DEG = 45.f;

static constexpr uint32_t UI_LOOP_PERIOD_MS = 25; // DefaultUI::loopTask cadence (the sim mirrors it)

// Standby transition: opacity only. The first version also shrank the wordmark, which
// forced LVGL's software resampler on every frame; on the ESP32-S3 that measured 4 frames
// in 205 ms (~50 ms a frame, each blocking the UI task) against 7 in the sim. The wordmark
// bitmap is now pre-scaled to its on-screen size (394x79, zoom 256), so both the animation
// and every ordinary standby redraw are plain alpha blits. Note LVGL charges a new animation
// for the whole tick interval it was created in, so an N ms animation renders in ~N-25 ms.
static constexpr uint32_t STANDBY_EXIT_MS = 200;
static constexpr uint32_t STANDBY_ENTER_MS = 120;    // not latency-critical, but still pure delay
static constexpr uint32_t STANDBY_CANCEL_MS = 120;   // undo a press that did not become a wake
static constexpr lv_opa_t STANDBY_CHEVRON_OPA = 153; // resting chevron opacity (screens.c)

// Profile and the new menu screen show shortened meter ticks.
static bool isShortTickScreen(ScreensEnum s) {
    return s == SCREEN_ID_PROFILE_SCREEN || s == SCREEN_ID_MENU_SCREEN_NEW || s == SCREEN_ID_INFO_SCREEN;
}

// Format a millisecond duration as "m:ss" for the brew/profile time labels.
static void formatDuration(unsigned long ms, char *buf, size_t len) {
    const double seconds = ms / 1000.0;
    const int minutes = static_cast<int>(seconds / 60.0);
    const int secs = static_cast<int>(seconds) % 60;
    snprintf(buf, len, "%d:%02d", minutes, secs);
}

static float clampPercentage(float pct) { return pct < 0.0f ? 0.0f : (pct > 100.0f ? 100.0f : pct); }

// EEZ string setters allocate a fresh StringRef on the LVGL heap each call; skip unchanged text to cut churn.
static bool stringChanged(const char *current, const char *next) {
    return current == nullptr || next == nullptr || strcmp(current, next) != 0;
}

int16_t calculate_angle(int set_temp, int range, int offset) {
    const double percentage = static_cast<double>(set_temp) / static_cast<double>(MAX_TEMP);
    return (percentage * ((double)range)) - range / 2 - offset;
}

void DefaultUI::updateTempHistory() {
    if (currentTemp > 0) {
        if (tempHistoryIndex >= TEMP_HISTORY_LENGTH) {
            tempHistoryIndex = 0;
            isTempHistoryInitialized = true;
        }
        tempHistory[tempHistoryIndex] = currentTemp;
        tempHistoryIndex += 1;
    }

    // 1 Hz heating flash: every fourth 250 ms tick, counted separately from the history index so
    // the cadence holds while the temperature is still invalid (the index used to stall then,
    // and the flash either froze or toggled every tick).
    if (++heatingFlashTick % 4 == 0) {
        heatingFlash = !heatingFlash;
        rerender = true;
    }
}

void DefaultUI::updateTempStableFlag() {
    if (isTempHistoryInitialized) {
        float totalError = 0.0f;
        float maxError = 0.0f;
        for (uint16_t i = 0; i < TEMP_HISTORY_LENGTH; i++) {
            float error = abs(tempHistory[i] - targetTemp);
            totalError += error;
            maxError = error > maxError ? error : maxError;
        }

        const float avgError = totalError / TEMP_HISTORY_LENGTH;
        const float errorMargin = max(2.0f, static_cast<float>(targetTemp) * 0.02f);

        isTemperatureStable = avgError < errorMargin && maxError <= errorMargin;
    }

    // instantly reset stability if setpoint has changed
    if (prevTargetTemp != targetTemp) {
        isTemperatureStable = false;
    }

    prevTargetTemp = targetTemp;
}

void DefaultUI::reloadProfiles() {
    profileLoaded = 0;
#ifndef GAGGIMATE_SIM // the sim has no task notifications; its main loop polls loopProfiles() instead
    if (profileTaskHandle != nullptr) { // may be called before init() has created the task
        xTaskNotifyGive(profileTaskHandle);
    }
#endif
}

DefaultUI::DefaultUI(Controller *controller, Driver *driver, PluginManager *pluginManager)
    : controller(controller), panelDriver(driver), pluginManager(pluginManager) {
    setupPanel();
    xTaskCreatePinnedToCore(loopTask, "DefaultUI::loop", configMINIMAL_STACK_SIZE * 6, this, 1, &taskHandle, 1);
}

void DefaultUI::init() {
    profileManager = controller->getProfileManager();
    auto triggerRender = [this](Event const &) { rerender = true; };
    // The temperature and mode callbacks only ask for a render: updateState() reads the live
    // values from the controller on every render, so writing them here from another task
    // gained nothing.
    pluginManager->on("boiler:currentTemperature:change", [this](Event const &event) {
        const int newTemp = static_cast<int>(event.getFloat("value"));
        if (newTemp != lastTempEvent) {
            lastTempEvent = newTemp;
            rerender = true;
        }
    });
    pluginManager->on("boiler:pressure:change", [this](Event const &event) {
        const float newPressure = event.getFloat("value");
        if (round(newPressure * 10.0f) != round(pressure * 10.0f)) {
            pressure = newPressure;
            rerender = true;
        }
    });
    pluginManager->on("boiler:targetTemperature:change", [this](Event const &event) {
        const int newTemp = static_cast<int>(event.getFloat("value"));
        if (newTemp != lastTargetTempEvent) {
            lastTargetTempEvent = newTemp;
            rerender = true;
        }
    });
    pluginManager->on("controller:targetVolume:change", [=, this](Event const &event) { rerender = true; });
    pluginManager->on("controller:targetDuration:change", [=, this](Event const &event) { rerender = true; });
    pluginManager->on("controller:grindDuration:change", [=, this](Event const &event) { rerender = true; });
    pluginManager->on("controller:grindVolume:change", [=, this](Event const &event) { rerender = true; });
    pluginManager->on("controller:process:end", triggerRender);
    pluginManager->on("controller:process:start", triggerRender);
    pluginManager->on("controller:mode:change", [this](Event const &event) {
        switch (event.getInt("value")) {
        case MODE_STANDBY:
            changeScreen(SCREEN_ID_STANDBY_SCREEN);
            break;
        case MODE_BREW:
            changeScreen(SCREEN_ID_BREW_SCREEN);
            break;
        case MODE_GRIND:
            changeScreen(SCREEN_ID_GRIND_SCREEN);
            break;
        case MODE_STEAM:
            changeScreen(SCREEN_ID_STEAM_SCREEN);
            break;
        case MODE_WATER:
            changeScreen(SCREEN_ID_WATER_SCREEN);
            break;
        default:
            break;
        };
    });
    pluginManager->on("controller:brew:start", [this](Event const &event) { changeScreen(SCREEN_ID_STATUS_SCREEN); });
    pluginManager->on("controller:brew:clear", [this](Event const &event) {
        if (eez_flow_get_current_screen() == SCREEN_ID_STATUS_SCREEN) {
            changeScreen(SCREEN_ID_BREW_SCREEN);
        }
    });
    pluginManager->on("controller:bluetooth:waiting", [this](Event const &) {
        waitingForController = true;
        rerender = true;
    });
    pluginManager->on("controller:bluetooth:connect", [this](Event const &) {
        waitingForController = false;
        rerender = true;
        initialized = true;
        // Stay on the standby screen when the controller is incompatible so the
        // mismatch message remains visible instead of jumping into brew.
        if (eez_flow_get_current_screen() == SCREEN_ID_STANDBY_SCREEN && !controller->getSystemInfo().protocolMismatch) {
            ::Settings &settings = controller->getSettings();
            if (settings.getStartupMode() == MODE_BREW) {
                changeScreen(SCREEN_ID_BREW_SCREEN);
            } else {
                standbyEnterTime = ::millis();
            }
        }
    });
    pluginManager->on("controller:bluetooth:disconnect", [this](Event const &) {
        waitingForController = true;
        rerender = true;
    });
    pluginManager->on("controller:wifi:connect", [this](Event const &event) {
        rerender = true;
        apActive = event.getInt("AP") != 0;
    });
    pluginManager->on("ota:update:start", [this](Event const &) {
        rerender = true;
        changeScreen(SCREEN_ID_STANDBY_SCREEN);
    });
    pluginManager->on("ota:update:end", [this](Event const &) {
        rerender = true;
        changeScreen(SCREEN_ID_STANDBY_SCREEN);
    });
    pluginManager->on("ota:update:status", [this](Event const &event) {
        rerender = true;
        updateAvailable = event.getInt("value") != 0;
    });
    pluginManager->on("controller:error", [this](Event const &) {
        rerender = true;
        changeScreen(SCREEN_ID_STANDBY_SCREEN);
    });
    pluginManager->on("controller:protocol:mismatch", [this](Event const &) {
        // Incompatible firmware on the other end: control is inhibited (OTA only),
        // so surface it on the standby screen like a runaway error.
        rerender = true;
        changeScreen(SCREEN_ID_STANDBY_SCREEN);
    });
    pluginManager->on("controller:autotune:start", [this](Event const &) { changeScreen(SCREEN_ID_STANDBY_SCREEN); });
    pluginManager->on("controller:autotune:result", [this](Event const &) { changeScreen(SCREEN_ID_STANDBY_SCREEN); });

    pluginManager->on("profiles:profile:select", [this](Event const &event) {
        reloadProfiles();
        rerender = true;
    });
    pluginManager->on("profiles:profile:favorite", [this](Event const &event) { reloadProfiles(); });
    pluginManager->on("profiles:profile:unfavorite", [this](Event const &event) { reloadProfiles(); });
    pluginManager->on("profiles:profile:save", [this](Event const &event) { reloadProfiles(); });
    pluginManager->on("controller:volumetric-measurement:bluetooth:change", [this](Event const &event) {
        const float newWeight = event.getFloat("value");
        if (round(newWeight * 10.0f) != round(bluetoothWeight * 10.0f)) {
            bluetoothWeight = newWeight;
            rerender = true;
        }
    });
    xTaskCreatePinnedToCore(profileLoopTask, "DefaultUI::loopProfiles", configMINIMAL_STACK_SIZE * 4, this, 1, &profileTaskHandle,
                            0);
}

void DefaultUI::loop() {
    const unsigned long now = ::millis();
    const unsigned long diff = now - lastRender;

    if (now - lastTempLog > TEMP_HISTORY_INTERVAL) {
        updateTempHistory();
        lastTempLog = now;
    }

    if ((controller->isActive() && diff > RERENDER_INTERVAL_ACTIVE) || diff > RERENDER_INTERVAL_IDLE) {
        rerender = true;
    }

    // exchange() so a request that lands between the test and the clear is not lost.
    if (rerender.exchange(false)) {
        lastRender = now;
        applyTheme();
        if (controller->isErrorState()) {
            changeScreen(SCREEN_ID_STANDBY_SCREEN);
        }
        updateTempStableFlag();

        updateState();
        // Fill the EEZ data models before handleScreenChange() creates/ticks a screen (undefined fields abort the flow).
        updateSystemStatus();
        updateProfileInfo();
        updateBoiler();
        updateBrewProcess();
        currentWeight = FloatValue(bluetoothWeight);
        eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_SCALE_WEIGHT_CURRENT, currentWeight);

        char timeBuf[12];
        formatDuration(controller->getSettings().getTargetGrindDuration(), timeBuf, sizeof(timeBuf));
        if (stringChanged(grindTimeTarget.getString(), timeBuf)) {
            grindTimeTarget = StringValue(timeBuf);
            eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_GRIND_TIME_TARGET, grindTimeTarget);
        }
        grindWeightTarget = FloatValue(controller->getSettings().getTargetGrindVolume());
        eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_GRIND_WEIGHT_TARGET, grindWeightTarget);

        if (standbyReleasePending) {
            standbyReleasePending = false;
            // The press started the exit; if the release did not turn into a wake (guards in
            // action_on_wakeup, or the finger slid off), bring the standby screen back.
            if (targetScreen == SCREEN_ID_STANDBY_SCREEN && standbyFade != StandbyFade::Resting) {
                cancelStandbyExit();
            }
        }
        handleScreenChange();
        currentScreen = static_cast<ScreensEnum>(eez_flow_get_current_screen());
        effect_mgr.evaluate_all();

        if (currentScreen == SCREEN_ID_STANDBY_SCREEN && standbyEnterTime > 0) {
            const Settings &settings = controller->getSettings();
            if (now - standbyEnterTime >= settings.getStandbyBrightnessTimeout()) {
                // Dim once. handleScreenChange() restores the main brightness on the way out and
                // re-arms the timer on the next entry, so nothing needs re-sending per render.
                setBrightness(settings.getStandbyBrightness());
                standbyEnterTime = 0;
            }
        }
    }

    ui_tick();
    lv_task_handler();
    pollProfileDial(); // after the handler so it sees this tick's pointer state
}

// --- Profile dial ---------------------------------------------------------------------------

float DefaultUI::profileDialDetent() {
    std::lock_guard<std::mutex> guard(profilesMutex);
    const size_t n = favoritedProfileIds.empty() ? 1 : favoritedProfileIds.size();
    const float perLap = 360.f / static_cast<float>(n);
    return std::min(DIAL_DETENT_MAX_DEG, std::max(DIAL_DETENT_MIN_DEG, perLap));
}

void DefaultUI::pollProfileDial() {
    lv_indev_t *indev = lv_indev_get_next(nullptr);
    if (indev == nullptr) {
        return;
    }
    const bool down = indev->proc.state == LV_INDEV_STATE_PRESSED && currentScreen == SCREEN_ID_PROFILE_SCREEN;
    if (!down) {
        profileDial.pressed = false;
        profileDial.active = false;
        return;
    }
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    const float dx = static_cast<float>(p.x) - lv_disp_get_hor_res(nullptr) / 2.f;
    const float dy = static_cast<float>(p.y) - lv_disp_get_ver_res(nullptr) / 2.f;
    const float angle = atan2f(dy, dx) * 180.f / static_cast<float>(M_PI);

    if (!profileDial.pressed) { // first tick of this press: decide whether it is a dial press
        profileDial.pressed = true;
        profileDial.active = dx * dx + dy * dy >= DIAL_INNER_RADIUS * DIAL_INNER_RADIUS;
        profileDial.lastAngle = angle;
        profileDial.accumulated = 0.f;
        return;
    }
    if (!profileDial.active) {
        return;
    }
    float delta = angle - profileDial.lastAngle;
    if (delta > 180.f) { // unwrap across the +/-180 seam
        delta -= 360.f;
    } else if (delta < -180.f) {
        delta += 360.f;
    }
    profileDial.lastAngle = angle;
    profileDial.accumulated += delta;

    const float detent = profileDialDetent();
    while (profileDial.accumulated >= detent) {
        profileDial.accumulated -= detent;
        onNextProfile();
    }
    while (profileDial.accumulated <= -detent) {
        profileDial.accumulated += detent;
        onPreviousProfile();
    }
}

void DefaultUI::loopProfiles() {
    // Claim the reload before building, so a reloadProfiles() that lands mid-rebuild clears the
    // flag again and is picked up by the next wake instead of being overwritten by "loaded" at the end.
    if (profileLoaded.exchange(1) == 0) {
        // Build into locals and swap under the lock — the UI task reads these concurrently (GM-147).
        const auto favoritedIds = profileManager->getFavoritedProfiles();
        std::vector<String> ids;
        ids.reserve(favoritedIds.size() + 1);
        ids.emplace_back(controller->getSettings().getSelectedProfile());
        for (const auto &id : favoritedIds) {
            if (std::find(ids.begin(), ids.end(), id) == ids.end())
                ids.emplace_back(id);
        }
        std::vector<Profile> profiles;
        profiles.reserve(ids.size());
        for (const auto &profileId : ids) {
            Profile profile{};
            profileManager->loadProfile(profileId, profile);
            profiles.emplace_back(std::move(profile));
        }
        {
            std::lock_guard<std::mutex> guard(profilesMutex);
            favoritedProfileIds = std::move(ids);
            favoritedProfiles = std::move(profiles);
        }
    }
}

void DefaultUI::changeScreen(ScreensEnum screen) {
    targetScreen = screen;
    brewScreenState = BrewScreenState::Brew;
    rerender = true;
    // Reset some submenus
}

void DefaultUI::changeBrewScreenMode(BrewScreenState state) {
    brewScreenState = state;
    rerender = true;
}

void DefaultUI::onProfileSwitch() {
    currentProfileIdx = 0;
    changeScreen(SCREEN_ID_PROFILE_SCREEN);
}

void DefaultUI::onNextProfile() {
    std::lock_guard<std::mutex> guard(profilesMutex);
    if (currentProfileIdx + 1 < static_cast<int>(favoritedProfileIds.size())) {
        currentProfileIdx++;
    }
    rerender = true;
}

void DefaultUI::onPreviousProfile() {
    if (currentProfileIdx > 0) {
        currentProfileIdx--;
    }
    rerender = true;
}

void DefaultUI::onProfileSelect() {
    String id;
    {
        std::lock_guard<std::mutex> guard(profilesMutex);
        if (currentProfileIdx >= 0 && currentProfileIdx < static_cast<int>(favoritedProfileIds.size())) {
            id = favoritedProfileIds[currentProfileIdx];
        }
    }
    if (!id.isEmpty()) {
        profileManager->selectProfile(id);
    }
    profileDirty = false;
    changeScreen(SCREEN_ID_BREW_SCREEN);
}

void DefaultUI::onVolumetricDelete() {
    controller->onVolumetricDelete();
    profileDirty = true;
}

void DefaultUI::setupPanel() {
    ui_init();
    attachStandbyPressHandler(); // screens are created once by ui_init(), so the pointers are stable
    setupState();
    applyTheme();
    ui_tick();

    // Polished power-up: ui_init() makes standby active instantly, so stage a black screen and
    // fade standby in over it (lv_scr_load_anim no-ops when the target is already the active screen).
    lv_obj_t *standby = lv_scr_act();
    lv_obj_t *black = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(black, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(black, LV_OPA_COVER, LV_PART_MAIN);
    lv_scr_load(black);
    lv_scr_load_anim(standby, LV_SCR_LOAD_ANIM_FADE_ON, STARTUP_FADE_MS, 0, true);

    lv_task_handler();

    delay(100);
    // Set initial brightness based on settings
    const ::Settings &settings = controller->getSettings();
    setBrightness(settings.getMainBrightness());
}

void DefaultUI::setupState() {
    eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_SCALE_WEIGHT_CURRENT, currentWeight);
    eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_GRIND_WEIGHT_TARGET, grindWeightTarget);
    eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_GRIND_TIME_TARGET, grindTimeTarget);

    eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_SYSTEM, systemStatus);
    eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_PREVIEW_PROFILE, previewProfileInfo);
    eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_SELECTED_PROFILE, selectedProfileInfo);
    eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_BOILER, boiler);
    eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_UI_FLAGS, uiFlags);
    eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_BREW_PROCESS_INFO, brewProcess);

    updateState();
    updateSystemStatus();
    updateProfileInfo();
    updateBoiler();
    updateBrewProcess();

    effect_mgr.use_effect([this]() { return currentScreen == SCREEN_ID_INFO_SCREEN; },
                          [=, this]() {
                              String content = "";
                              if (apActiveUi) {
                                  // WIFI: QR syntax — escape \ ; , : " in the password per the spec.
                                  const String pw = controller->getSettings().getWifiApPassword();
                                  String escaped;
                                  escaped.reserve(pw.length() + 4);
                                  for (size_t i = 0; i < pw.length(); i++) {
                                      const char c = pw.charAt(i);
                                      if (c == '\\' || c == ';' || c == ',' || c == ':' || c == '"') {
                                          escaped += '\\';
                                      }
                                      escaped += c;
                                  }
                                  if (escaped.isEmpty()) {
                                      content = "WIFI:S:" WIFI_AP_SSID ";;;;";
                                  } else {
                                      content = "WIFI:S:" WIFI_AP_SSID ";T:WPA;P:" + escaped + ";;";
                                  }
                              } else if (wifiConnected) {
                                  content = "http://" + WiFi.localIP().toString() + "/";
                              }
                              if (content == "") {
                                  return;
                              }
                              const char *data = content.c_str();
                              lv_qrcode_update(objects.qrcode, data, strlen(data));
                          },
                          &wifiConnected, &apActiveUi); // the effect copies its deps, which an atomic forbids
    effect_mgr.use_effect([this]() { return currentScreen == SCREEN_ID_MENU_SCREEN_NEW; },
                          [this]() {
                              int radius = 135;
                              int count = grindAvailable ? 4 : 3;
                              int step = 360 / (grindAvailable ? 4 : 3);
                              int rotationOffset = count == 4 ? 45 : 0;
                              positionMenuIcon(objects.btn_brew_1, step * 0 - rotationOffset, radius);
                              positionMenuIcon(objects.btn_steam_1, step * 1 - rotationOffset, radius);
                              positionMenuIcon(objects.btn_water_1, step * 2 - rotationOffset, radius);
                              positionMenuIcon(objects.btn_grind_1, step * 3 - rotationOffset, radius);
                          },
                          &grindAvailable);
}

void DefaultUI::handleScreenChange() {
    const ScreensEnum target = targetScreen; // one snapshot: changeScreen() may race us from another task
    if (currentScreen == target) {
        return;
    }
    if (target == SCREEN_ID_STANDBY_SCREEN) {
        standbyEnterTime = ::millis();
    } else if (currentScreen == SCREEN_ID_STANDBY_SCREEN) {
        // Hold the switch until the exit transition has played out. A tap started it on
        // press; programmatic exits (BLE connect, OTA end, ...) start it here.
        if (standbyFade != StandbyFade::Exited) {
            if (standbyFade != StandbyFade::ExitRunning) {
                beginStandbyExit();
            }
            return;
        }
        standbyPressArmed = false;
        const ::Settings &settings = controller->getSettings();
        setBrightness(settings.getMainBrightness());
    }
    const ScreensEnum from = currentScreen;
    eez_flow_set_screen(target, LV_SCR_LOAD_ANIM_NONE, 0, 0);
    animateGaugeTicks(from, target);
    if (target == SCREEN_ID_STANDBY_SCREEN && from != SCREEN_ID_STANDBY_SCREEN) {
        beginStandbyEnter();
    }
    rerender = true;
}

// --- Standby transition ---------------------------------------------------------------------

void DefaultUI::attachStandbyPressHandler() {
    if (objects.standby_screen != nullptr) {
        lv_obj_add_event_cb(objects.standby_screen, standbyPressCb, LV_EVENT_ALL, this);
    }
}

// Same conditions under which action_on_wakeup() (eez/actions.cpp) will actually leave standby.
bool DefaultUI::wakeAllowed() const {
    return !controller->isUpdating() && !controller->isErrorState() && !controller->isAutotuning() &&
           controller->getClientController()->isConnected();
}

void DefaultUI::standbyPressCb(lv_event_t *e) {
    auto *ui = static_cast<DefaultUI *>(lv_event_get_user_data(e));
    switch (lv_event_get_code(e)) {
    case LV_EVENT_PRESSED:
        // The generated handler wakes on CLICKED, i.e. on release. Starting here lets the
        // 150 ms run while the finger is still down, so the Brew screen is ready by the time
        // it lifts instead of 150 ms after.
        if (ui->currentScreen == SCREEN_ID_STANDBY_SCREEN && ui->targetScreen == SCREEN_ID_STANDBY_SCREEN &&
            ui->wakeAllowed()) {
            ui->standbyPressArmed = true;
            ui->beginStandbyExit();
        }
        break;
    case LV_EVENT_RELEASED:
    case LV_EVENT_PRESS_LOST:
        // LVGL sends RELEASED before CLICKED, so whether this release is a wake is only known
        // once the indev has finished dispatching. loop() looks at targetScreen afterwards.
        if (ui->standbyPressArmed) {
            ui->standbyPressArmed = false;
            ui->standbyReleasePending = true;
            ui->rerender = true;
        }
        break;
    default:
        break;
    }
}

void DefaultUI::setStandbyFade(int32_t v) {
    standbyFadeValue = v;
    standbyFadeFrames++;
    const auto opa = static_cast<lv_opa_t>(v);
    if (objects.obj1 != nullptr) { // the wordmark
        lv_obj_set_style_img_opa(objects.obj1, opa, LV_PART_MAIN);
    }
    if (objects.touch_icon != nullptr) {
        lv_obj_set_style_img_opa(objects.touch_icon, static_cast<lv_opa_t>(STANDBY_CHEVRON_OPA * v / 255), LV_PART_MAIN);
    }
    for (lv_obj_t *icon : {objects.wifi_icon, objects.bluetooth_icon, objects.update_icon}) {
        if (icon != nullptr) {
            lv_obj_set_style_img_opa(icon, opa, LV_PART_MAIN);
        }
    }
    // Per-part opacity, not the object-level `opa` style: that one renders through an
    // intermediate layer, which is exactly the per-frame cost this is designed to avoid.
    for (lv_obj_t *label : {objects.time, objects.status}) {
        if (label != nullptr) {
            lv_obj_set_style_text_opa(label, opa, LV_PART_MAIN);
        }
    }
}

void DefaultUI::standbyFadeAnimCb(void *var, int32_t v) { static_cast<DefaultUI *>(var)->setStandbyFade(v); }

void DefaultUI::standbyFadeReadyCb(lv_anim_t *a) {
    auto *ui = static_cast<DefaultUI *>(a->var);
    if (ui->standbyFade == StandbyFade::ExitRunning) {
        ui->standbyFade = StandbyFade::Exited;
    } else if (ui->standbyFade == StandbyFade::EnterRunning) {
        ui->standbyFade = StandbyFade::Resting;
    }
    // How many frames the transition really got is the honest measure of its smoothness on
    // the device; the sim cannot tell us that.
    ESP_LOGI("DefaultUI", "standby %s: %u frames in %lu ms", ui->standbyFade == StandbyFade::Exited ? "exit" : "enter",
             ui->standbyFadeFrames, ::millis() - ui->standbyFadeStart);
    ui->rerender = true; // handleScreenChange() only runs under rerender; let it see the new state
}

void DefaultUI::runStandbyFade(int32_t to, uint32_t ms, lv_anim_path_cb_t path) {
    lv_anim_del(this, standbyFadeAnimCb);
    standbyFadeFrames = 0;
    standbyFadeStart = ::millis();
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, this);
    lv_anim_set_exec_cb(&a, standbyFadeAnimCb);
    lv_anim_set_values(&a, standbyFadeValue, to);
    lv_anim_set_time(&a, ms);
    lv_anim_set_path_cb(&a, path);
    lv_anim_set_ready_cb(&a, standbyFadeReadyCb);
    lv_anim_start(&a);
}

void DefaultUI::beginStandbyExit() {
    if (standbyFade == StandbyFade::ExitRunning || standbyFade == StandbyFade::Exited) {
        return;
    }
    standbyFade = StandbyFade::ExitRunning;
    runStandbyFade(0, STANDBY_EXIT_MS, lv_anim_path_ease_in); // accelerate out
}

void DefaultUI::cancelStandbyExit() {
    standbyFade = StandbyFade::EnterRunning;
    runStandbyFade(255, STANDBY_CANCEL_MS, lv_anim_path_ease_out);
}

void DefaultUI::beginStandbyEnter() {
    standbyFade = StandbyFade::EnterRunning;
    setStandbyFade(0);
    runStandbyFade(255, STANDBY_ENTER_MS, lv_anim_path_ease_out); // decelerate in
}

// Collect every lv_meter under obj (the dial gauges) so their tick length can be animated together.
void DefaultUI::collectMeters(lv_obj_t *obj) {
    const uint32_t n = lv_obj_get_child_cnt(obj);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *child = lv_obj_get_child(obj, i);
        if (gaugeCount < 4 && lv_obj_check_type(child, &lv_meter_class)) {
            gaugeMeters[gaugeCount++] = child;
        }
        collectMeters(child);
    }
}

void DefaultUI::setGaugeTickLength(int32_t len) {
    for (uint8_t i = 0; i < gaugeCount; i++) {
        auto *meter = reinterpret_cast<lv_meter_t *>(gaugeMeters[i]);
        auto *scale = static_cast<lv_meter_scale_t *>(_lv_ll_get_head(&meter->scale_ll));
        if (scale != nullptr) {
            scale->tick_length = static_cast<uint16_t>(len);
        }
        lv_obj_invalidate(gaugeMeters[i]);
    }
}

void DefaultUI::gaugeTickAnimCb(void *var, int32_t v) { static_cast<DefaultUI *>(var)->setGaugeTickLength(v); }

void DefaultUI::animateGaugeTicks(ScreensEnum from, ScreensEnum to) {
    const int32_t fromLen = isShortTickScreen(from) ? GAUGE_TICK_SHORT : GAUGE_TICK_LONG;
    const int32_t toLen = isShortTickScreen(to) ? GAUGE_TICK_SHORT : GAUGE_TICK_LONG;

    lv_anim_del(this, gaugeTickAnimCb); // cancel any in-flight tick animation
    gaugeCount = 0;
    collectMeters(lv_scr_act());
    if (gaugeCount == 0) {
        return;
    }
    // Start at the previous screen's length so the ticks morph continuously in both directions.
    setGaugeTickLength(fromLen);
    if (fromLen == toLen) {
        return;
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, this);
    lv_anim_set_exec_cb(&a, gaugeTickAnimCb);
    lv_anim_set_values(&a, fromLen, toLen);
    lv_anim_set_time(&a, GAUGE_TICK_ANIM_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

void DefaultUI::positionMenuIcon(lv_obj_t *obj, int angle, int radius) {
    int x = sin(angle * M_PI / 180) * radius;
    int y = -1 * cos(angle * M_PI / 180) * radius;
    lv_obj_set_pos(obj, x, y);
}

void DefaultUI::updateState() {
    const auto &settings = controller->getSettings();
    mode = controller->getMode();
    currentTemp = static_cast<int>(controller->getCurrentTemp());
    targetTemp = static_cast<int>(controller->getTargetTemp());
    pressureAvailable = controller->getSystemInfo().capabilities.pressure;
    wifiConnected = WiFi.status() == WL_CONNECTED;
    apActiveUi = apActive;
    grindAvailable = settings.isSmartGrindActive() || settings.getAltRelayFunction() == ALT_RELAY_GRIND;

    uiFlags.brew_adjustments(brewScreenState == BrewScreenState::Settings);
    uiFlags.active(controller->isActive());
    uiFlags.grind_active(controller->isGrindActive());
    uiFlags.grind_volumetric(controller->isVolumetricAvailable() && settings.isVolumetricTarget());
    uiFlags.heating_flash(heatingFlash);
    uiFlags.temperature_stable(isTemperatureStable);
    uiFlags.has_prev_profile(currentProfileIdx > 0);
    {
        std::lock_guard<std::mutex> guard(profilesMutex);
        uiFlags.has_next_profile(currentProfileIdx + 1 < static_cast<int>(favoritedProfileIds.size()));
    }
}

void DefaultUI::updateSystemStatus() {
    const auto &settings = controller->getSettings();
    const SystemInfo info = controller->getSystemInfo(); // returned by value; one copy for the whole render
    systemStatus.bluetooth(controller->getClientController()->isConnected());
    systemStatus.wifi(!apActive && WiFi.status() == WL_CONNECTED);
    bool error = !initialized || waitingForController || controller->isErrorState() || controller->isUpdating() ||
                 controller->isAutotuning() || info.protocolMismatch || !controller->isReady();
    systemStatus.error(error);
    const char *errorLabel = error ? getErrorMessage() : "";
    if (stringChanged(systemStatus.error_label(), errorLabel))
        systemStatus.error_label(errorLabel);
    systemStatus.volumetric_available(controller->isVolumetricAvailable());
    systemStatus.bluetooth_scales(controller->isBluetoothScaleHealthy());
    if (stringChanged(systemStatus.controller_version(), info.version.c_str()))
        systemStatus.controller_version(info.version.c_str());
    if (stringChanged(systemStatus.display_version(), BUILD_GIT_VERSION))
        systemStatus.display_version(BUILD_GIT_VERSION);
    systemStatus.update_available(updateAvailable);
    systemStatus.in_menu(currentScreen == SCREEN_ID_MENU_SCREEN_NEW);
    systemStatus.pressure_available(pressureAvailable);
    systemStatus.grind_available(grindAvailable);
    systemStatus.mode(mode);
    char ipBuf[16] = "4.4.4.1"; // the AP's own address
    if (!apActive) {
        const IPAddress ip = WiFi.localIP();
        snprintf(ipBuf, sizeof(ipBuf), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    }
    if (stringChanged(systemStatus.ip(), ipBuf))
        systemStatus.ip(ipBuf);
    const String network =
        apActive ? String(WIFI_AP_SSID) : systemStatus.wifi() ? settings.getWifiSsid() : String("Disconnected");
    if (stringChanged(systemStatus.network(), network.c_str()))
        systemStatus.network(network.c_str());
    systemStatus.ap_active(apActive);

    // Read the clock directly rather than through getLocalTime(): that helper sleeps 10 ms per
    // call while the year is still 1970, i.e. every render blocked the UI task until NTP had synced.
    char timeBuf[12] = "";
    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    if (timeinfo.tm_year > (2016 - 1900)) {
        strftime(timeBuf, sizeof(timeBuf), settings.isClock24hFormat() ? "%H:%M" : "%I:%M %p", &timeinfo);
        if (!settings.isClock24hFormat() && timeBuf[0] == '0')
            timeBuf[0] = ' ';
    }
    if (stringChanged(systemStatus.time(), timeBuf))
        systemStatus.time(timeBuf);
}

static void populateProfileInfo(ProfileInfoValue &info, const Profile &profile, bool isCurrent) {
    char timeBuf[12];
    formatDuration(static_cast<unsigned long>(profile.getTotalDuration() * 1000.0f), timeBuf, sizeof(timeBuf));
    if (stringChanged(info.name(), profile.label.c_str()))
        info.name(profile.label.c_str());
    info.temperature(profile.temperature);
    if (stringChanged(info.time(), timeBuf))
        info.time(timeBuf);
    info.phases(static_cast<int>(profile.getPhaseCount()));
    info.steps(static_cast<int>(profile.phases.size()));
    info.is_volumetric(profile.isVolumetric());
    info.is_current(isCurrent);
    info.target_weight(profile.getTotalVolume());
}

void DefaultUI::updateProfileInfo() {
    if (!initialized) {
        return;
    }
    populateProfileInfo(selectedProfileInfo, profileManager->getSelectedProfile(), true);
    selectedProfileInfo.dirty(profileDirty);

    // Preview backs the ProfileScreen carousel (index 0 = selected); hold the lock while
    // reading the vector — the profile task rebuilds it concurrently (GM-147).
    bool populated = false;
    {
        std::lock_guard<std::mutex> guard(profilesMutex);
        if (!favoritedProfiles.empty() && currentProfileIdx >= 0 &&
            currentProfileIdx < static_cast<int>(favoritedProfiles.size())) {
            populateProfileInfo(previewProfileInfo, favoritedProfiles[currentProfileIdx], currentProfileIdx == 0);
            populated = true;
        }
    }
    if (!populated) {
        populateProfileInfo(previewProfileInfo, profileManager->getSelectedProfile(), true);
    }
}

void DefaultUI::updateBoiler() {
    const ::Settings &settings = controller->getSettings();
    boiler.current_temperature(controller->getCurrentTemp());
    boiler.target_temperature(controller->getTargetTemp());
    boiler.current_pressure(pressure);
    boiler.target_pressure(controller->getTargetPressure());
    boiler.max_temperature(160.0f);
    boiler.max_pressure(settings.getPressureScaling());
}

// Mirror the live BrewProcess into brew_process_info; every field must stay valid/typed or the StatusScreen flow aborts.
void DefaultUI::updateBrewProcess() {
    if (!initialized) {
        return;
    }

    const Profile &selected = profileManager->getSelectedProfile();
    char buf[12];

    // Profile-derived defaults so the struct is valid even before a process runs.
    formatDuration(static_cast<unsigned long>(selected.getTotalDuration() * 1000.0f), buf, sizeof(buf));
    brewProcess.profile_temperature(selected.temperature);
    if (stringChanged(brewProcess.profile_time(), buf))
        brewProcess.profile_time(buf);
    brewProcess.profile_phases(static_cast<int>(selected.getPhaseCount()));
    brewProcess.profile_steps(static_cast<int>(selected.phases.size()));
    brewProcess.profile_is_volumetric(selected.isVolumetric());
    brewProcess.profile_is_current(true);
    brewProcess.profile_target_weight(selected.getTotalVolume());
    brewProcess.boiler_target_temperature(controller->getTargetTemp());

    // Hold the process lock across every deref below — the logic/AsyncTCP/BLE tasks delete
    // the process at any time (GM-147).
    std::lock_guard<std::recursive_mutex> guard(controller->getProcessLock());
    Process *process = controller->getProcess();
    if (process == nullptr) {
        process = controller->getLastProcess();
    }
    const bool validBrew = process != nullptr && process->getType() == MODE_BREW;
    if (!validBrew) {
        if (stringChanged(brewProcess.phase_type(), ""))
            brewProcess.phase_type("");
        if (stringChanged(brewProcess.phase_name(), ""))
            brewProcess.phase_name("");
        brewProcess.phase_value_current(0.0f);
        brewProcess.phase_value_target(0.0f);
        brewProcess.phase_value_is_weight(false);
        if (stringChanged(brewProcess.elapsed_time(), "0:00"))
            brewProcess.elapsed_time("0:00");
        brewProcess.elapsed_percentage(0.0f);
        brewProcess.is_complete(false);
        return;
    }

    auto *bp = static_cast<BrewProcess *>(process);
    if (bp->profile.phases.empty() || bp->phaseIndex >= bp->profile.phases.size()) {
        // Object is mid-mutation/invalid: keep the last valid values.
        return;
    }

    const Phase phase = bp->currentPhase;
    const bool active = process->isActive();

    // Live profile fields from the running process.
    formatDuration(bp->getTotalDuration(), buf, sizeof(buf));
    brewProcess.profile_temperature(bp->profile.temperature);
    if (stringChanged(brewProcess.profile_time(), buf))
        brewProcess.profile_time(buf);
    brewProcess.profile_phases(static_cast<int>(bp->profile.getPhaseCount()));
    brewProcess.profile_steps(static_cast<int>(bp->profile.phases.size()));
    brewProcess.profile_is_volumetric(bp->target == ProcessTarget::VOLUMETRIC);
    brewProcess.profile_target_weight(bp->getBrewVolume());
    brewProcess.boiler_target_temperature(bp->getTemperature());
    brewProcess.current_volume(bp->currentVolume);

    const char *phaseType = phase.phase == PhaseType::PHASE_TYPE_BREW ? "BREW" : "INFUSION";
    if (stringChanged(brewProcess.phase_type(), phaseType))
        brewProcess.phase_type(phaseType);

    String phaseName = "Finished";
    if (active) {
        phaseName = phase.name;
    } else if (controller->getSettings().isDelayAdjust() && !process->isComplete()) {
        phaseName = "Calibrating...";
    }
    if (stringChanged(brewProcess.phase_name(), phaseName.c_str()))
        brewProcess.phase_name(phaseName.c_str());

    unsigned long now = ::millis();
    if (!active && bp->finished > 0) {
        now = bp->finished;
    }
    const unsigned long elapsedMs = (bp->processStarted > 0 && now >= bp->processStarted) ? now - bp->processStarted : 0;
    formatDuration(elapsedMs, buf, sizeof(buf));
    if (stringChanged(brewProcess.elapsed_time(), buf))
        brewProcess.elapsed_time(buf);

    const bool weightTarget = bp->target == ProcessTarget::VOLUMETRIC && phase.hasVolumetricTarget();
    brewProcess.phase_value_is_weight(weightTarget);
    if (weightTarget) {
        const float target = phase.getVolumetricTarget().value;
        const float current = static_cast<float>(bp->currentVolume);
        brewProcess.phase_value_current(current);
        brewProcess.phase_value_target(target);
        brewProcess.elapsed_percentage(target > 0.0f ? clampPercentage(current / target * 100.0f) : 0.0f);
    } else {
        const unsigned long phaseElapsed =
            (bp->currentPhaseStarted > 0 && now >= bp->currentPhaseStarted) ? now - bp->currentPhaseStarted : 0;
        const float current = phaseElapsed / 1000.0f;
        const float target = bp->getPhaseDuration() / 1000.0f;
        brewProcess.phase_value_current(current);
        brewProcess.phase_value_target(target);
        brewProcess.elapsed_percentage(target > 0.0f ? clampPercentage(current / target * 100.0f) : 0.0f);
    }

    brewProcess.is_complete(process->isComplete());
}

const char *DefaultUI::getErrorMessage() {
    if (controller->isUpdating()) {
        return "Updating...";
    }
    if (controller->isAutotuning()) {
        return "Autotuning...";
    }
    if (controller->getSystemInfo().protocolMismatch) {
        return controller->getSystemInfo().protocolVersion > gm_proto::PROTOCOL_VERSION ? "Version mismatch, update display"
                                                                                        : "Version mismatch, update controller";
    }
    if (controller->isErrorState()) {
        switch (controller->getError()) {
        case ERROR_CODE_RUNAWAY:
            return "Temperature error, restart...";
        default:
            return "Unknown error";
        }
    }
    if (waitingForController) {
        return "Waiting for controller...";
    }
    return initialized ? "" : "Starting...";
}

void DefaultUI::applyTheme() {
    const ::Settings &settings = controller->getSettings();
    int newThemeMode = settings.getThemeMode();
#ifndef GAGGIMATE_SIM // Amoled-specific black theme override is device-only
    if (newThemeMode == 0 && panelDriver == AmoledDisplayDriver::getInstance()) {
        newThemeMode = THEME_ID_AMOLED_DARK;
    }
#endif

    if (newThemeMode != currentThemeMode) {
        currentThemeMode = newThemeMode;
        change_color_theme(currentThemeMode);
    }
}

void DefaultUI::loopTask(void *arg) {
    auto *ui = static_cast<DefaultUI *>(arg);
    TickType_t lastWake = xTaskGetTickCount();
    while (true) {
        ui->loop();
        // Sleep only the remainder of the period. A fixed 25 ms vTaskDelay after loop() made
        // the frame period 25 ms plus the render time -- ~43 ms on the panel while animating
        // (measured 5 frames in 215 ms over serial) -- and uneven. With the period fixed, the UI
        // runs at a steady 40 Hz whenever a frame fits, and simply back-to-back when it does not.
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(UI_LOOP_PERIOD_MS));
    }
}

void DefaultUI::profileLoopTask(void *arg) {
    auto *ui = static_cast<DefaultUI *>(arg);
    ui->loopProfiles(); // initial load
    while (true) {
        // Sleep until reloadProfiles() notifies us instead of polling the flag every 25 ms. A
        // notification given before the task first waits is counted, not lost, so early reloads are safe.
#ifdef GAGGIMATE_SIM
        vTaskDelay(pdMS_TO_TICKS(UI_LOOP_PERIOD_MS)); // never runs: the sim drives loopProfiles() from its main loop
#else
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
#endif
        ui->loopProfiles();
    }
}
