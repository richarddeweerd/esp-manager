#include "EspManager.h"

#include <cstdio>
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

DeviceStatus EspManager::status() const
{
    WifiDriver& wifi = platform_.wifi;
    DeviceStatus status;
    status.connection = connection_.state();
    status.portal = portal_.state();
    status.ssid = connection_.ssid();
    if (status.connection == ConnectionState::Connected)
    {
        status.ip = wifi.localIp();
        status.gateway = wifi.gatewayIp();
        status.rssi = wifi.rssi();
        status.channel = wifi.channel();
    }
    status.hostname = hostname();
    const MacAddress mac = wifi.macAddress();
    char text[18];
    std::snprintf(text, sizeof(text), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2],
                  mac[3], mac[4], mac[5]);
    status.mac = text;
    status.uptimeMs = platform_.clock.nowMs();
    status.freeHeap = platform_.system.freeHeap();
    status.chip = platform_.system.chipModel();
    status.firmwareVersion = firmwareVersion_;
    status.libraryVersion = ESPMGR_VERSION;
    return status;
}

void EspManager::setFirmwareVersion(const std::string& version)
{
    firmwareVersion_ = version;
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

bool EspManager::startScan()
{
    return scanner_.start();
}

bool EspManager::scanComplete() const
{
    return !scanner_.busy();
}

std::vector<ScanResult> EspManager::scanResults() const
{
    return NetworkScanner::networkList(scanner_.results(), portal_.minSignal());
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
