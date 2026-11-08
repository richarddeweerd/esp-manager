#pragma once

// The board side of the serial handshake with scripts/run-hw-tests.py, shared by the on-device
// tests in test/test_hw_*/:
//   board → host  "ESPMGR-HW READY"           repeated until the host answers
//   host → board  "WIFI\t<ssid>\t<password>[\t<second ssid>]"  or "NOWIFI"
//
// A second SSID means the host can switch its access point to that name, with the same
// password: "ESPMGR-HW SWITCH b" (or "a" for the first) → "SWITCH DONE <outcome>".
//
// A test that restarts the board says so again when it's back, because output sent before the
// host has reopened a native USB port is lost:
//   board → host  "ESPMGR-HW RESTARTED"       repeated until the host answers
//   host → board  "RESTART OK"

#include <Arduino.h>
#include <WiFi.h>
#include <unity.h>

#include <cstdint>
#include <functional>
#include <string>

namespace hostlink
{

// The test network the host sent; empty when there is none.
inline std::string testSsid;
inline std::string testPassword;
// The access point's other name; empty when the host can't switch.
inline std::string testSsidB;

inline bool haveNetwork()
{
    return !testSsid.empty();
}

inline bool haveSecondNetwork()
{
    return !testSsidB.empty();
}

// Reads one line from Serial, waiting at most `timeoutMs`. Returns false on a timeout.
inline bool readLine(std::string& line, uint32_t timeoutMs)
{
    line.clear();
    const uint32_t start = millis();
    while (millis() - start < timeoutMs)
    {
        while (Serial.available() > 0)
        {
            const char c = static_cast<char>(Serial.read());
            if (c == '\n')
            {
                return true;
            }
            if (c != '\r')
            {
                line += c;
            }
        }
        delay(5);
    }
    return false;
}

// Asks the host for the test network, giving up after `timeoutMs` (a plain `pio test`).
inline void askForNetwork(uint32_t timeoutMs)
{
    const uint32_t start = millis();
    std::string line;
    while (millis() - start < timeoutMs)
    {
        Serial.println("ESPMGR-HW READY");
        if (!readLine(line, 500))
        {
            continue;
        }
        if (line.rfind("WIFI\t", 0) == 0)
        {
            const size_t tab = line.find('\t', 5);
            if (tab != std::string::npos)
            {
                testSsid = line.substr(5, tab - 5);
                const size_t second = line.find('\t', tab + 1);
                testPassword = line.substr(tab + 1, second - tab - 1);
                if (second != std::string::npos)
                {
                    testSsidB = line.substr(second + 1);
                }
            }
            return;
        }
        if (line == "NOWIFI")
        {
            return;
        }
    }
}

// After a restart: waits until the host is listening again, giving up after `timeoutMs` (a plain
// `pio test`).
inline void waitForHostAfterRestart(uint32_t timeoutMs)
{
    const uint32_t start = millis();
    std::string line;
    while (millis() - start < timeoutMs)
    {
        Serial.println("ESPMGR-HW RESTARTED");
        if (readLine(line, 500) && line == "RESTART OK")
        {
            return;
        }
    }
}

// Sends `request` to the host and waits up to `timeoutMs` for the line starting with `prefix`,
// calling `idle` meanwhile (esp.process(), say). Returns the rest of that line, or "" on a
// timeout.
inline std::string ask(const std::string& request, const std::string& prefix, uint32_t timeoutMs,
                       const std::function<void()>& idle)
{
    Serial.println(request.c_str());
    std::string line;
    const uint32_t start = millis();
    while (millis() - start < timeoutMs)
    {
        idle();
        if (Serial.available() > 0 && readLine(line, 1000) && line.rfind(prefix, 0) == 0)
        {
            return line.substr(prefix.size());
        }
        delay(2);
    }
    return "";
}

// --- Wi-Fi event log, for finding out why a connect failed on the rig ---
//
// The Wi-Fi stack's events arrive on its own task. Printing them there could land in the middle
// of a Unity result line and break the host's parsing, so they're kept here and printed by
// printWifiEvents(), which the tests call from setUp() (the previous result line is complete by
// then) and after UNITY_END().

struct WifiEventRecord
{
    uint32_t atMs;
    arduino_event_id_t event;
    uint8_t reason;
    int8_t rssi;
    uint8_t channel;
};

inline constexpr size_t kWifiEventCapacity = 32;
inline WifiEventRecord wifiEvents[kWifiEventCapacity];
inline size_t wifiEventCount = 0;
inline size_t wifiEventsLost = 0;
inline portMUX_TYPE wifiEventLock = portMUX_INITIALIZER_UNLOCKED;

inline void startWifiEventLog()
{
    WiFi.onEvent(
        [](arduino_event_id_t event, arduino_event_info_t info)
        {
            WifiEventRecord record{millis(), event, 0, 0, 0};
            if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED)
            {
                record.reason = info.wifi_sta_disconnected.reason;
                record.rssi = info.wifi_sta_disconnected.rssi;
            }
            else if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED)
            {
                record.channel = info.wifi_sta_connected.channel;
            }
            else if (event != ARDUINO_EVENT_WIFI_STA_GOT_IP &&
                     event != ARDUINO_EVENT_WIFI_STA_START && event != ARDUINO_EVENT_WIFI_STA_STOP)
            {
                return;
            }
            portENTER_CRITICAL(&wifiEventLock);
            if (wifiEventCount < kWifiEventCapacity)
            {
                wifiEvents[wifiEventCount++] = record;
            }
            else
            {
                ++wifiEventsLost;
            }
            portEXIT_CRITICAL(&wifiEventLock);
        });
}

// Prints the events kept since the last call, one "wifi <ms> …" line each, and forgets them.
inline void printWifiEvents()
{
    WifiEventRecord copy[kWifiEventCapacity];
    portENTER_CRITICAL(&wifiEventLock);
    const size_t count = wifiEventCount;
    const size_t lost = wifiEventsLost;
    for (size_t i = 0; i < count; ++i)
    {
        copy[i] = wifiEvents[i];
    }
    wifiEventCount = 0;
    wifiEventsLost = 0;
    portEXIT_CRITICAL(&wifiEventLock);

    for (size_t i = 0; i < count; ++i)
    {
        const WifiEventRecord& r = copy[i];
        switch (r.event)
        {
        case ARDUINO_EVENT_WIFI_STA_START:
            Serial.printf("wifi %lu station started\n", static_cast<unsigned long>(r.atMs));
            break;
        case ARDUINO_EVENT_WIFI_STA_STOP:
            Serial.printf("wifi %lu station stopped\n", static_cast<unsigned long>(r.atMs));
            break;
        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
            Serial.printf("wifi %lu associated, channel %u\n", static_cast<unsigned long>(r.atMs),
                          r.channel);
            break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            Serial.printf("wifi %lu got an address\n", static_cast<unsigned long>(r.atMs));
            break;
        default:
            Serial.printf("wifi %lu disconnected, reason %u (%s), rssi %d\n",
                          static_cast<unsigned long>(r.atMs), r.reason,
                          WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(r.reason)),
                          r.rssi);
            break;
        }
    }
    if (lost > 0)
    {
        Serial.printf("wifi %u more events not kept\n", static_cast<unsigned>(lost));
    }
}

// Ignores the calling test when the host sent no network.
#define HOSTLINK_REQUIRE_NETWORK()                                                                 \
    do                                                                                             \
    {                                                                                              \
        if (!hostlink::haveNetwork())                                                              \
        {                                                                                          \
            TEST_IGNORE_MESSAGE("no test network (run through scripts/run-hw-tests.py)");          \
        }                                                                                          \
    } while (false)

// Ignores the calling test when the host can't switch to a second network.
#define HOSTLINK_REQUIRE_SECOND_NETWORK()                                                          \
    do                                                                                             \
    {                                                                                              \
        if (!hostlink::haveSecondNetwork())                                                        \
        {                                                                                          \
            TEST_IGNORE_MESSAGE("the host has no second test network to switch to");               \
        }                                                                                          \
    } while (false)

}  // namespace hostlink
