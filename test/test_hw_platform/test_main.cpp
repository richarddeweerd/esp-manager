// On-device tests of the real ESP32 hardware code (src/platform/esp32/), run on a board.
//
// scripts/run-hw-tests.py flashes this, sends the test network over the serial port and
// answers the HTTP checks. A plain `pio test -e <board>` works too, but then nothing answers
// and the tests that need a network are ignored.
//
// Serial protocol, one line each way, after the handshake in test/hw/HostLink.h:
//   board → host  "ESPMGR-HW HTTP <ip>"       the host now runs its HTTP checks
//   host → board  "HTTP DONE ok"              or "HTTP DONE <what failed>"

#include <Arduino.h>
#include <core/SavedNetworkStore.h>
#include <platform/esp32/Esp32Platform.h>
#include <unity.h>
#include <web/Pages.h>

#include "../hw/HostLink.h"

#include <algorithm>

using namespace espmgr;

namespace
{

Esp32Platform hardware;
Platform& hw = hardware.platform();

const char* const kNamespace = "espmgr-hwtest";
const char* const kRealm = "esp-manager-hw";
const char* const kUser = "admin";
const char* const kAuthPassword = "hw-check";

using hostlink::haveNetwork;
using hostlink::readLine;
using hostlink::testPassword;
using hostlink::testSsid;
uint8_t testChannel = 0;

void requireNetwork()
{
    HOSTLINK_REQUIRE_NETWORK();
}

StationStatus waitWhileConnecting(uint32_t timeoutMs)
{
    const uint64_t start = hw.clock.nowMs();
    while (hw.wifi.status() == StationStatus::Connecting && hw.clock.nowMs() - start < timeoutMs)
    {
        delay(50);
    }
    return hw.wifi.status();
}

bool waitForScan(uint32_t timeoutMs)
{
    const uint64_t start = hw.clock.nowMs();
    while (!hw.wifi.scanComplete())
    {
        if (hw.clock.nowMs() - start > timeoutMs)
        {
            return false;
        }
        delay(50);
    }
    return true;
}

}  // namespace

void setUp()
{
}

void tearDown()
{
}

// --- Clock ---

void test_clock_advances_in_milliseconds()
{
    const uint64_t start = hw.clock.nowMs();
    delay(100);
    const uint32_t elapsed = static_cast<uint32_t>(hw.clock.nowMs() - start);
    TEST_ASSERT_UINT32_WITHIN(5, 100, elapsed);
}

void test_clock_random_stays_below_the_bound()
{
    for (int i = 0; i < 1000; ++i)
    {
        TEST_ASSERT_LESS_THAN_UINT32(7, hw.clock.random(7));
    }
    TEST_ASSERT_EQUAL_UINT32(0, hw.clock.random(1));
}

// --- WifiDriver: MAC and scan ---

void test_mac_address_is_a_real_unicast_address()
{
    const MacAddress mac = hw.wifi.macAddress();
    TEST_ASSERT_FALSE(std::all_of(mac.begin(), mac.end(), [](uint8_t b) { return b == 0; }));
    TEST_ASSERT_EQUAL_UINT8(0, mac[0] & 0x01);
}

void test_scan_completes_and_finds_the_test_network()
{
    TEST_ASSERT_TRUE(hw.wifi.startScan());
    TEST_ASSERT_TRUE_MESSAGE(waitForScan(15000), "scan did not finish within 15 s");
    const std::vector<ScanResult> results = hw.wifi.scanResults();
    if (!haveNetwork())
    {
        return;
    }
    const auto found = std::find_if(results.begin(), results.end(),
                                    [](const ScanResult& r) { return r.ssid == testSsid; });
    TEST_ASSERT_TRUE_MESSAGE(found != results.end(), "test network not in the scan results");
    TEST_ASSERT_TRUE(found->secured);
    TEST_ASSERT_LESS_THAN_INT8(0, found->rssi);
    testChannel = found->channel;
}

// --- WifiDriver: connecting ---

void test_connect_to_a_missing_network_reports_network_not_found()
{
    hw.wifi.connect("espmgr-no-such-network", "whatever1");
    TEST_ASSERT_EQUAL(StationStatus::Disconnected, waitWhileConnecting(30000));
    TEST_ASSERT_EQUAL(FailureReason::NetworkNotFound, hw.wifi.lastFailure());
}

void test_connect_with_a_wrong_password_reports_wrong_password()
{
    requireNetwork();
    hw.wifi.connect(testSsid, testPassword + "-wrong");
    TEST_ASSERT_EQUAL(StationStatus::Disconnected, waitWhileConnecting(30000));
    TEST_ASSERT_EQUAL(FailureReason::WrongPassword, hw.wifi.lastFailure());
}

void test_connect_joins_the_test_network()
{
    requireNetwork();
    hw.wifi.setHostname("espmgr-hwtest");
    hw.wifi.useDhcp();
    hw.wifi.connect(testSsid, testPassword);
    TEST_ASSERT_EQUAL(StationStatus::Connected, waitWhileConnecting(30000));
    TEST_ASSERT_EQUAL(FailureReason::None, hw.wifi.lastFailure());
    TEST_ASSERT_FALSE(hw.wifi.localIp().isUnset());
    TEST_ASSERT_LESS_THAN_INT8(0, hw.wifi.rssi());
    if (testChannel != 0)
    {
        TEST_ASSERT_EQUAL_UINT8(testChannel, hw.wifi.channel());
    }
}

void test_access_point_runs_next_to_the_station()
{
    const bool connected = hw.wifi.status() == StationStatus::Connected;
    const uint8_t channel = connected ? hw.wifi.channel() : 6;
    TEST_ASSERT_TRUE(hw.wifi.startAccessPoint("espmgr-hwtest", "", channel));
    delay(500);
    TEST_ASSERT_EQUAL_UINT8(channel, hw.wifi.channel());
    TEST_ASSERT_EQUAL_UINT8(0, hw.wifi.accessPointClientCount());
    if (connected)
    {
        TEST_ASSERT_EQUAL(StationStatus::Connected, hw.wifi.status());
    }
    hw.wifi.stopAccessPoint();
}

// --- HttpServer, checked from the host over the test network ---

void test_http_server_answers_the_host()
{
    requireNetwork();
    TEST_ASSERT_EQUAL_MESSAGE(StationStatus::Connected, hw.wifi.status(),
                              "needs the connection from the previous test");

    std::string seenName;
    int authorized = 0;
    hw.http.on(HttpMethod::Get, "/hw",
               [&seenName](const HttpRequest& request)
               {
                   seenName = request.arg("name").value_or("");
                   hw.http.send(200, "text/plain", "name=" + seenName);
               });
    hw.http.on(HttpMethod::Get, "/hw-auth",
               [&authorized](const HttpRequest&)
               {
                   if (!hw.http.authenticateDigest(kRealm, kUser, kAuthPassword))
                   {
                       return;
                   }
                   ++authorized;
                   hw.http.send(200, "text/plain", "authorized");
               });
    // The generated page, served from flash as the Provisioning Portal will (#11).
    hw.http.on(HttpMethod::Get, "/page", [](const HttpRequest&) { web::sendPortalPage(hw.http); });
    hw.http.onNotFound([](const HttpRequest&) { hw.http.sendRedirect("/hw"); });
    hw.http.begin(80);

    Serial.printf("ESPMGR-HW HTTP %s\n", hw.wifi.localIp().toString().c_str());
    std::string answer;
    const uint32_t start = millis();
    while (millis() - start < 30000 && answer.rfind("HTTP DONE ", 0) != 0)
    {
        hw.http.handle();
        while (Serial.available() > 0)
        {
            const char c = static_cast<char>(Serial.read());
            if (c == '\n')
            {
                if (answer.rfind("HTTP DONE ", 0) == 0)
                {
                    break;
                }
                answer.clear();
            }
            else if (c != '\r')
            {
                answer += c;
            }
        }
        delay(2);
    }
    hw.http.stop();

    TEST_ASSERT_TRUE_MESSAGE(answer.rfind("HTTP DONE ", 0) == 0, "no answer from the host");
    TEST_ASSERT_EQUAL_STRING("ok", answer.substr(10).c_str());
    TEST_ASSERT_EQUAL_STRING("rig", seenName.c_str());
    TEST_ASSERT_EQUAL_INT(1, authorized);
}

// --- Storage (NVS) ---

void test_storage_round_trips_each_type()
{
    hw.storage.clear(kNamespace);
    TEST_ASSERT_TRUE(hw.storage.putString(kNamespace, "greeting", "hello"));
    TEST_ASSERT_TRUE(hw.storage.putU32(kNamespace, "count", 42));
    TEST_ASSERT_TRUE(hw.storage.putBool(kNamespace, "flag", true));
    TEST_ASSERT_TRUE(hw.storage.putBytes(kNamespace, "ip", {192, 168, 1, 20}));

    TEST_ASSERT_EQUAL_STRING("hello", hw.storage.getString(kNamespace, "greeting").value().c_str());
    TEST_ASSERT_EQUAL_UINT32(42, hw.storage.getU32(kNamespace, "count").value());
    TEST_ASSERT_TRUE(hw.storage.getBool(kNamespace, "flag").value());
    const std::vector<uint8_t> expected{192, 168, 1, 20};
    TEST_ASSERT_TRUE(hw.storage.getBytes(kNamespace, "ip").value() == expected);
}

void test_storage_keeps_the_type_of_a_value()
{
    TEST_ASSERT_TRUE(hw.storage.putString(kNamespace, "greeting", "hello"));
    TEST_ASSERT_FALSE(hw.storage.getU32(kNamespace, "greeting").has_value());
    TEST_ASSERT_FALSE(hw.storage.getBool(kNamespace, "greeting").has_value());
    TEST_ASSERT_FALSE(hw.storage.getBytes(kNamespace, "greeting").has_value());
}

void test_storage_rejects_names_longer_than_nvs_allows()
{
    TEST_ASSERT_FALSE(hw.storage.putString(kNamespace, "sixteen_chars_ke", "x"));
    TEST_ASSERT_FALSE(hw.storage.putString("sixteen_chars_ns", "key", "x"));
    TEST_ASSERT_TRUE(hw.storage.putString(kNamespace, "fifteen_chars_k", "x"));
}

void test_storage_remove_and_clear()
{
    TEST_ASSERT_TRUE(hw.storage.putU32(kNamespace, "count", 42));
    TEST_ASSERT_TRUE(hw.storage.remove(kNamespace, "greeting"));
    TEST_ASSERT_FALSE(hw.storage.getString(kNamespace, "greeting").has_value());
    TEST_ASSERT_TRUE(hw.storage.clear(kNamespace));
    TEST_ASSERT_FALSE(hw.storage.getU32(kNamespace, "count").has_value());
}

void test_storage_reads_nothing_from_a_missing_namespace()
{
    TEST_ASSERT_FALSE(hw.storage.getString("espmgr-nothere", "key").has_value());
    TEST_ASSERT_FALSE(hw.storage.getU32("espmgr-nothere", "key").has_value());
}

// --- SavedNetworkStore on NVS ---

void test_saved_networks_survive_nvs()
{
    Logger log(hw.log, hw.clock);
    SavedNetworkStore store(hw.storage, log);
    const std::string backupNs = SavedNetworkStore::kNamespace;
    const std::optional<std::vector<uint8_t>> previous =
        hw.storage.getBytes(backupNs, SavedNetworkStore::kKey);

    SavedNetwork dhcp;
    dhcp.ssid = "espmgr-hw-a";
    dhcp.password = "first password";
    SavedNetwork fixed;
    fixed.ssid = std::string(32, 'b');
    fixed.password = std::string(64, 'c');
    fixed.priority = 3;
    fixed.dhcp = false;
    fixed.staticIp = {Ipv4Address(10, 42, 0, 50), Ipv4Address(10, 42, 0, 1),
                      Ipv4Address(255, 255, 255, 0), Ipv4Address(10, 42, 0, 1), Ipv4Address()};

    TEST_ASSERT_TRUE(store.save({dhcp, fixed}));
    const SavedNetworkStore::LoadResult result = store.load();
    TEST_ASSERT_EQUAL(SavedNetworkStore::LoadStatus::Ok, result.status);
    TEST_ASSERT_EQUAL(2, result.networks.size());
    TEST_ASSERT_TRUE(result.networks[0] == dhcp);
    TEST_ASSERT_TRUE(result.networks[1] == fixed);

    TEST_ASSERT_TRUE(store.clear());
    TEST_ASSERT_EQUAL(SavedNetworkStore::LoadStatus::Missing, store.load().status);

    // Put back whatever the board had saved before the test.
    if (previous)
    {
        hw.storage.putBytes(backupNs, SavedNetworkStore::kKey, *previous);
    }
}

void setup()
{
    Serial.begin(115200);
    hostlink::askForNetwork(10000);

    UNITY_BEGIN();
    RUN_TEST(test_clock_advances_in_milliseconds);
    RUN_TEST(test_clock_random_stays_below_the_bound);
    RUN_TEST(test_mac_address_is_a_real_unicast_address);
    RUN_TEST(test_scan_completes_and_finds_the_test_network);
    RUN_TEST(test_connect_to_a_missing_network_reports_network_not_found);
    RUN_TEST(test_connect_with_a_wrong_password_reports_wrong_password);
    RUN_TEST(test_connect_joins_the_test_network);
    RUN_TEST(test_access_point_runs_next_to_the_station);
    RUN_TEST(test_http_server_answers_the_host);
    RUN_TEST(test_storage_round_trips_each_type);
    RUN_TEST(test_storage_keeps_the_type_of_a_value);
    RUN_TEST(test_storage_rejects_names_longer_than_nvs_allows);
    RUN_TEST(test_storage_remove_and_clear);
    RUN_TEST(test_storage_reads_nothing_from_a_missing_namespace);
    RUN_TEST(test_saved_networks_survive_nvs);
    UNITY_END();

    hw.wifi.disconnect();
}

void loop()
{
}
