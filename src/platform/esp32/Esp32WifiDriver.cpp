#include "platform/esp32/Esp32WifiDriver.h"

#include <WiFi.h>
#include <esp_mac.h>

namespace espmgr
{

namespace
{

IPAddress toIpAddress(const Ipv4Address& ip)
{
    return IPAddress(ip[0], ip[1], ip[2], ip[3]);
}

}  // namespace

void Esp32WifiDriver::ensureStarted()
{
    if (started_)
    {
        return;
    }
    started_ = true;

    // The core decides when to reconnect and stores Saved Networks itself.
    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);

    WiFi.onEvent(
        [this](arduino_event_id_t event, arduino_event_info_t info)
        {
            if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP)
            {
                wasConnected_ = true;
                connecting_ = false;
            }
            else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED)
            {
                onDisconnected(info.wifi_sta_disconnected.reason);
            }
        });
}

void Esp32WifiDriver::onDisconnected(uint8_t reason)
{
    // Called on the Wi-Fi event task. A disconnect we asked for isn't a failure.
    if (!connecting_ && !wasConnected_)
    {
        return;
    }
    lastFailure_ = wasConnected_ ? FailureReason::ConnectionLost : mapReason(reason);
    connecting_ = false;
    wasConnected_ = false;
}

FailureReason Esp32WifiDriver::mapReason(uint8_t reason)
{
    switch (reason)
    {
    case WIFI_REASON_NO_AP_FOUND:
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
        return FailureReason::NetworkNotFound;
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_MIC_FAILURE:
    case WIFI_REASON_802_1X_AUTH_FAILED:
        return FailureReason::WrongPassword;
    case WIFI_REASON_AUTH_EXPIRE:
    case WIFI_REASON_ASSOC_EXPIRE:
    case WIFI_REASON_BEACON_TIMEOUT:
    case WIFI_REASON_TIMEOUT:
        return FailureReason::Timeout;
    default:
        return FailureReason::Other;
    }
}

void Esp32WifiDriver::applyMode()
{
    // WiFi.scanNetworks() switches the station on by itself, so scanning needs no mode here.
    wifi_mode_t mode = WIFI_OFF;
    if (stationWanted_ && accessPointActive_)
    {
        mode = WIFI_AP_STA;
    }
    else if (stationWanted_)
    {
        mode = WIFI_STA;
    }
    else if (accessPointActive_)
    {
        mode = WIFI_AP;
    }
    WiFi.mode(mode);
}

void Esp32WifiDriver::connect(const std::string& ssid, const std::string& password)
{
    ensureStarted();
    lastFailure_ = FailureReason::None;
    wasConnected_ = false;
    connecting_ = true;
    stationWanted_ = true;

    // The hostname only takes effect if it's set before the station interface starts.
    if (!hostname_.empty())
    {
        WiFi.setHostname(hostname_.c_str());
    }
    applyMode();

    if (useStaticIp_)
    {
        WiFi.config(toIpAddress(staticIp_.ip), toIpAddress(staticIp_.gateway),
                    toIpAddress(staticIp_.subnet), toIpAddress(staticIp_.dns1),
                    toIpAddress(staticIp_.dns2));
    }
    else
    {
        WiFi.config(IPAddress(), IPAddress(), IPAddress());
    }

    WiFi.begin(ssid.c_str(), password.empty() ? nullptr : password.c_str());
}

void Esp32WifiDriver::disconnect()
{
    connecting_ = false;
    wasConnected_ = false;
    stationWanted_ = false;
    if (started_)
    {
        WiFi.disconnect();
        applyMode();
    }
}

StationStatus Esp32WifiDriver::status() const
{
    if (WiFi.isConnected() && wasConnected_)
    {
        return StationStatus::Connected;
    }
    if (connecting_)
    {
        return StationStatus::Connecting;
    }
    return StationStatus::Disconnected;
}

FailureReason Esp32WifiDriver::lastFailure() const
{
    return lastFailure_;
}

bool Esp32WifiDriver::startScan()
{
    ensureStarted();
    scanResults_.clear();
    const int16_t result = WiFi.scanNetworks(/*async=*/true, /*show_hidden=*/false);
    scanning_ = result == WIFI_SCAN_RUNNING;
    return scanning_;
}

bool Esp32WifiDriver::scanComplete()
{
    if (!scanning_)
    {
        return true;
    }

    const int16_t count = WiFi.scanComplete();
    if (count == WIFI_SCAN_RUNNING)
    {
        return false;
    }

    for (int16_t i = 0; i < count; ++i)
    {
        ScanResult result;
        result.ssid = WiFi.SSID(i).c_str();
        result.rssi = static_cast<int8_t>(WiFi.RSSI(i));
        result.channel = static_cast<uint8_t>(WiFi.channel(i));
        result.secured = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
        scanResults_.push_back(result);
    }
    WiFi.scanDelete();
    scanning_ = false;
    return true;
}

std::vector<ScanResult> Esp32WifiDriver::scanResults() const
{
    return scanResults_;
}

bool Esp32WifiDriver::startAccessPoint(const std::string& ssid, const std::string& password,
                                       uint8_t channel)
{
    ensureStarted();
    accessPointActive_ = true;
    applyMode();
    const bool started =
        WiFi.softAP(ssid.c_str(), password.empty() ? nullptr : password.c_str(), channel);
    if (!started)
    {
        accessPointActive_ = false;
        applyMode();
    }
    return started;
}

void Esp32WifiDriver::stopAccessPoint()
{
    if (!accessPointActive_)
    {
        return;
    }
    WiFi.softAPdisconnect();
    accessPointActive_ = false;
    applyMode();
}

uint8_t Esp32WifiDriver::accessPointClientCount() const
{
    return accessPointActive_ ? WiFi.softAPgetStationNum() : 0;
}

uint8_t Esp32WifiDriver::channel() const
{
    return static_cast<uint8_t>(WiFi.channel());
}

void Esp32WifiDriver::setHostname(const std::string& hostname)
{
    hostname_ = hostname;
}

void Esp32WifiDriver::useDhcp()
{
    useStaticIp_ = false;
}

void Esp32WifiDriver::useStaticIp(const StaticIpConfig& config)
{
    useStaticIp_ = true;
    staticIp_ = config;
}

Ipv4Address Esp32WifiDriver::localIp() const
{
    const IPAddress ip = WiFi.localIP();
    return Ipv4Address(ip[0], ip[1], ip[2], ip[3]);
}

int8_t Esp32WifiDriver::rssi() const
{
    return status() == StationStatus::Connected ? WiFi.RSSI() : 0;
}

MacAddress Esp32WifiDriver::macAddress() const
{
    // Read from eFuse, so it works before the radio has been started.
    MacAddress mac{};
    esp_read_mac(mac.data(), ESP_MAC_WIFI_STA);
    return mac;
}

}  // namespace espmgr
