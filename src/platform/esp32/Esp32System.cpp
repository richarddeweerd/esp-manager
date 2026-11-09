#include "platform/esp32/Esp32System.h"

#include <Arduino.h>
#include <esp_system.h>

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

ResetReason Esp32System::resetReason() const
{
    switch (esp_reset_reason())
    {
    case ESP_RST_POWERON:
        return ResetReason::PowerOn;
    case ESP_RST_EXT:
    case ESP_RST_USB:
    case ESP_RST_JTAG:
        return ResetReason::External;
    case ESP_RST_SW:
        return ResetReason::Software;
    case ESP_RST_PANIC:
        return ResetReason::Crash;
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
        return ResetReason::Watchdog;
    case ESP_RST_BROWNOUT:
        return ResetReason::Brownout;
    case ESP_RST_DEEPSLEEP:
        return ResetReason::DeepSleep;
    default:
        return ResetReason::Other;
    }
}

void Esp32System::restart()
{
    ESP.restart();
}

}  // namespace espmgr
