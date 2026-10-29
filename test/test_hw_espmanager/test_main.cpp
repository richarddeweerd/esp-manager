// On-device test of EspManager as a sketch uses it: the default constructor on real hardware,
// a network saved with esp.networks(), then begin() and waitForConnection(), which calls
// process() (#7, #9), the default hostname and a static-IP Saved Network (#10), and the
// Provisioning Portal opened next to the connection (#12). The rig can't join the portal's
// access point, so it checks from the test network that the portal refuses it there.
//
// Run it with scripts/run-hw-tests.py, which sends the test network; see test/hw/HostLink.h.
// The test network stays saved on the board afterwards, as it would after setup.

#include <Arduino.h>
#include <EspManager.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <unity.h>

#include "../hw/HostLink.h"

#include <algorithm>
#include <string>
#include <vector>

using espmgr::ConnectedInfo;
using espmgr::ConnectionState;
using espmgr::FailureReason;
using espmgr::LogLevel;
using espmgr::PortalState;

namespace
{

EspManager esp;
std::vector<std::string> events;
ConnectedInfo connected;
std::string portalSsid;

const char* name(ConnectionState state)
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

}  // namespace

void setUp()
{
}

void tearDown()
{
}

void test_saved_network_is_stored()
{
    HOSTLINK_REQUIRE_NETWORK();
    TEST_ASSERT_TRUE(esp.networks().save(hostlink::testSsid, hostlink::testPassword));
    TEST_ASSERT_EQUAL(1, esp.networks().count());
}

void test_begin_returns_at_once_and_stays_idle()
{
    const uint32_t start = millis();
    esp.begin();
    TEST_ASSERT_LESS_THAN_UINT32(100, millis() - start);
    TEST_ASSERT_EQUAL_STRING("Idle", name(esp.state()));
}

void test_wait_for_connection_connects_to_the_saved_network()
{
    HOSTLINK_REQUIRE_NETWORK();
    const uint32_t start = millis();
    TEST_ASSERT_TRUE_MESSAGE(esp.waitForConnection(30000), "not connected within 30 s");
    TEST_ASSERT_LESS_THAN_UINT32(30000, millis() - start);
    TEST_ASSERT_EQUAL_STRING("Connected", name(esp.state()));
    TEST_ASSERT_EQUAL_STRING(hostlink::testSsid.c_str(), connected.ssid.c_str());
    TEST_ASSERT_FALSE(connected.ip.isUnset());
    TEST_ASSERT_NOT_EQUAL(0, connected.channel);
    TEST_ASSERT_TRUE(events.size() >= 2);
    TEST_ASSERT_EQUAL_STRING("Idle>Connecting", events.front().c_str());
    TEST_ASSERT_EQUAL_STRING("Connecting>Connected", events.back().c_str());
}

void test_each_process_call_is_short()
{
    HOSTLINK_REQUIRE_NETWORK();
    uint32_t longest = 0;
    for (int i = 0; i < 200; ++i)
    {
        const uint32_t start = micros();
        esp.process();
        longest = std::max(longest, micros() - start);
    }
    TEST_ASSERT_LESS_THAN_UINT32_MESSAGE(5000, longest, "process() took 5 ms or more");
    TEST_ASSERT_EQUAL_STRING("Connected", name(esp.state()));
}

// Reconnects through the state machine: drops the connection and waits for the next attempt.
bool reconnect()
{
    WiFi.disconnect();
    return esp.waitForConnection(30000);
}

void test_hostname_reaches_the_wifi_stack()
{
    HOSTLINK_REQUIRE_NETWORK();
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char expected[16];
    snprintf(expected, sizeof(expected), "esp-%02x%02x%02x", mac[3], mac[4], mac[5]);
    TEST_ASSERT_EQUAL_STRING(expected, esp.hostname().c_str());
    TEST_ASSERT_EQUAL_STRING(expected, WiFi.STA.getHostname());
}

void test_static_ip_saved_network_gets_its_address()
{
    HOSTLINK_REQUIRE_NETWORK();
    // Below the range the rig's access point hands out (10.42.0.10 and up).
    espmgr::SavedNetwork network;
    network.ssid = hostlink::testSsid;
    network.password = hostlink::testPassword;
    network.dhcp = false;
    network.staticIp = {espmgr::Ipv4Address(10, 42, 0, 5), espmgr::Ipv4Address(10, 42, 0, 1),
                        espmgr::Ipv4Address(255, 255, 255, 0), espmgr::Ipv4Address(10, 42, 0, 1),
                        espmgr::Ipv4Address()};
    TEST_ASSERT_TRUE(esp.networks().save(network));
    TEST_ASSERT_TRUE_MESSAGE(reconnect(), "not reconnected within 30 s");
    TEST_ASSERT_EQUAL_STRING("10.42.0.5", connected.ip.toString().c_str());
    TEST_ASSERT_EQUAL_STRING("10.42.0.5", WiFi.localIP().toString().c_str());
    TEST_ASSERT_EQUAL_STRING("10.42.0.1", WiFi.gatewayIP().toString().c_str());
}

void test_back_to_dhcp_gets_a_leased_address()
{
    HOSTLINK_REQUIRE_NETWORK();
    TEST_ASSERT_TRUE(esp.networks().save(hostlink::testSsid, hostlink::testPassword));
    TEST_ASSERT_TRUE_MESSAGE(reconnect(), "not reconnected within 30 s");
    TEST_ASSERT_NOT_EQUAL(5, connected.ip[3]);
    TEST_ASSERT_EQUAL_UINT8(10, connected.ip[0]);
    TEST_ASSERT_EQUAL_UINT8(42, connected.ip[1]);
}

// Calls process() until the portal is in `state`, for at most 10 s.
bool waitForPortal(PortalState state)
{
    const uint32_t start = millis();
    while (esp.portalState() != state && millis() - start < 10000)
    {
        esp.process();
        delay(5);
    }
    return esp.portalState() == state;
}

wifi_auth_mode_t accessPointAuthMode()
{
    wifi_config_t config{};
    esp_wifi_get_config(WIFI_IF_AP, &config);
    return config.ap.authmode;
}

void test_portal_opens_next_to_the_connection_on_its_channel()
{
    HOSTLINK_REQUIRE_NETWORK();
    esp.startPortal();
    TEST_ASSERT_TRUE_MESSAGE(waitForPortal(PortalState::Active), "portal didn't open in 10 s");
    TEST_ASSERT_EQUAL(WIFI_AP_STA, WiFi.getMode());
    TEST_ASSERT_EQUAL_STRING(esp.hostname().c_str(), WiFi.softAPSSID().c_str());
    TEST_ASSERT_EQUAL_STRING(esp.hostname().c_str(), portalSsid.c_str());
    TEST_ASSERT_EQUAL_STRING("192.168.4.1", WiFi.softAPIP().toString().c_str());
    TEST_ASSERT_EQUAL(WIFI_AUTH_OPEN, accessPointAuthMode());
    TEST_ASSERT_EQUAL_UINT8(connected.channel, WiFi.channel());
    TEST_ASSERT_EQUAL_STRING("Connected", name(esp.state()));
}

void test_portal_refuses_requests_from_the_test_network()
{
    HOSTLINK_REQUIRE_NETWORK();
    TEST_ASSERT_EQUAL(PortalState::Active, esp.portalState());
    Serial.printf("ESPMGR-HW PORTAL %s\n", WiFi.localIP().toString().c_str());
    std::string answer;
    const uint32_t start = millis();
    while (millis() - start < 30000)
    {
        esp.process();
        if (Serial.available() > 0 && hostlink::readLine(answer, 1000) &&
            answer.rfind("PORTAL DONE ", 0) == 0)
        {
            break;
        }
        delay(2);
    }
    TEST_ASSERT_TRUE_MESSAGE(answer.rfind("PORTAL DONE ", 0) == 0, "no answer from the host");
    TEST_ASSERT_EQUAL_STRING("ok", answer.substr(12).c_str());
}

void test_stop_portal_closes_the_access_point()
{
    HOSTLINK_REQUIRE_NETWORK();
    esp.stopPortal();
    TEST_ASSERT_TRUE(waitForPortal(PortalState::Off));
    TEST_ASSERT_EQUAL(WIFI_STA, WiFi.getMode());
    TEST_ASSERT_EQUAL_STRING("Connected", name(esp.state()));
}

void test_portal_password_makes_the_access_point_wpa2()
{
    HOSTLINK_REQUIRE_NETWORK();
    TEST_ASSERT_TRUE(esp.portal().setPassword("hw-portal-pw"));
    esp.startPortal();
    TEST_ASSERT_TRUE(waitForPortal(PortalState::Active));
    TEST_ASSERT_NOT_EQUAL(WIFI_AUTH_OPEN, accessPointAuthMode());
    esp.stopPortal();
    TEST_ASSERT_TRUE(waitForPortal(PortalState::Off));
    TEST_ASSERT_TRUE(esp.portal().setPassword(""));
}

void setup()
{
    Serial.begin(115200);
    hostlink::askForNetwork(10000);

    esp.setLogLevel(LogLevel::Info);
    esp.onConnectionStateChanged([](ConnectionState from, ConnectionState to)
                                 { events.push_back(std::string(name(from)) + ">" + name(to)); });
    esp.onConnected([](const ConnectedInfo& info) { connected = info; });
    esp.onPortalStarted([](const std::string& ssid) { portalSsid = ssid; });
    esp.onConnectFailed(
        [](const std::string&, FailureReason reason)
        { Serial.printf("connect failed, reason %d\n", static_cast<int>(reason)); });

    UNITY_BEGIN();
    RUN_TEST(test_saved_network_is_stored);
    RUN_TEST(test_begin_returns_at_once_and_stays_idle);
    RUN_TEST(test_wait_for_connection_connects_to_the_saved_network);
    RUN_TEST(test_each_process_call_is_short);
    RUN_TEST(test_hostname_reaches_the_wifi_stack);
    RUN_TEST(test_static_ip_saved_network_gets_its_address);
    RUN_TEST(test_back_to_dhcp_gets_a_leased_address);
    RUN_TEST(test_portal_opens_next_to_the_connection_on_its_channel);
    RUN_TEST(test_portal_refuses_requests_from_the_test_network);
    RUN_TEST(test_stop_portal_closes_the_access_point);
    RUN_TEST(test_portal_password_makes_the_access_point_wpa2);
    UNITY_END();

    // Leave the test network properly before the next test is flashed. A board that is just
    // reset stays listed at the Pi's access point, which then refuses its next attempts for a
    // while. EspManager has no disconnect() yet, so go to the Wi-Fi library directly.
    WiFi.disconnect();
    delay(500);
}

void loop()
{
}
