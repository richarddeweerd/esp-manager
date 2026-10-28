#include "platform/esp32/Esp32Platform.h"

#include "EspManager.h"

namespace espmgr
{

// Lives here rather than in EspManager.cpp, so the native tests, which don't compile this
// folder, can only construct an EspManager with fakes.
EspManager::EspManager()
    : ownedPlatform_(std::make_shared<Esp32Platform>()),
      platform_(static_cast<Esp32Platform*>(ownedPlatform_.get())->platform()),
      log_(platform_.log, platform_.clock)
{
}

}  // namespace espmgr
