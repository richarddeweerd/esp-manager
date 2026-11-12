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
        .key("dns")
        .string(connected ? status.dns.toString() : "")
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
        .key("resetReason")
        .string(toString(status.resetReason))
        .key("freeHeap")
        .number(status.freeHeap)
        .key("chip")
        .string(status.chip)
        .key("firmware")
        .string(status.firmwareVersion)
        .key("library")
        .string(status.libraryVersion)
        .key("time")
        .beginObject()
        .key("enabled")
        .boolean(status.time.enabled)
        .key("synchronised")
        .boolean(status.time.synchronised)
        .key("epoch")
        .number(status.time.epoch)
        .key("localTime")
        .string(status.time.localTime)
        .key("zone")
        .string(status.time.zone)
        .key("dst")
        .boolean(status.time.dst)
        .key("posix")
        .string(status.time.posix)
        .key("lastSync")
        .number(status.time.lastSync)
        .key("server")
        .string(status.time.server)
        .endObject()
        .endObject();
}

}  // namespace espmgr
