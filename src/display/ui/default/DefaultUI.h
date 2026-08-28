#ifndef DEFAULTUI_H
#define DEFAULTUI_H

#include <atomic>
#include <display/core/PluginManager.h>
#include <display/core/ProfileManager.h>
#include <display/core/constants.h>
#include <display/drivers/Driver.h>
#include <display/models/profile.h>
#include <display/ui/default/eez/screens.h>
#include <display/ui/default/eez/structs.h>
#include <mutex>

class Controller;

constexpr int RERENDER_INTERVAL_IDLE = 2500;
constexpr int RERENDER_INTERVAL_ACTIVE = 100;

constexpr int TEMP_HISTORY_INTERVAL = 250;
constexpr int TEMP_HISTORY_LENGTH = 20 * 1000 / TEMP_HISTORY_INTERVAL;

int16_t calculate_angle(int set_temp, int range, int offset);

enum class BrewScreenState { Brew, Settings };

class DefaultUI {
  public:
    DefaultUI(Controller *controller, Driver *driver, PluginManager *pluginManager);

    // Default work methods
    void init();
    void loop();
    void loopProfiles();

    // Interface methods
    void changeScreen(ScreensEnum screen);

    void changeBrewScreenMode(BrewScreenState state);
    void onProfileSwitch();
    void onNextProfile();
    void onPreviousProfile();
    // True while a press that began on the profile screen's rim is held: the rim is the
    // profile dial, so the swipe handler must leave that press alone.
    bool isProfileDialActive() const { return profileDial.active; }
    void onProfileSelect();
    void setBrightness(int brightness) {
        if (panelDriver) {
            panelDriver->setBrightness(brightness);
        }
    };

    void onVolumetricDelete();

    void markDirty() { rerender = true; }
    void markProfileDirty() { profileDirty = true; }
    void markProfileClean() { profileDirty = false; }

    void applyTheme();

    bool isTaskHealthy() const {
        return is_task_healthy(eTaskGetState(taskHandle)) && is_task_healthy(eTaskGetState(profileTaskHandle));
    }

  private:
    void setupPanel();
    void setupState();

    void handleScreenChange();

    // Animate the dial meters' tick length on screen change (short on profile/new-menu, long elsewhere).
    void animateGaugeTicks(ScreensEnum from, ScreensEnum to);
    void collectMeters(lv_obj_t *obj);
    void setGaugeTickLength(int32_t len);
    static void gaugeTickAnimCb(void *var, int32_t v);
    lv_obj_t *gaugeMeters[4] = {nullptr};
    uint8_t gaugeCount = 0;
    void positionMenuIcon(lv_obj_t *obj, int angle, int radius);

    // Profile dial: dragging around the rim of the profile screen steps through the
    // favourites like a knob — clockwise is next. Polled from loop() rather than wired to
    // LVGL press events, because the screen's full-size containers swallow presses and only
    // gestures bubble up to the screen object.
    struct ProfileDial {
        bool pressed = false;   // pointer was down last tick
        bool active = false;    // this press began on the rim
        float lastAngle = 0.f;  // degrees, screen convention (y down, clockwise positive)
        float accumulated = 0.f;
    } profileDial;
    void pollProfileDial();
    float profileDialDetent();

    // Standby transition: the wordmark shrinks and fades, the clock/chevron/icons fade with it.
    // Leaving standby waits for the exit to play out; it starts on press so it runs during the
    // dwell of the tap rather than after it.
    enum class StandbyFade { Resting, ExitRunning, Exited, EnterRunning };
    StandbyFade standbyFade = StandbyFade::Resting;
    int32_t standbyFadeValue = 255;     // 255 = resting, 0 = fully exited
    bool standbyPressArmed = false;     // exit began on a press that has not yet become a wake
    bool standbyReleasePending = false; // a release happened; decide in loop() whether it woke us
    uint16_t standbyFadeFrames = 0;
    unsigned long standbyFadeStart = 0;
    void attachStandbyPressHandler();
    static void standbyPressCb(lv_event_t *e);
    bool wakeAllowed() const;
    void beginStandbyExit();
    void cancelStandbyExit();
    void beginStandbyEnter();
    void runStandbyFade(int32_t to, uint32_t ms, lv_anim_path_cb_t path);
    void setStandbyFade(int32_t v);
    static void standbyFadeAnimCb(void *var, int32_t v);
    static void standbyFadeReadyCb(lv_anim_t *a);

    void updateState();
    void updateSystemStatus();
    void updateProfileInfo();
    void updateBoiler();
    void updateBrewProcess();
    void updateMenuScreen();
    String getErrorMessage();

    void adjustDials(lv_obj_t *dials);
    void adjustTarget(lv_obj_t *obj, double percentage, double start, double range) const;

    int tempHistory[TEMP_HISTORY_LENGTH] = {0};
    int tempHistoryIndex = 0;
    int prevTargetTemp = 0;
    bool isTempHistoryInitialized = false;
    int isTemperatureStable = false;
    unsigned long lastTempLog = 0;

    void updateTempHistory();
    void updateTempStableFlag();
    void reloadProfiles();

    Driver *panelDriver = nullptr;
    Controller *controller;
    PluginManager *pluginManager;
    ProfileManager *profileManager;

    // Screen state
    int updateAvailable = false;
    int apActive = false;
    int wifiConnected = false;
    int waitingForController = false;
    int initialized = false;
    int grindAvailable = false;

    // Seasonal flags
    int christmasMode = false;

    bool rerender = false;
    unsigned long lastRender = 0;

    int mode = MODE_STANDBY;
    bool pressureAvailable = false;
    int heatingFlash = 0;
    float pressure = 0.0f;
    float currentTemp = 0.0f;
    float targetTemp = 0.0f;
    double bluetoothWeight = 0.0;
    BrewScreenState brewScreenState = BrewScreenState::Brew;

    // EEZ Structs
    SystemStatusValue systemStatus;
    ProfileInfoValue selectedProfileInfo;
    ProfileInfoValue previewProfileInfo;
    BoilerValue boiler;
    UIFlagsValue uiFlags;
    BrewProcessValue brewProcess;
    Value currentWeight = FloatValue(0.0);
    Value steamReady = BooleanValue(false);
    Value grindWeightTarget = FloatValue(18.0);
    Value grindTimeTarget = StringValue("0:15");

    int profileDirty = 0;
    int currentProfileIdx = 0;
    std::atomic<int> profileLoaded{0}; // cleared from event callbacks on arbitrary tasks
    // The profile task (core 0) rebuilds these while the UI task reads them (GM-147).
    std::mutex profilesMutex;
    std::vector<String> favoritedProfileIds;
    std::vector<Profile> favoritedProfiles;
    int currentThemeMode = -1; // Force applyTheme on first loop

    // Screen change
    ScreensEnum targetScreen = ScreensEnum::SCREEN_ID_STANDBY_SCREEN;
    ScreensEnum currentScreen = ScreensEnum::SCREEN_ID_STANDBY_SCREEN;

    // Standby brightness control
    unsigned long standbyEnterTime = 0;

    xTaskHandle taskHandle;
    static void loopTask(void *arg);
    xTaskHandle profileTaskHandle;
    static void profileLoopTask(void *arg);
};

#endif // DEFAULTUI_H
