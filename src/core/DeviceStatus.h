#pragma once

#include "core/Events.h"
#include "core/Ipv4Address.h"
#include "core/JsonWriter.h"

#include <cstdint>
#include <string>

namespace espmgr
{

// Where the device stands, for the status pages and for sketches (esp.status()). It never holds
// a password.
struct DeviceStatus
{
    ConnectionState connection = ConnectionState::Idle;
    PortalState portal = PortalState::Off;
    // The network the station is connected to or trying; empty when Idle.
    std::string ssid;
    // Unset while not connected.
    Ipv4Address ip;
    Ipv4Address gateway;
    // dBm; 0 while not connected.
    int8_t rssi = 0;
    uint8_t channel = 0;

    std::string hostname;
    std::string mac;  // "24:6f:28:ab:cd:ef"
    uint64_t uptimeMs = 0;
    uint32_t freeHeap = 0;
    std::string chip;
    // Set by the Developer with esp.setFirmwareVersion(); empty if not.
    std::string firmwareVersion;
    std::string libraryVersion;
};

// "Idle", "Connecting", "Connected" or "Waiting".
const char* toString(ConnectionState state);
// "Off" or "Active".
const char* toString(PortalState state);

// Writes `status` as one JSON object, for GET /api/status.
void writeJson(JsonWriter& json, const DeviceStatus& status);

}  // namespace espmgr
