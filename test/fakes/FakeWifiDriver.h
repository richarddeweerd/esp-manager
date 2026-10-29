#pragma once

#include "core/WifiDriver.h"

namespace espmgr
{

// A WifiDriver that only moves when the test tells it to. connect() leaves it Connecting until
// completeConnect() or failConnect(); startScan() leaves a scan running until completeScan().
class FakeWifiDriver : public WifiDriver
{
public:
    // What the core asked for, so tests can check it.
    struct ConnectCall
    {
        std::string ssid;
        std::string password;
        std::string hostname;
        bool staticIp = false;
        StaticIpConfig staticIpConfig;
    };

    void connect(const std::string& ssid, const std::string& password) override
    {
        connectCalls.push_back({ssid, password, hostname, staticIp, staticIpConfig});
        status_ = StationStatus::Connecting;
        failure_ = FailureReason::None;
    }

    void disconnect() override
    {
        status_ = StationStatus::Disconnected;
    }

    StationStatus status() const override
    {
        return status_;
    }

    FailureReason lastFailure() const override
    {
        return failure_;
    }

    bool startScan() override
    {
        if (!scanStartSucceeds)
        {
            return false;
        }
        ++scansStarted;
        scanning_ = true;
        results_.clear();
        return true;
    }

    bool scanComplete() override
    {
        return !scanning_;
    }

    std::vector<ScanResult> scanResults() const override
    {
        return results_;
    }

    bool startAccessPoint(const std::string& ssid, const std::string& password,
                          uint8_t channel) override
    {
        if (!accessPointStartSucceeds)
        {
            return false;
        }
        accessPointActive = true;
        accessPointSsid = ssid;
        accessPointPassword = password;
        channel_ = channel;
        return true;
    }

    void stopAccessPoint() override
    {
        accessPointActive = false;
        clientCount = 0;
    }

    uint8_t accessPointClientCount() const override
    {
        return clientCount;
    }

    Ipv4Address accessPointIp() const override
    {
        return accessPointActive ? apIp : Ipv4Address();
    }

    uint8_t channel() const override
    {
        return channel_;
    }

    void setHostname(const std::string& name) override
    {
        hostname = name;
    }

    void useDhcp() override
    {
        staticIp = false;
    }

    void useStaticIp(const StaticIpConfig& config) override
    {
        staticIp = true;
        staticIpConfig = config;
    }

    Ipv4Address localIp() const override
    {
        return status_ == StationStatus::Connected ? ip : Ipv4Address();
    }

    int8_t rssi() const override
    {
        return status_ == StationStatus::Connected ? connectedRssi : 0;
    }

    MacAddress macAddress() const override
    {
        return mac;
    }

    // --- Driven by the test ---

    // Ends a pending connect() successfully. `onChannel` is the channel of the joined network.
    void completeConnect(uint8_t onChannel = 6)
    {
        status_ = StationStatus::Connected;
        channel_ = onChannel;
    }

    // Ends a pending connect() unsuccessfully.
    void failConnect(FailureReason reason)
    {
        status_ = StationStatus::Disconnected;
        failure_ = reason;
    }

    // Drops an established connection, as when the router goes away.
    void loseConnection()
    {
        status_ = StationStatus::Disconnected;
        failure_ = FailureReason::ConnectionLost;
    }

    void completeScan(std::vector<ScanResult> results)
    {
        scanning_ = false;
        results_ = std::move(results);
    }

    bool scanStartSucceeds = true;
    bool accessPointStartSucceeds = true;
    uint8_t clientCount = 0;
    Ipv4Address ip{192, 168, 1, 50};
    Ipv4Address apIp{192, 168, 4, 1};
    int8_t connectedRssi = -55;
    MacAddress mac{0x24, 0x6f, 0x28, 0xab, 0xcd, 0xef};

    // --- Recorded for the test ---

    std::vector<ConnectCall> connectCalls;
    int scansStarted = 0;
    bool accessPointActive = false;
    std::string accessPointSsid;
    std::string accessPointPassword;
    std::string hostname;
    bool staticIp = false;
    StaticIpConfig staticIpConfig;

private:
    StationStatus status_ = StationStatus::Disconnected;
    FailureReason failure_ = FailureReason::None;
    bool scanning_ = false;
    std::vector<ScanResult> results_;
    uint8_t channel_ = 1;
};

}  // namespace espmgr
