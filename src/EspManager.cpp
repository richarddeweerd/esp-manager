#include "EspManager.h"

#include "core/Platform.h"

namespace espmgr
{

// The default constructor is in platform/esp32/Esp32Platform.cpp.
EspManager::EspManager(Platform& platform) : platform_(platform), log_(platform.log, platform.clock)
{
}

void EspManager::begin()
{
    log_.info("core", "EspManager %s starting", ESPMGR_VERSION);
}

void EspManager::process()
{
}

void EspManager::setLogLevel(LogLevel level)
{
    log_.setLevel(level);
}

}  // namespace espmgr
