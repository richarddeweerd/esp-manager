#pragma once

#include "core/Clock.h"
#include "core/Events.h"
#include "core/HttpServer.h"
#include "core/Logger.h"
#include "core/NetworkScanner.h"
#include "core/SavedNetworks.h"
#include "core/WifiConnection.h"
#include "core/WifiDriver.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace espmgr
{

// The Provisioning Portal (EP02): a temporary access point with the setup pages, where the
// Owner picks the network the device should join. Reached through esp.portal() for its
// settings; esp.startPortal() and esp.stopPortal() open and close it.
//
// It opens by itself only when there's no Saved Network, or, if the Developer asks for it, after
// the device has been unable to connect for a while. Before the access point starts, a scan picks
// its channel: that of the strongest Saved Network in range, otherwise of the strongest network
// in range, so the access point is less likely to jump channels when the station connects. It
// closes after a time without requests from the Owner.
//
// It's a captive portal: while it's open, a DNS server answers every name with the access point's
// address, and every unknown address on it redirects to the setup page. Phones check for
// internet access when they join a network (Android /generate_204, Apple /hotspot-detect.html,
// Windows /connecttest.txt, …); the redirect makes them show "sign in to network" and open the
// page by themselves.
class ProvisioningPortal
{
public:
    static constexpr uint32_t kDefaultTimeoutMs = 10 * 60 * 1000;
    static constexpr uint16_t kHttpPort = 80;
    // The channel used when a scan finds nothing.
    static constexpr uint8_t kDefaultChannel = 1;
    // The page's list is scanned again when it's older than this.
    static constexpr uint64_t kScanMaxAgeMs = 10000;

    // `deviceName` gives the access point's default SSID.
    ProvisioningPortal(WifiDriver& wifi, HttpServer& http, Clock& clock, NetworkScanner& scanner,
                       const SavedNetworks& networks, const WifiConnection& connection,
                       Events& events, Logger& log, std::function<std::string()> deviceName)
        : wifi_(wifi), http_(http), clock_(clock), scanner_(scanner), networks_(networks),
          connection_(connection), events_(events), log_(log), deviceName_(std::move(deviceName))
    {
    }

    ProvisioningPortal(const ProvisioningPortal&) = delete;
    ProvisioningPortal& operator=(const ProvisioningPortal&) = delete;

    // --- Settings, for the Developer ---

    // The access point's network name; the device name (such as "esp-abcdef") by default.
    // It must be 1 to 32 bytes, or empty for the default; returns false, changing nothing,
    // otherwise. Takes effect the next time the portal opens.
    bool setSsid(const std::string& ssid);
    std::string ssid() const;

    // Protects the access point with WPA2. Empty (the default) leaves it open; otherwise it must
    // be 8 to 63 printable ASCII characters, and false is returned, changing nothing, if not.
    // Takes effect the next time the portal opens.
    bool setPassword(const std::string& password);

    // Closes the portal after this long without a request from the Owner; 0 keeps it open.
    // 10 minutes by default.
    void setTimeout(uint32_t ms);

    // What a timeout does while there's no Saved Network: open the portal again (the default),
    // or leave it closed until startPortal() is called.
    void setRestartOnTimeout(bool restart);

    // Opens the portal when the device has had Saved Networks but no connection for this long,
    // and closes it again once connected. 0 (the default) never does.
    void setOpenAfterFailingFor(uint32_t ms);

    // Turns the captive portal off (it's on by default): no DNS server and no redirects, so the
    // Owner opens http://192.168.4.1/ by hand. Takes effect the next time the portal opens.
    void setCaptivePortal(bool enabled);

    // Networks weaker than this (in dBm) are left out of the list; -90 by default. Applies to
    // esp.scanResults() too.
    void setMinSignal(int8_t dbm);
    int8_t minSignal() const
    {
        return minSignalDbm_;
    }

    // --- Driven by EspManager ---

    PortalState state() const
    {
        return state_;
    }

    // Lets process() open the portal by itself from now on.
    void begin();

    // Opens the portal: a scan first, then the access point and the pages. Returns at once; the
    // portal is Active, and portalStarted raised, once process() has finished starting it.
    void start();

    // Closes the portal, or stops it from starting.
    void stop();

    void process();

    // The channel for the access point: that of the strongest Saved Network in `scan`,
    // otherwise of the strongest network in it, otherwise kDefaultChannel.
    static uint8_t chooseChannel(const std::vector<SavedNetwork>& networks,
                                 const std::vector<ScanResult>& scan);

    static bool isValidPassword(const std::string& password);

private:
    enum class Opened
    {
        ByRequest,
        NoNetwork,
        NotConnecting,
    };

    void open(Opened reason);
    void bringUp(uint8_t channel);
    void close();
    void applyRules();
    void registerRoutes();
    void sendScan(const HttpRequest& request);
    // True if the portal should answer `request`. Otherwise answers it with an error itself.
    bool accept(const HttpRequest& request);
    // The setup page's address on the access point, such as "http://192.168.4.1/".
    std::string pageUrl() const;

    WifiDriver& wifi_;
    HttpServer& http_;
    Clock& clock_;
    NetworkScanner& scanner_;
    const SavedNetworks& networks_;
    const WifiConnection& connection_;
    Events& events_;
    Logger& log_;
    std::function<std::string()> deviceName_;

    std::string ssid_;  // empty: the device name
    std::string password_;
    uint32_t timeoutMs_ = kDefaultTimeoutMs;
    bool restartOnTimeout_ = true;
    uint32_t openAfterFailingMs_ = 0;
    bool captivePortal_ = true;
    int8_t minSignalDbm_ = NetworkScanner::kDefaultMinSignalDbm;

    bool begun_ = false;
    bool routesRegistered_ = false;
    PortalState state_ = PortalState::Off;
    bool startRequested_ = false;
    bool starting_ = false;
    Opened reason_ = Opened::ByRequest;
    std::string activeSsid_;
    bool dnsRunning_ = false;
    uint64_t lastActivityMs_ = 0;
    // Set once the portal has opened for a missing Saved Network, so closing it doesn't make it
    // open again straight away; cleared when a network is saved.
    bool openedForNoNetwork_ = false;
    // When the device was last connected, or the portal last closed.
    uint64_t lastConnectedMs_ = 0;
};

}  // namespace espmgr
