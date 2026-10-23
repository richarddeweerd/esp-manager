#include "platform/esp32/Esp32Clock.h"

#include <esp_random.h>
#include <esp_timer.h>

namespace espmgr
{

uint64_t Esp32Clock::nowMs() const
{
    return static_cast<uint64_t>(esp_timer_get_time()) / 1000;
}

uint32_t Esp32Clock::random(uint32_t below)
{
    // The modulo bias is far too small to matter for backoff jitter.
    return below == 0 ? 0 : esp_random() % below;
}

}  // namespace espmgr
