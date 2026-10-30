#include <EspManager.h>
#include <FakePlatform.h>
#include <unity.h>

#include <string>
#include <vector>

using espmgr::ConnectedInfo;
using espmgr::ConnectionState;
using espmgr::FailureReason;
using espmgr::FakePlatform;
using espmgr::Ipv4Address;
using espmgr::SavedNetwork;
using espmgr::SavedNetworkStore;
using espmgr::StationStatus;
using espmgr::WifiConnection;

namespace
{

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

// An EspManager on fakes that records every event as text.
struct Device
{
    FakePlatform fakes;
    EspManager esp{fakes.platform};
    std::vector<std::string> events;

    Device()
    {
        // These tests are about the states themselves: without a scan, each attempt connects
        // straight away. test_reconnect covers scanning and the retry schedule.
        fakes.wifi.scanStartSucceeds = false;
        esp.onConnectionStateChanged(
               [this](ConnectionState from, ConnectionState to)
               { events.push_back(std::string(name(from)) + ">" + name(to)); })
            .onConnected(
                [this](const ConnectedInfo& info)
                {
                    events.push_back("connected " + info.ssid + " " + info.ip.toString() + " ch" +
                                     std::to_string(info.channel));
                })
            .onConnectFailed(
                [this](const std::string& ssid, FailureReason reason)
                {
                    events.push_back("failed " + ssid + " " +
                                     std::to_string(static_cast<int>(reason)));
                })
            .onDisconnected(
                [this](FailureReason reason)
                { events.push_back("lost " + std::to_string(static_cast<int>(reason))); });
    }

    // Stores a network the way an earlier run of the firmware would have.
    void storeNetwork(const std::string& ssid, const std::string& password)
    {
        TEST_ASSERT_TRUE(esp.networks().save(ssid, password));
    }

    std::string takeEvents()
    {
        std::string out;
        for (const std::string& event : events)
        {
            out += (out.empty() ? "" : ", ") + event;
        }
        events.clear();
        return out;
    }

    void assertState(ConnectionState expected)
    {
        TEST_ASSERT_EQUAL_STRING(name(expected), name(esp.state()));
    }
};

std::string text(FailureReason reason)
{
    return std::to_string(static_cast<int>(reason));
}

}  // namespace

void setUp()
{
}

void tearDown()
{
}

void test_nothing_happens_before_begin()
{
    Device d;
    d.storeNetwork("Home", "correct horse");
    d.esp.process();
    d.assertState(ConnectionState::Idle);
    TEST_ASSERT_EQUAL(0, d.fakes.wifi.connectCalls.size());
}

void test_without_a_saved_network_it_stays_idle()
{
    Device d;
    d.esp.begin();
    d.esp.process();
    d.esp.process();
    d.assertState(ConnectionState::Idle);
    TEST_ASSERT_EQUAL(0, d.fakes.wifi.connectCalls.size());
    TEST_ASSERT_EQUAL_STRING("", d.takeEvents().c_str());
}

void test_begin_returns_without_connecting()
{
    Device d;
    d.storeNetwork("Home", "correct horse");
    d.esp.begin();
    d.assertState(ConnectionState::Idle);
    TEST_ASSERT_EQUAL(0, d.fakes.wifi.connectCalls.size());
}

void test_connects_to_the_stored_network_after_a_restart()
{
    FakePlatform fakes;
    fakes.wifi.scanStartSucceeds = false;
    {
        EspManager firstBoot(fakes.platform);
        TEST_ASSERT_TRUE(firstBoot.networks().save("Home", "correct horse"));
    }
    EspManager esp(fakes.platform);
    esp.begin();
    TEST_ASSERT_EQUAL(1, esp.networks().count());
    esp.process();
    TEST_ASSERT_EQUAL(ConnectionState::Connecting, esp.state());
    TEST_ASSERT_EQUAL(1, fakes.wifi.connectCalls.size());
    TEST_ASSERT_EQUAL_STRING("Home", fakes.wifi.connectCalls[0].ssid.c_str());
    TEST_ASSERT_EQUAL_STRING("correct horse", fakes.wifi.connectCalls[0].password.c_str());
    TEST_ASSERT_FALSE(fakes.wifi.connectCalls[0].staticIp);
}

void test_idle_to_connecting_to_connected()
{
    Device d;
    d.storeNetwork("Home", "correct horse");
    d.esp.begin();
    d.esp.process();
    d.assertState(ConnectionState::Connecting);
    TEST_ASSERT_EQUAL_STRING("Idle>Connecting", d.takeEvents().c_str());

    d.esp.process();
    d.assertState(ConnectionState::Connecting);
    d.fakes.wifi.ip = Ipv4Address(192, 168, 1, 50);
    d.fakes.wifi.completeConnect(11);
    d.esp.process();
    d.assertState(ConnectionState::Connected);
    TEST_ASSERT_EQUAL_STRING("Connecting>Connected, connected Home 192.168.1.50 ch11",
                             d.takeEvents().c_str());

    d.esp.process();
    TEST_ASSERT_EQUAL_STRING("", d.takeEvents().c_str());
    TEST_ASSERT_EQUAL(1, d.fakes.wifi.connectCalls.size());
}

void test_failed_attempt_waits_then_retries()
{
    Device d;
    d.storeNetwork("Home", "correct horse");
    d.esp.begin();
    d.esp.process();
    d.takeEvents();

    d.fakes.wifi.failConnect(FailureReason::WrongPassword);
    d.esp.process();
    d.assertState(ConnectionState::Waiting);
    TEST_ASSERT_EQUAL_STRING(
        ("Connecting>Waiting, failed Home " + text(FailureReason::WrongPassword)).c_str(),
        d.takeEvents().c_str());

    d.fakes.clock.advance(WifiConnection::kDefaultMinRetryDelayMs - 1);
    d.esp.process();
    d.assertState(ConnectionState::Waiting);
    TEST_ASSERT_EQUAL(1, d.fakes.wifi.connectCalls.size());

    d.fakes.clock.advance(1);
    d.esp.process();
    d.assertState(ConnectionState::Connecting);
    TEST_ASSERT_EQUAL(2, d.fakes.wifi.connectCalls.size());
    TEST_ASSERT_EQUAL_STRING("Waiting>Connecting", d.takeEvents().c_str());
}

void test_attempt_that_never_finishes_times_out()
{
    Device d;
    d.storeNetwork("Home", "correct horse");
    d.esp.begin();
    d.esp.process();
    d.takeEvents();

    d.fakes.clock.advance(WifiConnection::kAttemptTimeoutMs - 1);
    d.esp.process();
    d.assertState(ConnectionState::Connecting);

    d.fakes.clock.advance(1);
    d.esp.process();
    d.assertState(ConnectionState::Waiting);
    TEST_ASSERT_EQUAL(StationStatus::Disconnected, d.fakes.wifi.status());
    TEST_ASSERT_EQUAL_STRING(
        ("Connecting>Waiting, failed Home " + text(FailureReason::Timeout)).c_str(),
        d.takeEvents().c_str());
}

void test_failure_without_a_reason_is_reported_as_other()
{
    Device d;
    d.storeNetwork("Home", "correct horse");
    d.esp.begin();
    d.esp.process();
    d.takeEvents();
    d.fakes.wifi.disconnect();  // Disconnected, but lastFailure() is None
    d.esp.process();
    TEST_ASSERT_EQUAL_STRING(
        ("Connecting>Waiting, failed Home " + text(FailureReason::Other)).c_str(),
        d.takeEvents().c_str());
}

void test_lost_connection_waits_then_reconnects()
{
    Device d;
    d.storeNetwork("Home", "correct horse");
    d.esp.begin();
    d.esp.process();
    d.fakes.wifi.completeConnect();
    d.esp.process();
    d.takeEvents();

    d.fakes.wifi.loseConnection();
    d.esp.process();
    d.assertState(ConnectionState::Waiting);
    TEST_ASSERT_EQUAL_STRING(
        ("Connected>Waiting, lost " + text(FailureReason::ConnectionLost)).c_str(),
        d.takeEvents().c_str());

    d.fakes.clock.advance(WifiConnection::kDefaultMinRetryDelayMs);
    d.esp.process();
    d.assertState(ConnectionState::Connecting);
    d.fakes.wifi.completeConnect();
    d.esp.process();
    d.assertState(ConnectionState::Connected);
    TEST_ASSERT_EQUAL(2, d.fakes.wifi.connectCalls.size());
}

void test_network_saved_while_idle_is_used_on_the_next_process()
{
    Device d;
    d.esp.begin();
    d.esp.process();
    d.assertState(ConnectionState::Idle);
    TEST_ASSERT_TRUE(d.esp.networks().save("Office", "battery staple"));
    d.esp.process();
    d.assertState(ConnectionState::Connecting);
    TEST_ASSERT_EQUAL_STRING("Office", d.fakes.wifi.connectCalls.back().ssid.c_str());
}

void test_network_saved_while_connected_is_used_from_the_next_attempt()
{
    Device d;
    d.storeNetwork("Home", "correct horse");
    d.esp.begin();
    d.esp.process();
    d.fakes.wifi.completeConnect();
    d.esp.process();

    TEST_ASSERT_TRUE(d.esp.networks().save("Office", "battery staple"));
    d.esp.process();
    d.assertState(ConnectionState::Connected);
    TEST_ASSERT_EQUAL(1, d.fakes.wifi.connectCalls.size());

    d.fakes.wifi.loseConnection();
    d.esp.process();
    d.fakes.clock.advance(WifiConnection::kDefaultMinRetryDelayMs);
    d.esp.process();
    TEST_ASSERT_EQUAL_STRING("Office", d.fakes.wifi.connectCalls.back().ssid.c_str());
}

void test_clearing_the_networks_while_waiting_goes_idle()
{
    Device d;
    d.storeNetwork("Home", "correct horse");
    d.esp.begin();
    d.esp.process();
    d.fakes.wifi.failConnect(FailureReason::NetworkNotFound);
    d.esp.process();
    d.takeEvents();

    TEST_ASSERT_TRUE(d.esp.networks().clear());
    d.esp.process();
    d.assertState(ConnectionState::Idle);
    TEST_ASSERT_EQUAL_STRING("Waiting>Idle", d.takeEvents().c_str());
    d.fakes.clock.advance(60000);
    d.esp.process();
    TEST_ASSERT_EQUAL(1, d.fakes.wifi.connectCalls.size());
}

void test_static_addressing_is_applied_to_the_attempt()
{
    Device d;
    SavedNetwork network;
    network.ssid = "Office";
    network.password = "battery staple";
    network.dhcp = false;
    network.staticIp = {Ipv4Address(10, 0, 0, 20), Ipv4Address(10, 0, 0, 1),
                        Ipv4Address(255, 255, 255, 0), Ipv4Address(), Ipv4Address()};
    TEST_ASSERT_TRUE(d.esp.networks().save(network));
    d.esp.begin();
    d.esp.process();
    TEST_ASSERT_TRUE(d.fakes.wifi.connectCalls[0].staticIp);
    TEST_ASSERT_TRUE(d.fakes.wifi.connectCalls[0].staticIpConfig.ip == Ipv4Address(10, 0, 0, 20));
}

void test_listeners_see_the_new_state()
{
    Device d;
    std::vector<std::string> seen;
    d.esp.onConnectionStateChanged(
        [&](ConnectionState, ConnectionState to)
        { seen.push_back(std::string(name(to)) + "=" + name(d.esp.state())); });
    d.esp.onConnected([&](const ConnectedInfo&) { seen.push_back(name(d.esp.state())); });
    d.storeNetwork("Home", "correct horse");
    d.esp.begin();
    d.esp.process();
    d.fakes.wifi.completeConnect();
    d.esp.process();
    TEST_ASSERT_EQUAL(3, seen.size());
    TEST_ASSERT_EQUAL_STRING("Connecting=Connecting", seen[0].c_str());
    TEST_ASSERT_EQUAL_STRING("Connected=Connected", seen[1].c_str());
    TEST_ASSERT_EQUAL_STRING("Connected", seen[2].c_str());
}

void test_unreadable_record_counts_as_no_network()
{
    Device d;
    d.fakes.storage.data[SavedNetworkStore::kNamespace][SavedNetworkStore::kKey] =
        std::vector<uint8_t>{99, 0};
    d.esp.begin();
    d.esp.process();
    d.assertState(ConnectionState::Idle);
    TEST_ASSERT_EQUAL(0, d.esp.networks().count());
}

// --- esp.networks() ---

void test_a_new_network_goes_on_top()
{
    Device d;
    TEST_ASSERT_TRUE(d.esp.networks().save("Home", "correct horse"));
    TEST_ASSERT_TRUE(d.esp.networks().save("Office", ""));
    TEST_ASSERT_EQUAL(2, d.esp.networks().count());
    TEST_ASSERT_EQUAL_STRING("Office", d.esp.networks().list()[0].ssid.c_str());
    TEST_ASSERT_EQUAL_STRING("Home", d.esp.networks().list()[1].ssid.c_str());
    TEST_ASSERT_TRUE(d.esp.networks().list()[0].dhcp);
}

void test_invalid_networks_are_refused_and_change_nothing()
{
    Device d;
    d.esp.setLogLevel(espmgr::LogLevel::Verbose);
    TEST_ASSERT_TRUE(d.esp.networks().save("Home", "correct horse"));
    TEST_ASSERT_FALSE(d.esp.networks().save("", "correct horse"));
    TEST_ASSERT_FALSE(d.esp.networks().save("Office", "s3cr3t!"));
    d.esp.begin();
    d.esp.process();
    d.fakes.wifi.failConnect(FailureReason::WrongPassword);
    d.esp.process();
    TEST_ASSERT_TRUE(d.fakes.log.lines.size() >= 4);
    TEST_ASSERT_EQUAL_STRING("Home", d.esp.networks().list()[0].ssid.c_str());
    for (const auto& line : d.fakes.log.lines)
    {
        TEST_ASSERT_EQUAL(std::string::npos, line.text.find("s3cr3t!"));
        TEST_ASSERT_EQUAL(std::string::npos, line.text.find("correct horse"));
    }
}

void test_failed_write_keeps_the_old_network()
{
    Device d;
    TEST_ASSERT_TRUE(d.esp.networks().save("Home", "correct horse"));
    d.fakes.storage.writable = false;
    TEST_ASSERT_FALSE(d.esp.networks().save("Office", "battery staple"));
    TEST_ASSERT_EQUAL_STRING("Home", d.esp.networks().list()[0].ssid.c_str());
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_nothing_happens_before_begin);
    RUN_TEST(test_without_a_saved_network_it_stays_idle);
    RUN_TEST(test_begin_returns_without_connecting);
    RUN_TEST(test_connects_to_the_stored_network_after_a_restart);
    RUN_TEST(test_idle_to_connecting_to_connected);
    RUN_TEST(test_failed_attempt_waits_then_retries);
    RUN_TEST(test_attempt_that_never_finishes_times_out);
    RUN_TEST(test_failure_without_a_reason_is_reported_as_other);
    RUN_TEST(test_lost_connection_waits_then_reconnects);
    RUN_TEST(test_network_saved_while_idle_is_used_on_the_next_process);
    RUN_TEST(test_network_saved_while_connected_is_used_from_the_next_attempt);
    RUN_TEST(test_clearing_the_networks_while_waiting_goes_idle);
    RUN_TEST(test_static_addressing_is_applied_to_the_attempt);
    RUN_TEST(test_listeners_see_the_new_state);
    RUN_TEST(test_unreadable_record_counts_as_no_network);
    RUN_TEST(test_a_new_network_goes_on_top);
    RUN_TEST(test_invalid_networks_are_refused_and_change_nothing);
    RUN_TEST(test_failed_write_keeps_the_old_network);
    return UNITY_END();
}
