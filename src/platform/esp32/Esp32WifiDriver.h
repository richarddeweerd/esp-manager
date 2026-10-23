#pragma once

#include "core/WifiDriver.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace espmgr
{

// WifiDriver on Arduino-ESP32's WiFi object. The Wi-Fi event task only writes the atomics below;
// everything else runs on the caller's task.
class Esp32WifiDriver : public WifiDriver
{
public:
    void connect(const std::string& ssid, const std::string& password) override;
    void disconnect() override;
    StationStatus status() const override;
    FailureReason lastFailure() const override;

    bool startScan() override;
    bool scanComplete() override;
    std::vector<ScanResult> scanResults() const override;

    bool startAccessPoint(const std::string& ssid, const std::string& password,
                          uint8_t channel) override;
    void stopAccessPoint() override;
    uint8_t accessPointClientCount() const override;
    uint8_t channel() const override;

    void setHostname(const std::string& hostname) override;
    void useDhcp() override;
    void useStaticIp(const StaticIpConfig& config) override;

    Ipv4Address localIp() const override;
    int8_t rssi() const override;
    MacAddress macAddress() const override;

private:
    // Registers the event handler on first use, not in the constructor, because EspManager is
    // usually a global and WiFi may not be set up yet during static initialisation.
    void ensureStarted();
    void applyMode();
    void onDisconnected(uint8_t reason);

    static FailureReason mapReason(uint8_t reason);

    bool started_ = false;
    bool stationWanted_ = false;
    bool accessPointActive_ = false;
    bool scanning_ = false;
    std::string hostname_;
    bool useStaticIp_ = false;
    StaticIpConfig staticIp_;
    std::vector<ScanResult> scanResults_;

    std::atomic<bool> connecting_{false};
    std::atomic<bool> wasConnected_{false};
    std::atomic<FailureReason> lastFailure_{FailureReason::None};
};

}  // namespace espmgr
