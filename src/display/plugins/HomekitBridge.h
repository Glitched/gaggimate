#ifndef HOMEKITBRIDGE_H
#define HOMEKITBRIDGE_H
#include <cstdint>
#include <functional>

// Thin wall between the firmware and HomeSpan. HomeSpan 2.x defines a global `class Controller` (its paired-controller
// record) that collides with ours, so HomeSpan.h may only be included from HomekitBridge.cpp, which never sees
// Controller.h. Everything the plugin needs is expressed here in plain types.
class HomekitBridge {
  public:
    using change_callback_t = std::function<void()>;

    // Starts HomeSpan and publishes the thermostat accessory. Safe to call once; later calls are ignored.
    void begin(const char *deviceName, const char *hostName, const char *wifiSsid, const char *wifiPassword, uint16_t port,
               change_callback_t onChange);
    bool isStarted() const { return started; }

    bool getState() const; // true = heating (not standby)
    void setState(bool active) const;
    void setCurrentTemperature(float temperatureValue) const;
    void setTargetTemperature(float temperatureValue) const;
    float getTargetTemperature() const;

  private:
    bool started = false;
};

#endif // HOMEKITBRIDGE_H
