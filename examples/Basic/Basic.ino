// The smallest EspManager sketch: connect to a saved network, or start setup mode.

#include <EspManager.h>

EspManager esp;

void setup()
{
    esp.begin();
    // Optional, if the sketch needs Wi-Fi before it goes on: wait up to 10 s.
    // esp.waitForConnection(10000);
}

void loop()
{
    esp.process();
}
