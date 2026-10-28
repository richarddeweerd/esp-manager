#include "EspManager.h"

#include "core/Platform.h"

#include <utility>

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
    // The parts do their work first; listeners then see the result.
    events_.deliver();
}

void EspManager::setLogLevel(LogLevel level)
{
    log_.setLevel(level);
}

EspManager& EspManager::onConnected(std::function<void(const ConnectedInfo&)> listener)
{
    events_.connected.add(std::move(listener));
    return *this;
}

EspManager& EspManager::onDisconnected(std::function<void(FailureReason)> listener)
{
    events_.disconnected.add(std::move(listener));
    return *this;
}

EspManager&
EspManager::onConnectFailed(std::function<void(const std::string& ssid, FailureReason)> listener)
{
    events_.connectFailed.add(std::move(listener));
    return *this;
}

EspManager& EspManager::onConnectionStateChanged(
    std::function<void(ConnectionState from, ConnectionState to)> listener)
{
    events_.connectionStateChanged.add(std::move(listener));
    return *this;
}

EspManager&
EspManager::onPortalStarted(std::function<void(const std::string& accessPointSsid)> listener)
{
    events_.portalStarted.add(std::move(listener));
    return *this;
}

EspManager& EspManager::onPortalStopped(std::function<void()> listener)
{
    events_.portalStopped.add(std::move(listener));
    return *this;
}

EspManager&
EspManager::onProvisioningCompleted(std::function<void(const std::string& ssid)> listener)
{
    events_.provisioningCompleted.add(std::move(listener));
    return *this;
}

}  // namespace espmgr
