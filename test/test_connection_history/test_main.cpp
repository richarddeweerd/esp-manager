// The Connection History (F10.3): what's recorded, the ring, its capacity and the resets.

#include <EspManager.h>
#include <FakePlatform.h>
#include <unity.h>

#include <string>
#include <vector>

using espmgr::ConnectionEvent;
using espmgr::ConnectionHistory;
using espmgr::ConnectionState;
using espmgr::FailureReason;
using espmgr::FakePlatform;
using Kind = espmgr::ConnectionEvent::Kind;

namespace
{

// An EspManager on fakes with a Saved Network. Scans fail to start, so each attempt connects at
// once to the first network.
struct Device
{
    FakePlatform fakes;
    EspManager esp{fakes.platform};

    explicit Device(bool withNetwork = true)
    {
        fakes.wifi.scanStartSucceeds = false;
        if (withNetwork)
        {
            TEST_ASSERT_TRUE(esp.networks().save("Home", "correct horse"));
        }
    }

    void connect(uint8_t channel = 6)
    {
        esp.process();
        fakes.wifi.completeConnect(channel);
        esp.process();
        TEST_ASSERT_EQUAL(ConnectionState::Connected, esp.state());
    }

    // Lets the backoff run out, so the next process() starts another attempt.
    void waitForRetry()
    {
        fakes.clock.advance(10 * 60 * 1000);
        esp.process();
    }

    std::vector<ConnectionEvent> entries()
    {
        return esp.connectionHistory().entries();
    }
};

}  // namespace

void setUp()
{
}

void tearDown()
{
}

void test_is_empty_before_begin()
{
    Device d;
    TEST_ASSERT_EQUAL(0, d.esp.connectionHistory().size());
}

void test_begin_records_started()
{
    Device d;
    d.fakes.clock.advance(1234);
    d.esp.begin();
    const auto entries = d.entries();
    TEST_ASSERT_EQUAL(1, entries.size());
    TEST_ASSERT_EQUAL(Kind::Started, entries[0].kind);
    TEST_ASSERT_EQUAL_UINT64(1234, entries[0].uptimeMs);
}

void test_records_a_connect_with_its_signal_and_channel()
{
    Device d;
    d.esp.begin();
    d.fakes.wifi.connectedRssi = -61;
    d.fakes.clock.advance(5000);
    d.connect(11);
    const auto entries = d.entries();
    TEST_ASSERT_EQUAL(2, entries.size());
    TEST_ASSERT_EQUAL(Kind::Connected, entries[1].kind);
    TEST_ASSERT_EQUAL_STRING("Home", entries[1].ssid.c_str());
    TEST_ASSERT_EQUAL_INT8(-61, entries[1].rssi);
    TEST_ASSERT_EQUAL_UINT8(11, entries[1].channel);
    TEST_ASSERT_EQUAL_UINT64(5000, entries[1].uptimeMs);
}

void test_records_a_failed_attempt_with_its_reason()
{
    Device d;
    d.esp.begin();
    d.esp.process();
    d.fakes.wifi.failConnect(FailureReason::WrongPassword);
    d.esp.process();
    const auto entries = d.entries();
    TEST_ASSERT_EQUAL(2, entries.size());
    TEST_ASSERT_EQUAL(Kind::ConnectFailed, entries[1].kind);
    TEST_ASSERT_EQUAL_STRING("Home", entries[1].ssid.c_str());
    TEST_ASSERT_EQUAL(FailureReason::WrongPassword, entries[1].reason);
}

void test_records_a_lost_connection_for_the_network_it_was_on()
{
    Device d;
    d.esp.begin();
    d.connect();
    d.fakes.wifi.loseConnection();
    d.esp.process();
    const auto entries = d.entries();
    TEST_ASSERT_EQUAL(3, entries.size());
    TEST_ASSERT_EQUAL(Kind::Disconnected, entries[2].kind);
    TEST_ASSERT_EQUAL_STRING("Home", entries[2].ssid.c_str());
    TEST_ASSERT_EQUAL(FailureReason::ConnectionLost, entries[2].reason);
}

void test_listeners_already_see_the_entry()
{
    Device d;
    size_t seen = 0;
    d.esp.onConnected([&d, &seen](const espmgr::ConnectedInfo&)
                      { seen = d.esp.connectionHistory().size(); });
    d.esp.begin();
    d.connect();
    TEST_ASSERT_EQUAL(2, seen);
}

void test_a_failed_portal_test_isnt_recorded()
{
    Device d(/*withNetwork=*/false);
    d.esp.begin();
    d.esp.process();
    d.fakes.http.localIp = d.fakes.wifi.apIp;
    const auto response = d.fakes.http.request(espmgr::HttpMethod::Post, "/api/connect",
                                               {{"ssid", "Cafe"}, {"password", "wrong password"}});
    TEST_ASSERT_EQUAL_INT(200, response.status);
    d.esp.process();
    d.fakes.wifi.failConnect(FailureReason::WrongPassword);
    d.esp.process();
    const auto entries = d.entries();
    TEST_ASSERT_EQUAL(1, entries.size());
    TEST_ASSERT_EQUAL(Kind::Started, entries[0].kind);
}

void test_drops_the_oldest_when_full()
{
    Device d;
    TEST_ASSERT_TRUE(d.esp.connectionHistory().setCapacity(3));
    d.esp.begin();
    // Started, then failures with a clock that keeps moving: only the last three are kept.
    for (int i = 0; i < 4; ++i)
    {
        d.esp.process();
        d.fakes.clock.advance(1);
        d.fakes.wifi.failConnect(FailureReason::Timeout);
        d.esp.process();
        d.waitForRetry();
    }
    const auto entries = d.entries();
    TEST_ASSERT_EQUAL(3, entries.size());
    for (const auto& entry : entries)
    {
        TEST_ASSERT_EQUAL(Kind::ConnectFailed, entry.kind);
    }
    TEST_ASSERT_TRUE(entries[0].uptimeMs < entries[1].uptimeMs);
    TEST_ASSERT_TRUE(entries[1].uptimeMs < entries[2].uptimeMs);
}

void test_keeps_twenty_by_default()
{
    Device d;
    TEST_ASSERT_EQUAL(ConnectionHistory::kDefaultCapacity, d.esp.connectionHistory().capacity());
    TEST_ASSERT_EQUAL(20, ConnectionHistory::kDefaultCapacity);
    d.esp.begin();
    for (int i = 0; i < 30; ++i)
    {
        d.esp.process();
        d.fakes.wifi.failConnect(FailureReason::Timeout);
        d.esp.process();
        d.waitForRetry();
    }
    TEST_ASSERT_EQUAL(20, d.esp.connectionHistory().size());
}

void test_capacity_zero_keeps_nothing()
{
    Device d;
    TEST_ASSERT_TRUE(d.esp.connectionHistory().setCapacity(0));
    d.esp.begin();
    d.connect();
    TEST_ASSERT_EQUAL(0, d.esp.connectionHistory().size());
}

void test_capacity_above_fifty_is_refused()
{
    Device d;
    TEST_ASSERT_TRUE(d.esp.connectionHistory().setCapacity(50));
    TEST_ASSERT_FALSE(d.esp.connectionHistory().setCapacity(51));
    TEST_ASSERT_EQUAL(50, d.esp.connectionHistory().capacity());
}

void test_capacity_after_begin_is_refused()
{
    Device d;
    d.esp.setLogLevel(espmgr::LogLevel::Error);
    d.esp.begin();
    TEST_ASSERT_FALSE(d.esp.connectionHistory().setCapacity(5));
    TEST_ASSERT_EQUAL(20, d.esp.connectionHistory().capacity());
    TEST_ASSERT_FALSE(d.fakes.log.lines.empty());
}

void test_network_reset_keeps_it_and_records_leaving()
{
    Device d;
    d.esp.begin();
    d.connect();
    d.esp.networkReset();
    d.esp.process();
    const auto entries = d.entries();
    TEST_ASSERT_EQUAL(3, entries.size());
    TEST_ASSERT_EQUAL(Kind::Disconnected, entries[2].kind);
    TEST_ASSERT_EQUAL_STRING("Home", entries[2].ssid.c_str());
    TEST_ASSERT_EQUAL(FailureReason::None, entries[2].reason);
}

void test_settings_reset_keeps_it()
{
    Device d;
    d.esp.begin();
    d.connect();
    d.esp.settingsReset();
    d.esp.process();
    TEST_ASSERT_EQUAL(2, d.esp.connectionHistory().size());
}

void test_json_has_the_fields_for_each_kind()
{
    Device d;
    d.esp.begin();
    d.esp.process();
    d.fakes.wifi.failConnect(FailureReason::WrongPassword);
    d.esp.process();
    d.waitForRetry();
    d.fakes.wifi.completeConnect(3);
    d.esp.process();
    d.fakes.wifi.loseConnection();
    d.esp.process();
    espmgr::JsonWriter json;
    espmgr::writeJson(json, d.esp.connectionHistory(), 700000);
    const std::string expected =
        R"({"uptimeMs":700000,"capacity":20,"events":[)"
        R"({"kind":"started","uptimeMs":0},)"
        R"({"kind":"connect-failed","uptimeMs":0,"ssid":"Home","reason":"wrong-password"},)"
        R"({"kind":"connected","uptimeMs":600000,"ssid":"Home","rssi":-55,"channel":3},)"
        R"({"kind":"disconnected","uptimeMs":600000,"ssid":"Home","reason":"connection-lost"}]})";
    TEST_ASSERT_EQUAL_STRING(expected.c_str(), json.str().c_str());
}

void test_failures_have_short_codes()
{
    TEST_ASSERT_EQUAL_STRING("none", espmgr::failureCode(FailureReason::None));
    TEST_ASSERT_EQUAL_STRING("network-not-found",
                             espmgr::failureCode(FailureReason::NetworkNotFound));
    TEST_ASSERT_EQUAL_STRING("wrong-password", espmgr::failureCode(FailureReason::WrongPassword));
    TEST_ASSERT_EQUAL_STRING("timeout", espmgr::failureCode(FailureReason::Timeout));
    TEST_ASSERT_EQUAL_STRING("connection-lost", espmgr::failureCode(FailureReason::ConnectionLost));
    TEST_ASSERT_EQUAL_STRING("other", espmgr::failureCode(FailureReason::Other));
}

void test_kinds_have_short_codes()
{
    TEST_ASSERT_EQUAL_STRING("started", espmgr::toString(Kind::Started));
    TEST_ASSERT_EQUAL_STRING("connected", espmgr::toString(Kind::Connected));
    TEST_ASSERT_EQUAL_STRING("connect-failed", espmgr::toString(Kind::ConnectFailed));
    TEST_ASSERT_EQUAL_STRING("disconnected", espmgr::toString(Kind::Disconnected));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_is_empty_before_begin);
    RUN_TEST(test_begin_records_started);
    RUN_TEST(test_records_a_connect_with_its_signal_and_channel);
    RUN_TEST(test_records_a_failed_attempt_with_its_reason);
    RUN_TEST(test_records_a_lost_connection_for_the_network_it_was_on);
    RUN_TEST(test_listeners_already_see_the_entry);
    RUN_TEST(test_a_failed_portal_test_isnt_recorded);
    RUN_TEST(test_drops_the_oldest_when_full);
    RUN_TEST(test_keeps_twenty_by_default);
    RUN_TEST(test_capacity_zero_keeps_nothing);
    RUN_TEST(test_capacity_above_fifty_is_refused);
    RUN_TEST(test_capacity_after_begin_is_refused);
    RUN_TEST(test_network_reset_keeps_it_and_records_leaving);
    RUN_TEST(test_settings_reset_keeps_it);
    RUN_TEST(test_json_has_the_fields_for_each_kind);
    RUN_TEST(test_failures_have_short_codes);
    RUN_TEST(test_kinds_have_short_codes);
    return UNITY_END();
}
