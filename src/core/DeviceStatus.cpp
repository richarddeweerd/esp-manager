#include "core/DeviceStatus.h"

namespace espmgr
{

const char* toString(ConnectionState state)
{
    switch (state)
    {
    case ConnectionState::Idle:
        return "Idle";
    case ConnectionState::Connecting:
        return "Connecting";
    case ConnectionState::Connected:
        return "Connected";
    case ConnectionState::Waiting:
        return "Waiting";
    }
    return "?";
}

const char* toString(PortalState state)
{
    return state == PortalState::Active ? "Active" : "Off";
}

void writeJson(JsonWriter& json, const DeviceStatus& status)
{
    const bool connected = status.connection == ConnectionState::Connected;
    json.beginObject()
        .key("connection")
        .string(toString(status.connection))
        .key("portal")
        .string(toString(status.portal))
        .key("ssid")
        .string(status.ssid)
        .key("ip")
        .string(connected ? status.ip.toString() : "")
        .key("gateway")
        .string(connected ? status.gateway.toString() : "")
        .key("rssi")
        .number(status.rssi)
        .key("channel")
        .number(status.channel)
        .key("hostname")
        .string(status.hostname)
        .key("mac")
        .string(status.mac)
        .key("uptimeMs")
        .number(static_cast<int64_t>(status.uptimeMs))
        .key("freeHeap")
        .number(status.freeHeap)
        .key("chip")
        .string(status.chip)
        .key("firmware")
        .string(status.firmwareVersion)
        .key("library")
        .string(status.libraryVersion)
        .endObject();
}

}  // namespace espmgr
