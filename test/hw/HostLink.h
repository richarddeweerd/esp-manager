#pragma once

// The board side of the serial handshake with scripts/run-hw-tests.py, shared by the on-device
// tests in test/test_hw_*/:
//   board → host  "ESPMGR-HW READY"           repeated until the host answers
//   host → board  "WIFI\t<ssid>\t<password>[\t<second ssid>]"  or "NOWIFI"
//
// A second SSID means the host can switch its access point to that name, with the same
// password: "ESPMGR-HW SWITCH b" (or "a" for the first) → "SWITCH DONE <outcome>".

#include <Arduino.h>
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
