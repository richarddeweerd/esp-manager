#pragma once

#include <cstdint>
#include <string>

namespace espmgr
{

// Why the chip last started (US10.2.2).
enum class ResetReason : uint8_t
{
    // Power was switched on.
    PowerOn,
    // The reset button, or a programmer resetting it over USB.
    External,
    // The firmware restarted it, for example with esp.restart().
    Software,
    // A crash (panic or exception).
    Crash,
    // A watchdog fired.
    Watchdog,
    // The supply voltage dropped too low.
    Brownout,
    // Woken from deep sleep.
    DeepSleep,
    Other,
};

// A short code for JSON and logs: "power-on", "external", "software", "crash", "watchdog",
// "brownout", "deep-sleep" or "other".
inline const char* toString(ResetReason reason)
{
    switch (reason)
    {
    case ResetReason::PowerOn:
        return "power-on";
    case ResetReason::External:
        return "external";
    case ResetReason::Software:
        return "software";
    case ResetReason::Crash:
        return "crash";
    case ResetReason::Watchdog:
        return "watchdog";
    case ResetReason::Brownout:
        return "brownout";
    case ResetReason::DeepSleep:
        return "deep-sleep";
    case ResetReason::Other:
        break;
    }
    return "other";
}

// The chip itself: what the status page reports about it, and restarting it.
class System
{
public:
    virtual ~System() = default;

    // Bytes of heap that are free now.
    virtual uint32_t freeHeap() const = 0;
    // The chip and its revision, such as "ESP32-S3 rev 0".
    virtual std::string chipModel() const = 0;

    // Why the chip last started.
    virtual ResetReason resetReason() const = 0;

    // Restarts the chip; doesn't return on the device.
    virtual void restart() = 0;
};

}  // namespace espmgr
