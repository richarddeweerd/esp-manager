#pragma once

#include "EspManagerVersion.h"
#include "core/DeviceName.h"
#include "core/DeviceStatus.h"
#include "core/Events.h"
#include "core/Logger.h"
#include "core/ManagementInterface.h"
#include "core/NetworkScanner.h"
#include "core/Platform.h"
#include "core/ProvisioningPortal.h"
#include "core/SavedNetworkStore.h"
#include "core/SavedNetworks.h"
#include "core/WebRoutes.h"
#include "core/WifiConnection.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace espmgr
{

class Esp32Platform;
struct EspManagerTestAccess;

// The library's single entry point: call begin() from setup() and process() from loop().
class EspManager
{
public:
    // Uses the ESP32's own Wi-Fi, NVS storage and web server.
    EspManager();
    // Uses the given hardware instead; meant for tests. `platform` must outlive this object.
    explicit EspManager(Platform& platform);

    EspManager(const EspManager&) = delete;
    EspManager& operator=(const EspManager&) = delete;

    // Loads the Saved Networks and returns immediately; connecting happens in process().
    void begin();

    // Handles pending work without blocking. Call it on every loop().
    void process();

    // Optional: blocks, calling process() itself, until connected or `timeoutMs` has passed.
    // Returns true if connected. Returns false at once if begin() wasn't called or there's no
    // Saved Network. Listeners are called as usual while it waits (US01.1.2).
    bool waitForConnection(uint32_t timeoutMs);

    // How much the library prints to Serial; off by default. For example
    // `esp.setLogLevel(espmgr::LogLevel::Debug);`. Call Serial.begin() first.
    void setLogLevel(LogLevel level);

    // Everything the status pages show: connection, SSID, IP address, signal, hostname, MAC,
    // uptime, free heap, chip and versions (US11.2.4). Never a password.
    DeviceStatus status() const;

    // Your sketch's version, shown on the status pages, such as "1.4.2". Empty by default.
    void setFirmwareVersion(const std::string& version);

    // Where the Wi-Fi connection stands: Idle, Connecting, Connected or Waiting (to retry).
    ConnectionState state() const;

    // Why the last connection attempt failed or the connection was lost (None until then), for
    // example to show it to the Owner. espmgr::toString() gives a short English text.
    FailureReason lastFailure() const;

    // How long to wait before retrying after a failure: `minMs` after the first, doubling after
    // each further failure in a row up to `maxMs`, with some random variation below it. The
    // defaults are 1 s and 5 min. Returns false, changing nothing, unless 0 < minMs <= maxMs.
    bool setRetryDelays(uint32_t minMs, uint32_t maxMs);

    // The name the device gives the router (DHCP) and the network. Defaults to
    // "<prefix>-<last 6 hex digits of the MAC>", such as "esp-abcdef". Call these before begin();
    // a change later reaches the router with the next connection.
    std::string hostname() const;
    // Uses `hostname` instead of the default. It must be 1 to 31 letters, digits and hyphens, not
    // starting or ending with a hyphen; returns false, changing nothing, otherwise.
    bool setHostname(const std::string& hostname);
    // Changes the default name's prefix ("esp"), which the Provisioning Portal's network name
    // will use too. Returns false, changing nothing, if the resulting name isn't valid.
    bool setNamePrefix(const std::string& prefix);

    // Opens the Provisioning Portal, also when there are Saved Networks, for example from the
    // Developer's own button. It opens by itself only when there's no Saved Network. Returns at
    // once; onPortalStarted reports when the access point is up. Settings are on portal().
    void startPortal();
    // Closes the Provisioning Portal.
    void stopPortal();
    // Off or Active.
    PortalState portalState() const;
    // The Provisioning Portal's settings, for example `esp.portal().setPassword("12345678");`.
    ProvisioningPortal& portal();

    // The Management Interface, off by default: `esp.management().enable();` before begin()
    // turns it on, and the Owner chooses its password in the Provisioning Portal.
    ManagementInterface& management();

    // Scans for networks without blocking: startScan() returns at once (false if no scan could
    // be started), scanComplete() turns true when it's done, and scanResults() then lists what
    // was found, cleaned up as in the Provisioning Portal: hidden networks left out, each SSID
    // once at its strongest, networks below esp.portal().setMinSignal() dropped, strongest
    // first. The device's own connection attempts share these scans.
    bool startScan();
    bool scanComplete() const;
    std::vector<ScanResult> scanResults() const;

    // The Saved Networks, for example `esp.networks().save("Home", "password");`. A network
    // saved while Idle is connected to on the next process().
    SavedNetworks& networks();

    // Events. Each call adds a listener; any number can be added, and they can't be removed.
    // Listeners are only ever called from inside process(), after the change they report, so
    // they may call back into EspManager. Each returns *this, so calls can be chained.

    // The station connected and has an IP address (US12.1.1).
    EspManager& onConnected(std::function<void(const ConnectedInfo&)> listener);
    // An established connection was lost (US12.1.2).
    EspManager& onDisconnected(std::function<void(FailureReason)> listener);
    // A connection attempt to a Saved Network failed (US12.1.3).
    EspManager&
    onConnectFailed(std::function<void(const std::string& ssid, FailureReason)> listener);
    // The connection state changed (US07.2.2).
    EspManager& onConnectionStateChanged(
        std::function<void(ConnectionState from, ConnectionState to)> listener);
    // The Provisioning Portal started, with the SSID of its access point (US12.2.1).
    EspManager& onPortalStarted(std::function<void(const std::string& accessPointSsid)> listener);
    // The Provisioning Portal stopped.
    EspManager& onPortalStopped(std::function<void()> listener);
    // The Owner finished the Provisioning Portal and the device saved the network (US12.2.2).
    EspManager& onProvisioningCompleted(std::function<void(const std::string& ssid)> listener);

private:
    friend struct EspManagerTestAccess;

    // How long waitForConnection() pauses between process() calls.
    static constexpr uint32_t kWaitStepMs = 10;

    explicit EspManager(std::shared_ptr<Esp32Platform> owned);
    // Used by the default constructor, which keeps the ESP32 hardware objects in `owned`.
    EspManager(std::shared_ptr<Esp32Platform> owned, Platform& platform);

    // Owns the ESP32 hardware objects when the default constructor made them.
    std::shared_ptr<Esp32Platform> ownedPlatform_;
    Platform& platform_;
    Logger log_{platform_.log, platform_.clock};
    Events events_{log_};
    SavedNetworkStore store_{platform_.storage, log_};
    SavedNetworks networks_{store_, log_};
    NetworkScanner scanner_{platform_.wifi, platform_.clock, log_};
    WifiConnection connection_{platform_.wifi, platform_.clock, scanner_, networks_, events_, log_};
    WebRoutes routes_{platform_.wifi, platform_.http};
    ManagementInterface management_{routes_,
                                    platform_.storage,
                                    platform_.clock,
                                    connection_,
                                    networks_,
                                    log_,
                                    [this] { return hostname(); },
                                    [this] { return status(); }};
    ProvisioningPortal portal_{platform_.wifi,
                               routes_,
                               platform_.clock,
                               scanner_,
                               networks_,
                               connection_,
                               management_,
                               events_,
                               log_,
                               [this] { return hostname(); },
                               [this] { return status(); }};
    std::string namePrefix_ = kDefaultNamePrefix;
    std::string hostname_;  // empty: the default name
    std::string firmwareVersion_;
    bool begun_ = false;
};

}  // namespace espmgr

using espmgr::EspManager;
