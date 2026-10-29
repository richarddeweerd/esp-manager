#include "core/ProvisioningPortal.h"

#include "web/Pages.h"

#include <algorithm>

namespace espmgr
{

namespace
{

constexpr size_t kMaxSsidLength = 32;

}  // namespace

bool ProvisioningPortal::setSsid(const std::string& ssid)
{
    if (ssid.size() > kMaxSsidLength)
    {
        log_.warn("portal", "ignoring an access point SSID of %u bytes",
                  static_cast<unsigned>(ssid.size()));
        return false;
    }
    ssid_ = ssid;
    return true;
}

std::string ProvisioningPortal::ssid() const
{
    return ssid_.empty() ? deviceName_() : ssid_;
}

bool ProvisioningPortal::setPassword(const std::string& password)
{
    if (!password.empty() && !isValidPassword(password))
    {
        log_.warn("portal", "ignoring an access point password that isn't 8 to 63 characters");
        return false;
    }
    password_ = password;
    return true;
}

void ProvisioningPortal::setTimeout(uint32_t ms)
{
    timeoutMs_ = ms;
}

void ProvisioningPortal::setRestartOnTimeout(bool restart)
{
    restartOnTimeout_ = restart;
}

void ProvisioningPortal::setOpenAfterFailingFor(uint32_t ms)
{
    openAfterFailingMs_ = ms;
}

void ProvisioningPortal::begin()
{
    begun_ = true;
    lastConnectedMs_ = clock_.nowMs();
}

void ProvisioningPortal::start()
{
    // The Developer's own request; it no longer closes by itself once connected.
    reason_ = Opened::ByRequest;
    if (state_ == PortalState::Off && !starting_)
    {
        startRequested_ = true;
    }
}

void ProvisioningPortal::stop()
{
    startRequested_ = false;
    starting_ = false;
    if (state_ == PortalState::Active)
    {
        close();
    }
}

void ProvisioningPortal::process()
{
    if (!begun_)
    {
        return;
    }
    if (connection_.state() == ConnectionState::Connected)
    {
        lastConnectedMs_ = clock_.nowMs();
    }
    if (startRequested_)
    {
        startRequested_ = false;
        open(Opened::ByRequest);
    }

    if (starting_)
    {
        if (!scanner_.busy())
        {
            bringUp(chooseChannel(networks_.list(), scanner_.results()));
        }
        return;
    }

    if (state_ == PortalState::Active)
    {
        http_.handle();
        if (timeoutMs_ > 0 && clock_.nowMs() - lastActivityMs_ >= timeoutMs_)
        {
            const bool reopen = networks_.count() == 0 && restartOnTimeout_;
            log_.info("portal", "no requests for %u s; closing%s",
                      static_cast<unsigned>(timeoutMs_ / 1000), reopen ? " and reopening" : "");
            close();
            if (reopen)
            {
                open(Opened::NoNetwork);
            }
            return;
        }
        if (reason_ == Opened::NotConnecting && connection_.state() == ConnectionState::Connected)
        {
            log_.info("portal", "connected again; closing");
            close();
            return;
        }
    }

    applyRules();
}

void ProvisioningPortal::applyRules()
{
    if (state_ != PortalState::Off || starting_)
    {
        if (networks_.count() == 0)
        {
            openedForNoNetwork_ = true;
        }
        return;
    }
    if (networks_.count() == 0)
    {
        if (!openedForNoNetwork_)
        {
            openedForNoNetwork_ = true;
            log_.info("portal", "no Saved Network; opening");
            open(Opened::NoNetwork);
        }
        return;
    }
    openedForNoNetwork_ = false;
    if (openAfterFailingMs_ > 0 && connection_.state() != ConnectionState::Connected &&
        clock_.nowMs() - lastConnectedMs_ >= openAfterFailingMs_)
    {
        log_.info("portal", "not connected for %u s; opening",
                  static_cast<unsigned>(openAfterFailingMs_ / 1000));
        open(Opened::NotConnecting);
    }
}

void ProvisioningPortal::open(Opened reason)
{
    reason_ = reason;
    starting_ = true;
    if (connection_.state() == ConnectionState::Connected)
    {
        // The access point has to use the station's channel anyway.
        bringUp(wifi_.channel());
        return;
    }
    if (!scanner_.start())
    {
        bringUp(chooseChannel(networks_.list(), scanner_.results()));
    }
}

void ProvisioningPortal::bringUp(uint8_t channel)
{
    starting_ = false;
    activeSsid_ = ssid();
    if (!wifi_.startAccessPoint(activeSsid_, password_, channel))
    {
        log_.error("portal", "access point %s didn't start", activeSsid_.c_str());
        return;
    }
    registerRoutes();
    http_.begin(kHttpPort);
    state_ = PortalState::Active;
    lastActivityMs_ = clock_.nowMs();
    log_.info("portal", "open as %s on channel %u, %s", activeSsid_.c_str(),
              static_cast<unsigned>(channel), password_.empty() ? "open" : "WPA2");
    events_.raisePortalStarted(activeSsid_);
}

void ProvisioningPortal::close()
{
    http_.stop();
    wifi_.stopAccessPoint();
    state_ = PortalState::Off;
    lastConnectedMs_ = clock_.nowMs();
    log_.info("portal", "closed");
    events_.raisePortalStopped();
}

void ProvisioningPortal::registerRoutes()
{
    if (routesRegistered_)
    {
        return;
    }
    routesRegistered_ = true;

    http_.on(HttpMethod::Get, "/",
             [this](const HttpRequest& request)
             {
                 if (accept(request))
                 {
                     web::sendPortalPage(http_);
                 }
             });
    http_.onNotFound(
        [this](const HttpRequest& request)
        {
            if (accept(request))
            {
                http_.send(404, "text/plain", "Not found");
            }
        });
}

bool ProvisioningPortal::accept(const HttpRequest& request)
{
    if (state_ != PortalState::Active)
    {
        http_.send(404, "text/plain", "Not found");
        return false;
    }
    // Anyone who reaches the portal can change the Wi-Fi settings, so it answers only on its
    // own access point, not on the network the station is connected to.
    if (request.localIp != wifi_.accessPointIp())
    {
        http_.send(403, "text/plain",
                   "The setup pages are only available on the device's own Wi-Fi network.");
        return false;
    }
    lastActivityMs_ = clock_.nowMs();
    return true;
}

uint8_t ProvisioningPortal::chooseChannel(const std::vector<SavedNetwork>& networks,
                                          const std::vector<ScanResult>& scan)
{
    const ScanResult* strongest = nullptr;
    const ScanResult* strongestSaved = nullptr;
    for (const ScanResult& result : scan)
    {
        if (result.channel == 0)
        {
            continue;
        }
        if (strongest == nullptr || result.rssi > strongest->rssi)
        {
            strongest = &result;
        }
        const bool saved =
            std::any_of(networks.begin(), networks.end(), [&result](const SavedNetwork& network)
                        { return network.ssid == result.ssid; });
        if (saved && (strongestSaved == nullptr || result.rssi > strongestSaved->rssi))
        {
            strongestSaved = &result;
        }
    }
    if (strongestSaved != nullptr)
    {
        return strongestSaved->channel;
    }
    return strongest != nullptr ? strongest->channel : kDefaultChannel;
}

bool ProvisioningPortal::isValidPassword(const std::string& password)
{
    if (password.size() < 8 || password.size() > 63)
    {
        return false;
    }
    return std::all_of(password.begin(), password.end(),
                       [](char c) { return c >= 0x20 && c <= 0x7e; });
}

}  // namespace espmgr
