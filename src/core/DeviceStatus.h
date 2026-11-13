#pragma once

#include "core/Events.h"
#include "core/Ipv4Address.h"
#include "core/JsonWriter.h"
#include "core/System.h"
#include "core/UpdateTypes.h"

#include <cstdint>
#include <string>

namespace espmgr
{

// Network Time's part of the status (EP18).
struct TimeStatus
{
    bool enabled = false;
    bool synchronised = false;
    // Seconds since 1970-01-01 UTC now, and the local time as "2026-11-12T13:53:47+01:00"; 0 and
    // empty until synchronised.
    int64_t epoch = 0;
    std::string localTime;
    // The Owner's named zone ("Europe/Amsterdam") and daylight saving choice, when one was
    // picked; the POSIX string is the one in use either way.
    std::string zone;
    bool dst = true;
    std::string posix;
    // Seconds since 1970-01-01 UTC of the last successful sync; 0 before the first.
    int64_t lastSync = 0;
    // The server that answered; empty before the first answer.
    std::string server;
};

// The firmware updates part of the status (EP17).
struct UpdateStatus
{
    // A server is set, so updates can work.
    bool configured = false;
    UpdateState state = UpdateState::Idle;
    std::string channel;
    // The version the last check found; empty if none.
    std::string available;
    // 0 to 100 while downloading.
    uint8_t progress = 0;
    // The running firmware is new and not confirmed yet.
    bool pending = false;
    // The most recent install attempt.
    UpdateRecord last;
};

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
    // The first DNS server.
    Ipv4Address dns;
    // dBm; 0 while not connected.
    int8_t rssi = 0;
    uint8_t channel = 0;

    std::string hostname;
    std::string mac;  // "24:6f:28:ab:cd:ef"
    uint64_t uptimeMs = 0;
    // Why the device last started.
    ResetReason resetReason = ResetReason::Other;
    uint32_t freeHeap = 0;
    std::string chip;
    // Set by the Developer with esp.setFirmwareVersion(); empty if not.
    std::string firmwareVersion;
    std::string libraryVersion;
    TimeStatus time;
    UpdateStatus update;
};

// "Idle", "Connecting", "Connected" or "Waiting".
const char* toString(ConnectionState state);
// "Off" or "Active".
const char* toString(PortalState state);

// Writes an update record as one JSON object.
void writeJson(JsonWriter& json, const UpdateRecord& record);

// Writes `status` as one JSON object, for GET /api/status.
void writeJson(JsonWriter& json, const DeviceStatus& status);

}  // namespace espmgr
