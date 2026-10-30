// A button for setup and resets. EspManager reads no pins itself; the sketch decides what a
// button does and calls the library:
//
//   short press (under 1 s)    startPortal()    open the Provisioning Portal
//   hold for 5 s               networkReset()   forget every Saved Network
//   hold for 10 s              factoryReset()   erase everything and restart
//
// The action runs when the button is released, so holding on to 10 s never triggers the
// Network Reset on the way. The Serial monitor says which action letting go will run.
//
// The button is the board's BOOT button by default: GPIO 0 on ESP32 and ESP32-S3 boards,
// GPIO 9 on ESP32-C3 boards. It connects the pin to ground, so the pin uses its pull-up.
// Build with -D BUTTON_PIN=<n> for another one.
//
// The button is checked in loop() without delay(), so esp.process() keeps running.

#include <EspManager.h>

namespace
{

#if defined(BUTTON_PIN)
constexpr uint8_t kButtonPin = BUTTON_PIN;
#elif CONFIG_IDF_TARGET_ESP32C3
constexpr uint8_t kButtonPin = 9;
#else
constexpr uint8_t kButtonPin = 0;
#endif

constexpr uint32_t kDebounceMs = 30;
constexpr uint32_t kShortPressMaxMs = 1000;
constexpr uint32_t kNetworkResetMs = 5000;
constexpr uint32_t kFactoryResetMs = 10000;

EspManager esp;

enum class Action
{
    None,
    StartPortal,
    NetworkReset,
    FactoryReset,
};

// What letting go after `heldMs` does.
Action actionFor(uint32_t heldMs)
{
    if (heldMs >= kFactoryResetMs)
    {
        return Action::FactoryReset;
    }
    if (heldMs >= kNetworkResetMs)
    {
        return Action::NetworkReset;
    }
    // Between 1 and 5 s nothing happens, so a press that went on too long can be abandoned.
    return heldMs < kShortPressMaxMs ? Action::StartPortal : Action::None;
}

const char* describe(Action action)
{
    switch (action)
    {
    case Action::StartPortal:
        return "open the setup portal";
    case Action::NetworkReset:
        return "forget all Wi-Fi networks";
    case Action::FactoryReset:
        return "erase everything and restart";
    default:
        return "do nothing";
    }
}

// The button, debounced, polled from loop().
struct Button
{
    bool pressed = false;      // debounced state
    bool lastReading = false;  // raw state at the last poll
    uint32_t changedAtMs = 0;  // when the raw state last changed
    uint32_t pressedAtMs = 0;
    Action announced = Action::None;

    void poll()
    {
        const uint32_t now = millis();
        const bool reading = digitalRead(kButtonPin) == LOW;
        if (reading != lastReading)
        {
            lastReading = reading;
            changedAtMs = now;
        }
        if (now - changedAtMs >= kDebounceMs && reading != pressed)
        {
            pressed = reading;
            if (pressed)
            {
                pressedAtMs = now;
                announced = Action::StartPortal;
                Serial.println("Button pressed; let go now to open the setup portal");
            }
            else
            {
                released(now - pressedAtMs);
            }
        }
        if (pressed)
        {
            // Say so whenever holding on changes what letting go will do.
            const Action next = actionFor(now - pressedAtMs);
            if (next != announced)
            {
                announced = next;
                Serial.printf("Let go now to %s\n", describe(next));
            }
        }
    }

    void released(uint32_t heldMs)
    {
        const Action action = actionFor(heldMs);
        Serial.printf("Button held for %lu ms: %s\n", static_cast<unsigned long>(heldMs),
                      describe(action));
        switch (action)
        {
        case Action::StartPortal:
            esp.startPortal();
            break;
        case Action::NetworkReset:
            esp.networkReset();
            break;
        case Action::FactoryReset:
            esp.factoryReset();
            break;
        default:
            break;
        }
    }
};

Button button;

}  // namespace

void setup()
{
    Serial.begin(115200);
    pinMode(kButtonPin, INPUT_PULLUP);
    esp.onPortalStarted(
           [](const std::string& ssid)
           { Serial.printf("Join the Wi-Fi network %s to set me up\n", ssid.c_str()); })
        .onConnected([](const espmgr::ConnectedInfo& info)
                     { Serial.printf("Connected as %s\n", info.ip.toString().c_str()); });
    esp.begin();
}

void loop()
{
    button.poll();
    esp.process();
}
