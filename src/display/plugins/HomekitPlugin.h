#ifndef HOMEKITPLUGIN_H
#define HOMEKITPLUGIN_H
#include "../core/Plugin.h"
#include "HomekitBridge.h"
#include <Arduino.h>

#define HOMESPAN_PORT 8080
#define DEVICE_NAME "Exhalation"

class HomekitPlugin : public Plugin {
  public:
    HomekitPlugin(String wifiSsid, String wifiPassword);
    void setup(Controller *controller, PluginManager *pluginManager) override;
    void loop() override;

    bool hasAction() const;
    void clearAction();

  private:
    String wifiSsid;
    String wifiPassword;
    HomekitBridge bridge;
    bool actionRequired = false;
    Controller *controller = nullptr;
};

#endif // HOMEKITPLUGIN_H
