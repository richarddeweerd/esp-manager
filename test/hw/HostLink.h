#pragma once

// The board side of the serial handshake with scripts/run-hw-tests.py, shared by the on-device
// tests in test/test_hw_*/:
//   board → host  "ESPMGR-HW READY"           repeated until the host answers
//   host → board  "WIFI\t<ssid>\t<password>"  or "NOWIFI"

#include <Arduino.h>
#include <unity.h>

#include <cstdint>
#include <string>

namespace hostlink
{

// The test network the host sent; empty when there is none.
inline std::string testSsid;
inline std::string testPassword;

inline bool haveNetwork()
{
    return !testSsid.empty();
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
                testPassword = line.substr(tab + 1);
            }
            return;
        }
        if (line == "NOWIFI")
        {
            return;
        }
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

}  // namespace hostlink
