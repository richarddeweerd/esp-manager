#pragma once

#include "core/System.h"

namespace espmgr
{

// A System that reports what the test sets.
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

    uint32_t heap = 180000;
    std::string chip = "ESP32-C3 rev 4";
};

}  // namespace espmgr
