#include "HomekitPlugin.h"
#include "../core/Controller.h"
#include "../core/constants.h"
#include <utility>

HomekitPlugin::HomekitPlugin(String wifiSsid, String wifiPassword)
    : wifiSsid(std::move(wifiSsid)), wifiPassword(std::move(wifiPassword)) {}

bool HomekitPlugin::hasAction() const { return actionRequired; }

void HomekitPlugin::clearAction() { actionRequired = false; }

void HomekitPlugin::setup(Controller *controller, PluginManager *pluginManager) {
    this->controller = controller;

    pluginManager->on("controller:wifi:connect", [this](Event &event) {
        int apMode = event.getInt("AP");
        if (apMode)
            return;
        if (bridge.isStarted())
            return;
        bridge.begin(DEVICE_NAME, this->controller->getSettings().getMdnsName().c_str(), wifiSsid.c_str(),
                     wifiPassword.c_str(), HOMESPAN_PORT, [this]() { this->actionRequired = true; });
    });

    pluginManager->on("boiler:targetTemperature:change", [this](Event const &event) {
        if (!bridge.isStarted())
            return;
        bridge.setTargetTemperature(event.getFloat("value"));
    });

    pluginManager->on("boiler:currentTemperature:change", [this](Event const &event) {
        if (!bridge.isStarted())
            return;
        bridge.setCurrentTemperature(event.getFloat("value"));
    });

    pluginManager->on("controller:mode:change", [this](Event const &event) {
        if (!bridge.isStarted())
            return;
        bridge.setState(event.getInt("value") != MODE_STANDBY);
    });
}

void HomekitPlugin::loop() {
    if (!actionRequired || controller == nullptr || !bridge.isStarted())
        return;
    if (bridge.getState() && controller->getMode() == MODE_STANDBY) {
        controller->deactivateStandby();
    } else if (!bridge.getState() && controller->getMode() != MODE_STANDBY) {
        controller->activateStandby();
    }
    controller->setTargetTemp(bridge.getTargetTemperature());
    actionRequired = false;
}
