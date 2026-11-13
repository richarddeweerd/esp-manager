#pragma once

#include "core/System.h"

namespace espmgr
{

class Esp32System : public System
{
public:
    uint32_t freeHeap() const override;
    std::string chipModel() const override;
    ResetReason resetReason() const override;
    void restart() override;
    bool firmwarePending() const override;
    void confirmFirmware() override;
};

}  // namespace espmgr
