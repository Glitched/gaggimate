// The only translation unit that includes HomeSpan.h (see HomekitBridge.h for why).
#include "HomekitBridge.h"
#include <HomeSpan.h>

namespace {

class ThermostatService : public Service::Thermostat {
  public:
    explicit ThermostatService(HomekitBridge::change_callback_t callback) : callback(std::move(callback)) {
        state = new Characteristic::CurrentHeatingCoolingState();
        targetState = new Characteristic::TargetHeatingCoolingState();
        targetState->setValidValues(2, 0, 1);
        currentTemperature = new Characteristic::CurrentTemperature();
        currentTemperature->setRange(0, 160);
        targetTemperature = new Characteristic::TargetTemperature();
        targetTemperature->setRange(0, 160);
        displayUnits = new Characteristic::TemperatureDisplayUnits();
        displayUnits->setVal(0);
    }

    boolean update() override {
        if (targetState->getVal() != targetState->getNewVal()) {
            state->setVal(targetState->getNewVal());
            callback();
        }
        if (targetTemperature->getVal() != targetTemperature->getNewVal()) {
            callback();
        }
        return true;
    }

    bool getState() const { return targetState->getVal() == 1; }
    void setState(bool active) const {
        targetState->setVal(active ? 1 : 0, true);
        state->setVal(active ? 1 : 0, true);
    }
    void setCurrentTemperature(float v) const { currentTemperature->setVal(v, true); }
    void setTargetTemperature(float v) const { targetTemperature->setVal(v, true); }
    float getTargetTemperature() const { return targetTemperature->getVal(); }

  private:
    HomekitBridge::change_callback_t callback;
    SpanCharacteristic *state = nullptr;
    SpanCharacteristic *targetState = nullptr;
    SpanCharacteristic *currentTemperature = nullptr;
    SpanCharacteristic *targetTemperature = nullptr;
    SpanCharacteristic *displayUnits = nullptr;
};

// HomeSpan owns these for the life of the process; a single global thermostat is all the firmware exposes.
ThermostatService *thermostat = nullptr;

} // namespace

void HomekitBridge::begin(const char *deviceName, const char *hostName, const char *wifiSsid, const char *wifiPassword,
                          uint16_t port, change_callback_t onChange) {
    if (started)
        return;
    started = true;
    homeSpan.setHostNameSuffix("");
    homeSpan.setPortNum(port);
    homeSpan.begin(Category::Thermostats, deviceName, hostName);
    homeSpan.setWifiCredentials(wifiSsid, wifiPassword);
    new SpanAccessory();
    new Service::AccessoryInformation();
    new Characteristic::Identify();
    thermostat = new ThermostatService(std::move(onChange));
    homeSpan.autoPoll();
}

bool HomekitBridge::getState() const { return thermostat != nullptr && thermostat->getState(); }

void HomekitBridge::setState(bool active) const {
    if (thermostat)
        thermostat->setState(active);
}

void HomekitBridge::setCurrentTemperature(float v) const {
    if (thermostat)
        thermostat->setCurrentTemperature(v);
}

void HomekitBridge::setTargetTemperature(float v) const {
    if (thermostat)
        thermostat->setTargetTemperature(v);
}

float HomekitBridge::getTargetTemperature() const { return thermostat ? thermostat->getTargetTemperature() : 0.0f; }
