#include "platform/esp32/Esp32Platform.h"

#include "EspManager.h"

namespace espmgr
{

// Lives here rather than in EspManager.cpp, so the native tests, which don't compile this
// folder, can only construct an EspManager with fakes.
EspManager::EspManager() : EspManager(std::make_shared<Esp32Platform>())
{
}

EspManager::EspManager(std::shared_ptr<Esp32Platform> owned) : EspManager(owned, owned->platform())
{
}

}  // namespace espmgr
