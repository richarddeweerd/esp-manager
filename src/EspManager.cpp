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
    // Added before any of the Developer's listeners, so theirs already see the entry.
    events_.connected.add(
        [this](const ConnectedInfo& info)
        { history_.recordConnected(info.ssid, platform_.wifi.rssi(), info.channel); });
    events_.connectFailed.add([this](const std::string& ssid, FailureReason reason)
                              { history_.recordConnectFailed(ssid, reason); });
    events_.disconnected.add([this](FailureReason reason) { history_.recordDisconnected(reason); });
    events_.settingChanged.add(
        [this](const Setting& setting)
        {
            time_.settingChanged(setting);
            if (updates_)
            {
                updates_->settingChanged(setting);
            }
        });
}

void EspManager::begin()
{
    log_.info("core", "EspManager %s starting", ESPMGR_VERSION);
    networks_.load();
    settings_.begin();
    // Before anything starts the station interface, which takes the hostname with it.
    platform_.wifi.setHostname(hostname());
    log_.info("core", "hostname %s", hostname().c_str());
    begun_ = true;
    history_.begin();
    connection_.begin();
    time_.begin();
    if (updates_)
    {
        updates_->begin();
        updates_->addRoutes(management_);
    }
    else if (platform_.system.firmwarePending())
    {
        // Updates aren't used: accept firmware installed some other way (ArduinoOTA) at once,
        // as the platform does by itself.
        platform_.system.confirmFirmware();
    }
    portal_.begin();
    management_.begin();
}

void EspManager::process()
{
    if (restartPending_ && platform_.clock.nowMs() >= restartAtMs_)
    {
        restartPending_ = false;
        log_.info("core", "restarting");
        // A board reset while associated makes some access points refuse it for a while.
        platform_.wifi.disconnect();
        platform_.system.restart();
        return;
    }
    // The parts do their work first; listeners then see the result. The scanner goes first, so
    // the others see a scan that has just finished.
    scanner_.process();
    connection_.process();
    time_.process();
    if (updates_)
    {
        updates_->process();
    }
    if (portalAfterUpdate_ && !(updates_ && updates_->busy()))
    {
        portalAfterUpdate_ = false;
        portal_.start();
    }
    routes_.handle();
    portal_.process();
    management_.process();
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

void EspManager::setLogHandler(LogHandler handler)
{
    log_.setHandler(std::move(handler));
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
        status.dns = wifi.dnsIp();
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
    status.resetReason = platform_.system.resetReason();
    status.freeHeap = platform_.system.freeHeap();
    status.chip = platform_.system.chipModel();
    status.firmwareVersion = firmwareVersion_;
    status.libraryVersion = ESPMGR_VERSION;
    status.time.enabled = time_.enabled();
    status.time.synchronised = time_.synchronised();
    if (time_.synchronised())
    {
        status.time.epoch = time_.now();
        status.time.localTime = time_.localTime();
        status.time.server = time_.server();
    }
    status.time.zone = time_.zone();
    status.time.dst = time_.dst();
    status.time.posix = time_.timezone();
    status.time.lastSync = time_.lastSync();
    if (updates_)
    {
        updates_->writeStatus(status.update);
    }
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
    if (updates_ && updates_->busy())
    {
        // The portal's scan and channel change would break the download.
        log_.info("core", "the Provisioning Portal opens once the update is done");
        portalAfterUpdate_ = true;
        return;
    }
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

bool EspManager::networkReset()
{
    log_.info("core", "Network Reset");
    const bool erased = networks_.clear();
    if (!erased)
    {
        log_.error("core", "couldn't erase the Saved Networks");
    }
    connection_.forget();
    return erased;
}

bool EspManager::settingsReset()
{
    log_.info("core", "Settings Reset");
    return settings_.reset();
}

bool EspManager::factoryReset()
{
    log_.warn("core", "Factory Reset: erasing everything and restarting");
    const bool erased = platform_.storage.eraseAll();
    if (!erased)
    {
        log_.error("core", "couldn't erase the storage");
    }
    networks_.load();
    settings_.load();
    management_.begin();
    connection_.forget();
    restart();
    return erased;
}

void EspManager::restart()
{
    restartPending_ = true;
    restartAtMs_ = platform_.clock.nowMs() + kRestartDelayMs;
}

ManagementInterface& EspManager::management()
{
    return management_;
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

ConnectionHistory& EspManager::connectionHistory()
{
    return history_;
}

Branding& EspManager::ui()
{
    return branding_;
}

NetworkTime& EspManager::ntp()
{
    return time_;
}

#ifdef ARDUINO
// In platform/esp32/Esp32UpdateDriver.cpp; only linked when a sketch calls updates().
UpdateDriver& esp32UpdateDriver();
#endif

UpdateManager& EspManager::updates()
{
    if (!updates_)
    {
        UpdateManager::Hooks hooks{[this] { return firmwareVersion_; },
                                   [this] { return portal_.state() == PortalState::Active; },
                                   [this] { return time_.synchronised() ? time_.now() : 0; },
                                   [this] { restart(); }};
        updates_ = std::make_unique<UpdateManager>(platform_.clock, platform_.system,
                                                   platform_.storage, settings_, connection_,
                                                   events_, log_, std::move(hooks));
        UpdateDriver* driver = platform_.updates;
#ifdef ARDUINO
        if (driver == nullptr)
        {
            driver = &esp32UpdateDriver();
        }
#endif
        if (driver != nullptr)
        {
            updates_->attach(*driver);
        }
        if (begun_)
        {
            // Called first after begin(): pending firmware was confirmed there already.
            updates_->begin();
        }
    }
    return *updates_;
}

SavedNetworks& EspManager::networks()
{
    return networks_;
}

Settings& EspManager::settings()
{
    return settings_;
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

EspManager& EspManager::onSettingChanged(std::function<void(const Setting&)> listener)
{
    events_.settingChanged.add(std::move(listener));
    return *this;
}

EspManager& EspManager::onTimeSynced(std::function<void(const TimeSyncInfo&)> listener)
{
    events_.timeSynced.add(std::move(listener));
    return *this;
}

EspManager& EspManager::onTimeSyncFailed(std::function<void()> listener)
{
    events_.timeSyncFailed.add(std::move(listener));
    return *this;
}

EspManager& EspManager::onUpdateAvailable(std::function<void(const UpdateInfo&)> listener)
{
    events_.updateAvailable.add(std::move(listener));
    return *this;
}

EspManager& EspManager::onUpdateStarted(std::function<void(const UpdateInfo&)> listener)
{
    events_.updateStarted.add(std::move(listener));
    return *this;
}

EspManager& EspManager::onUpdateProgress(std::function<void(const UpdateProgress&)> listener)
{
    events_.updateProgress.add(std::move(listener));
    return *this;
}

EspManager& EspManager::onUpdateCompleted(std::function<void(const UpdateInfo&)> listener)
{
    events_.updateCompleted.add(std::move(listener));
    return *this;
}

EspManager& EspManager::onUpdateFailed(std::function<void(const UpdateError&)> listener)
{
    events_.updateFailed.add(std::move(listener));
    return *this;
}

}  // namespace espmgr
