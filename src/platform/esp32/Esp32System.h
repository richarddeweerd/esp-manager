#pragma once

#include "core/System.h"

namespace espmgr
{

class Esp32System : public System
{
public:
    uint32_t freeHeap() const override;
    std::string chipModel() const override;
    void restart() override;
};

}  // namespace espmgr
