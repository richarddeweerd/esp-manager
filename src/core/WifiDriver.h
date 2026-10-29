#pragma once

#include "core/Ipv4Address.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace espmgr
{

// Why the last connection attempt failed, or why an established connection was lost.
enum class FailureReason
{
    None,
    NetworkNotFound,
    WrongPassword,
    Timeout,
    ConnectionLost,
    Other,
};

// A short English name for logs and status pages, such as "wrong password".
inline const char* toString(FailureReason reason)
{
    switch (reason)
    {
    case FailureReason::None:
        return "none";
    case FailureReason::NetworkNotFound:
        return "network not found";
    case FailureReason::WrongPassword:
        return "wrong password";
    case FailureReason::Timeout:
        return "timed out";
    case FailureReason::ConnectionLost:
        return "connection lost";
    case FailureReason::Other:
        break;
    }
    return "other";
}

enum class StationStatus
{
    Disconnected,
    Connecting,
    Connected,
};

struct ScanResult
{
    std::string ssid;
    int8_t rssi = 0;
    uint8_t channel = 0;
    bool secured = false;
};

struct StaticIpConfig
{
    Ipv4Address ip;
    Ipv4Address gateway;
    Ipv4Address subnet;
    Ipv4Address dns1;
    Ipv4Address dns2;
};

using MacAddress = std::array<uint8_t, 6>;

// The Wi-Fi radio: station, access point and scanning. The core polls it from process(); an
// implementation never calls back into the core.
class WifiDriver
{
public:
    virtual ~WifiDriver() = default;

    // Starts connecting and returns at once. status() is Connecting until the attempt ends.
    // An empty password joins an open network.
    virtual void connect(const std::string& ssid, const std::string& password) = 0;
    virtual void disconnect() = 0;
    virtual StationStatus status() const = 0;
    // Set when an attempt fails or a connection is lost; cleared by connect().
    virtual FailureReason lastFailure() const = 0;

    // Starts a scan and returns at once. Returns false if a scan couldn't be started.
    virtual bool startScan() = 0;
    // True once the scan started by startScan() has finished, also when it failed.
    virtual bool scanComplete() = 0;
    // The networks found by the last completed scan. Hidden networks are left out.
    virtual std::vector<ScanResult> scanResults() const = 0;

    // Runs alongside the station connection. An empty password makes the access point open.
    virtual bool startAccessPoint(const std::string& ssid, const std::string& password,
                                  uint8_t channel) = 0;
    virtual void stopAccessPoint() = 0;
    virtual uint8_t accessPointClientCount() const = 0;
    // The radio's current channel, shared by station and access point.
    virtual uint8_t channel() const = 0;

    // Used by the next connect().
    virtual void setHostname(const std::string& hostname) = 0;
    virtual void useDhcp() = 0;
    virtual void useStaticIp(const StaticIpConfig& config) = 0;

    virtual Ipv4Address localIp() const = 0;
    virtual int8_t rssi() const = 0;
    virtual MacAddress macAddress() const = 0;
};

}  // namespace espmgr
