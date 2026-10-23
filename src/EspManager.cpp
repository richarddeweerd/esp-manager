#include "EspManager.h"

#include "core/Platform.h"

namespace espmgr
{

// The default constructor is in platform/esp32/Esp32Platform.cpp.
EspManager::EspManager(Platform& platform) : platform_(platform)
{
}

void EspManager::begin()
{
}

void EspManager::process()
{
}

}  // namespace espmgr
