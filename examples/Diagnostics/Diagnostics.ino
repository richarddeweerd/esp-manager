// Finding out what the device is doing: the library's log lines in your own format, the
// Connection History, and the status with the reason the device last started.
//
// The log handler here prints to Serial; a sketch could send the lines to syslog or MQTT instead.
// It's called at once from inside the library, so it must not call back into EspManager.

#include <Arduino.h>
#include <EspManager.h>

namespace
{

EspManager esp;
uint32_t lastReportMs = 0;
const uint32_t kReportEveryMs = 60000;

const char* levelName(espmgr::LogLevel level)
{
    switch (level)
    {
    case espmgr::LogLevel::Error:
        return "ERROR";
    case espmgr::LogLevel::Warn:
        return "WARN";
    case espmgr::LogLevel::Info:
        return "INFO";
    case espmgr::LogLevel::Debug:
        return "DEBUG";
    default:
        return "VERBOSE";
    }
}

void printEvent(const espmgr::ConnectionEvent& event)
{
    using Kind = espmgr::ConnectionEvent::Kind;
    Serial.printf("  %8.1f s  ", event.uptimeMs / 1000.0);
    switch (event.kind)
    {
    case Kind::Started:
        Serial.println("started");
        break;
    case Kind::Connected:
        Serial.printf("connected to %s, %d dBm, channel %u\n", event.ssid.c_str(), event.rssi,
                      event.channel);
        break;
    case Kind::ConnectFailed:
        Serial.printf("couldn't connect to %s: %s\n", event.ssid.c_str(),
                      espmgr::toString(event.reason));
        break;
    case Kind::Disconnected:
        Serial.printf("left %s: %s\n", event.ssid.c_str(), espmgr::toString(event.reason));
        break;
    }
}

void report()
{
    const espmgr::DeviceStatus status = esp.status();
    Serial.printf("%s, last start: %s, %u KB free\n", espmgr::toString(status.connection),
                  espmgr::toString(status.resetReason),
                  static_cast<unsigned>(status.freeHeap / 1024));
    if (status.connection == espmgr::ConnectionState::Connected)
    {
        Serial.printf("  %s at %s, gateway %s, DNS %s, %d dBm\n", status.ssid.c_str(),
                      status.ip.toString().c_str(), status.gateway.toString().c_str(),
                      status.dns.toString().c_str(), status.rssi);
    }
    Serial.println("Connection History:");
    for (const espmgr::ConnectionEvent& event : esp.connectionHistory().entries())
    {
        printEvent(event);
    }
}

}  // namespace

void setup()
{
    Serial.begin(115200);

    // Every line in our own format. Setting a handler doesn't turn logging on: the level does.
    esp.setLogHandler(
        [](const espmgr::LogEntry& entry)
        {
            Serial.printf("%10lu ms %-7s %-6s %s\n", static_cast<unsigned long>(entry.uptimeMs),
                          levelName(entry.level), entry.tag, entry.message);
        });
    esp.setLogLevel(espmgr::LogLevel::Info);

    // Keep the last 30 connection events instead of 20; only before begin().
    esp.connectionHistory().setCapacity(30);

    // The Management Interface shows the same on its Status tab, under Recent events.
    esp.management().enable();

    esp.begin();
}

void loop()
{
    esp.process();

    if (millis() - lastReportMs >= kReportEveryMs)
    {
        lastReportMs = millis();
        report();
    }
}
