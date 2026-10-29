#pragma once

#include "core/Clock.h"
#include "core/DeviceStatus.h"
#include "core/Events.h"
#include "core/HttpServer.h"
#include "core/Logger.h"
#include "core/ManagementInterface.h"
#include "core/NetworkScanner.h"
#include "core/SavedNetworks.h"
#include "core/WebRoutes.h"
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
// The Owner's network is tested before it's saved (test-then-save): the station tries it while
// the access point stays up, and only a network that worked is saved, unless the Owner chooses
// "save anyway". Either way provisioningCompleted is raised and the portal closes a little later,
// once the page has had time to show the result.
//
// It's a captive portal: while it's open, a DNS server answers every name with the access point's
// address, and every unknown address on it redirects to the setup page. Phones check for
// internet access when they join a network (Android /generate_204, Apple /hotspot-detect.html,
// Windows /connecttest.txt, …); the redirect makes them show "sign in to network" and open the
// page by themselves.
//
// When the Developer has enabled the Management Interface, the Owner also chooses its password
// here. It's stored together with the network, once provisioning completes.
class ProvisioningPortal
{
public:
    static constexpr uint32_t kDefaultTimeoutMs = 10 * 60 * 1000;
    static constexpr uint16_t kHttpPort = WebRoutes::kHttpPort;
    // The channel used when a scan finds nothing.
    static constexpr uint8_t kDefaultChannel = 1;
    // The page's list is scanned again when it's older than this.
    static constexpr uint64_t kScanMaxAgeMs = 10000;
    // How long the portal stays open after provisioning, so the page can fetch the result.
    static constexpr uint64_t kCloseDelayMs = 30000;

    // `deviceName` gives the access point's default SSID, `status` what GET /api/status shows.
    ProvisioningPortal(WifiDriver& wifi, WebRoutes& routes, Clock& clock, NetworkScanner& scanner,
                       SavedNetworks& networks, WifiConnection& connection,
                       ManagementInterface& management, Events& events, Logger& log,
                       std::function<std::string()> deviceName,
                       std::function<DeviceStatus()> status)
        : wifi_(wifi), routes_(routes), http_(routes.http()), clock_(clock), scanner_(scanner),
          networks_(networks), connection_(connection), management_(management), events_(events),
          log_(log), deviceName_(std::move(deviceName)), status_(std::move(status))
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
    // Where the Owner's network stands, for GET /api/result.
    enum class Result
    {
        None,
        Testing,
        Connected,
        Failed,
        Saved,
    };

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
    void sendSetup();
    void receiveNetwork(const HttpRequest& request);
    void sendResult();
    void sendError(int status, const char* error);
    // Follows the trial that receiveNetwork() started.
    void followTrial();
    void provisioned(const std::string& ssid);
    // Called for every request from the Owner.
    void seen();
    // The setup page's address on the access point, such as "http://192.168.4.1/".
    std::string pageUrl() const;

    WifiDriver& wifi_;
    WebRoutes& routes_;
    HttpServer& http_;
    Clock& clock_;
    NetworkScanner& scanner_;
    SavedNetworks& networks_;
    WifiConnection& connection_;
    ManagementInterface& management_;
    Events& events_;
    Logger& log_;
    std::function<std::string()> deviceName_;
    std::function<DeviceStatus()> status_;

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

    Result result_ = Result::None;
    std::string resultSsid_;
    FailureReason resultFailure_ = FailureReason::None;
    Ipv4Address resultIp_;
    // The network on trial; saved only if the trial connects.
    SavedNetwork candidate_;
    // The Management Interface password that comes with it; empty to keep the current one.
    std::string candidateManagementPassword_;
    bool closing_ = false;
    uint64_t closeAtMs_ = 0;
};

}  // namespace espmgr
