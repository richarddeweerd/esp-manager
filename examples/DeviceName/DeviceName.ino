// The device's name and the Provisioning Portal's password.
//
// The name is what the device gives the router (its hostname) and, by default, the name of the
// Provisioning Portal's access point. setNamePrefix() keeps the last six hex digits of the MAC,
// so every board gets its own name ("lamp-abcdef"); setHostname() uses one fixed name instead.
//
// The portal is open by default. With a password, the Owner's phone needs it to join the access
// point, so print it on the device or in its manual. It's 8 to 63 printable ASCII characters.

#include <Arduino.h>
#include <EspManager.h>

namespace
{

EspManager esp;

}  // namespace

void setup()
{
    Serial.begin(115200);

    if (!esp.setNamePrefix("lamp"))  // or: esp.setHostname("kitchen-lamp");
    {
        Serial.println("That name isn't valid; keeping the default.");
    }
    if (!esp.portal().setPassword("lamp-setup"))
    {
        Serial.println("That password isn't valid; the portal stays open.");
    }

    esp.onPortalStarted(
        [](const std::string& accessPointSsid)
        {
            Serial.printf("Join \"%s\" with the setup password to set up the device.\n",
                          accessPointSsid.c_str());
        });
    esp.onConnected(
        [](const espmgr::ConnectedInfo& info)
        { Serial.printf("Connected to %s as %s.\n", info.ssid.c_str(), esp.hostname().c_str()); });

    esp.begin();
    Serial.printf("This device is %s.\n", esp.hostname().c_str());
}

void loop()
{
    esp.process();
}
