#pragma once

#include "EspManagerVersion.h"
#include "core/Events.h"
#include "core/Logger.h"

#include <functional>
#include <memory>
#include <string>

namespace espmgr
{

struct Platform;
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

    // Returns immediately; all work happens in process().
    void begin();

    // Handles pending work without blocking. Call it on every loop().
    void process();

    // How much the library prints to Serial; off by default. For example
    // `esp.setLogLevel(espmgr::LogLevel::Debug);`. Call Serial.begin() first.
    void setLogLevel(LogLevel level);

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

    // Owns the ESP32 hardware objects when the default constructor made them. Type-erased, so
    // this header doesn't depend on the ESP32 code.
    std::shared_ptr<void> ownedPlatform_;
    Platform& platform_;
    Logger log_;
    Events events_{log_};
};

}  // namespace espmgr

using espmgr::EspManager;
