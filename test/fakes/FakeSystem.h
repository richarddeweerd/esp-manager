#pragma once

#include "core/System.h"

namespace espmgr
{

// A System that reports what the test sets and counts restarts, which change nothing else.
class FakeSystem : public System
{
public:
    uint32_t freeHeap() const override
    {
        return heap;
    }

    std::string chipModel() const override
    {
        return chip;
    }

    ResetReason resetReason() const override
    {
        return reset;
    }

    void restart() override
    {
        ++restarts;
    }

    bool firmwarePending() const override
    {
        return pending;
    }

    void confirmFirmware() override
    {
        pending = false;
        ++confirms;
    }

    uint32_t heap = 180000;
    std::string chip = "ESP32-C3 rev 4";
    int restarts = 0;
    bool pending = false;
    int confirms = 0;
    ResetReason reset = ResetReason::PowerOn;
};

}  // namespace espmgr
