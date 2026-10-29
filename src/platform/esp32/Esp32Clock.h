#pragma once

#include "core/Clock.h"

namespace espmgr
{

// Clock on the ESP32's microsecond timer and hardware random number generator.
class Esp32Clock : public Clock
{
public:
    uint64_t nowMs() const override;
    uint32_t random(uint32_t below) override;
    void delayMs(uint32_t ms) override;
};

}  // namespace espmgr
