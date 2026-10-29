#include "EspManager.h"

#include <utility>

namespace espmgr
{

// The default constructor is in platform/esp32/Esp32Platform.cpp.
EspManager::EspManager(Platform& platform) : EspManager(nullptr, platform)
{
}

EspManager::EspManager(std::shared_ptr<Esp32Platform> owned, Platform& platform)
    : ownedPlatform_(std::move(owned)), platform_(platform)
{
}

void EspManager::begin()
{
    log_.info("core", "EspManager %s starting", ESPMGR_VERSION);
    networks_.load();
    // Before anything starts the station interface, which takes the hostname with it.
    platform_.wifi.setHostname(hostname());
    log_.info("core", "hostname %s", hostname().c_str());
    begun_ = true;
    connection_.begin();
    portal_.begin();
}

void EspManager::process()
{
    // The parts do their work first; listeners then see the result. The scanner goes first, so
    // the others see a scan that has just finished.
    scanner_.process();
    connection_.process();
    portal_.process();
    events_.deliver();
}

bool EspManager::waitForConnection(uint32_t timeoutMs)
{
    const uint64_t start = platform_.clock.nowMs();
    for (;;)
    {
        process();
        if (connection_.state() == ConnectionState::Connected)
        {
            return true;
        }
        if (!connection_.started() || networks_.count() == 0 ||
            platform_.clock.nowMs() - start >= timeoutMs)
        {
            return false;
        }
        platform_.clock.delayMs(kWaitStepMs);
    }
}

void EspManager::setLogLevel(LogLevel level)
{
    log_.setLevel(level);
}

ConnectionState EspManager::state() const
{
    return connection_.state();
}

FailureReason EspManager::lastFailure() const
{
    return connection_.lastFailure();
}

bool EspManager::setRetryDelays(uint32_t minMs, uint32_t maxMs)
{
    return connection_.setRetryDelays(minMs, maxMs);
}

std::string EspManager::hostname() const
{
    return hostname_.empty() ? defaultDeviceName(namePrefix_, platform_.wifi.macAddress())
                             : hostname_;
}

bool EspManager::setHostname(const std::string& hostname)
{
    if (!isValidHostname(hostname))
    {
        log_.warn("core", "ignoring invalid hostname \"%s\"", hostname.c_str());
        return false;
    }
    hostname_ = hostname;
    if (begun_)
    {
        platform_.wifi.setHostname(this->hostname());
    }
    return true;
}

bool EspManager::setNamePrefix(const std::string& prefix)
{
    if (!isValidNamePrefix(prefix))
    {
        log_.warn("core", "ignoring invalid name prefix \"%s\"", prefix.c_str());
        return false;
    }
    namePrefix_ = prefix;
    if (begun_)
    {
        platform_.wifi.setHostname(hostname());
    }
    return true;
}

void EspManager::startPortal()
{
    portal_.start();
}

void EspManager::stopPortal()
{
    portal_.stop();
}

PortalState EspManager::portalState() const
{
    return portal_.state();
}

ProvisioningPortal& EspManager::portal()
{
    return portal_;
}

SavedNetworks& EspManager::networks()
{
    return networks_;
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
