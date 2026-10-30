#include "platform/esp32/Esp32System.h"

#include <Arduino.h>

namespace espmgr
{

uint32_t Esp32System::freeHeap() const
{
    return ESP.getFreeHeap();
}

std::string Esp32System::chipModel() const
{
    return std::string(ESP.getChipModel()) + " rev " + std::to_string(ESP.getChipRevision());
}

void Esp32System::restart()
{
    ESP.restart();
}

}  // namespace espmgr
