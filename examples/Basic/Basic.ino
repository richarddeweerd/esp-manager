// The smallest EspManager sketch: connect to a saved network, or start setup mode.

#include <EspManager.h>

EspManager esp;

void setup()
{
    esp.begin();
}

void loop()
{
    esp.process();
}
