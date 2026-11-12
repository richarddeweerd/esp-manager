// Network Time (EP18): enabling, servers, time zone, syncing, failures, events and status.

#include <EspManager.h>
#include <FakePlatform.h>
#include <unity.h>

#include <string>
#include <vector>

using espmgr::ConnectionState;
using espmgr::FakePlatform;
using espmgr::LogLevel;
using espmgr::NetworkTime;
using espmgr::TimeSyncInfo;

namespace
{

constexpr int64_t kNov12 = 1794484800;  // 2026-11-12 08:00:00 UTC

// An EspManager on fakes with a Saved Network, connected after connect().
struct Device
{
    FakePlatform fakes;
    EspManager esp{fakes.platform};
    std::vector<TimeSyncInfo> synced;
    int failures = 0;

    Device()
    {
        fakes.wifi.scanStartSucceeds = false;
        esp.setLogLevel(LogLevel::Debug);
        TEST_ASSERT_TRUE(esp.networks().save("Home", "correct horse"));
        esp.onTimeSynced([this](const TimeSyncInfo& info) { synced.push_back(info); });
        esp.onTimeSyncFailed([this] { ++failures; });
    }

    void connect()
    {
        esp.process();
        fakes.wifi.completeConnect(11);
        esp.process();
        TEST_ASSERT_EQUAL(ConnectionState::Connected, esp.state());
    }

    void advance(uint32_t ms)
    {
        fakes.clock.advance(ms);
        esp.process();
    }
};

}  // namespace

void setUp()
{
}

void tearDown()
{
}

void test_network_time_is_off_by_default()
{
    Device d;
    d.esp.begin();
    d.connect();
    d.advance(60000);
    TEST_ASSERT_FALSE(d.esp.ntp().enabled());
    TEST_ASSERT_EQUAL_INT(0, d.fakes.time.starts);
    TEST_ASSERT_EQUAL_INT(0, d.fakes.time.timezoneSets);
    TEST_ASSERT_EQUAL_INT(0, d.failures);
    TEST_ASSERT_FALSE(d.esp.ntp().synchronised());
}

void test_syncing_starts_when_the_station_connects()
{
    Device d;
    d.esp.ntp().enable();
    d.esp.begin();
    d.esp.process();
    TEST_ASSERT_EQUAL_INT(0, d.fakes.time.starts);
    d.connect();
    TEST_ASSERT_EQUAL_INT(1, d.fakes.time.starts);
    TEST_ASSERT_TRUE(d.fakes.time.running);
    const std::vector<std::string> expected{"pool.ntp.org", "time.google.com",
                                            "time.cloudflare.com"};
    TEST_ASSERT_TRUE(expected == d.fakes.time.servers);
    TEST_ASSERT_EQUAL_UINT32(3600000, d.fakes.time.intervalMs);
    TEST_ASSERT_FALSE(d.fakes.time.useDhcp);
    TEST_ASSERT_TRUE(d.fakes.log.contains(
        "syncing from pool.ntp.org, time.google.com, time.cloudflare.com every 3600 s"));
}

void test_enabling_while_connected_starts_at_once()
{
    Device d;
    d.esp.begin();
    d.connect();
    d.esp.ntp().enable();
    d.esp.process();
    TEST_ASSERT_EQUAL_INT(1, d.fakes.time.starts);
}

void test_a_sync_raises_the_event_and_sets_the_state()
{
    Device d;
    d.esp.ntp().enable();
    d.esp.begin();
    d.connect();
    d.fakes.time.answer(kNov12, "time.google.com");
    d.esp.process();
    TEST_ASSERT_TRUE(d.esp.ntp().synchronised());
    TEST_ASSERT_EQUAL_INT64(kNov12, d.esp.ntp().lastSync());
    TEST_ASSERT_EQUAL_STRING("time.google.com", d.esp.ntp().server().c_str());
    TEST_ASSERT_EQUAL_UINT(1, d.synced.size());
    TEST_ASSERT_EQUAL_INT64(kNov12, d.synced[0].epoch);
    TEST_ASSERT_TRUE(d.synced[0].first);
    TEST_ASSERT_TRUE(d.fakes.log.contains("synchronised with time.google.com"));

    d.fakes.time.answer(kNov12 + 3600);
    d.esp.process();
    TEST_ASSERT_EQUAL_UINT(2, d.synced.size());
    TEST_ASSERT_FALSE(d.synced[1].first);
    TEST_ASSERT_EQUAL_INT64(kNov12 + 3600, d.esp.ntp().lastSync());
}

void test_no_answer_in_30_s_is_a_failure_once_per_interval()
{
    Device d;
    d.esp.ntp().enable();
    d.esp.begin();
    d.connect();
    d.advance(29999);
    TEST_ASSERT_EQUAL_INT(0, d.failures);
    d.advance(1);
    TEST_ASSERT_EQUAL_INT(1, d.failures);
    TEST_ASSERT_TRUE(d.fakes.log.contains("no answer from the time servers within 30 s"));
    TEST_ASSERT_TRUE(d.fakes.time.running);
    // Not again until an interval has gone by without an answer.
    d.advance(3599999);
    TEST_ASSERT_EQUAL_INT(1, d.failures);
    d.advance(1);
    TEST_ASSERT_EQUAL_INT(2, d.failures);
    // A late answer still counts.
    d.fakes.time.answer(kNov12);
    d.esp.process();
    TEST_ASSERT_TRUE(d.esp.ntp().synchronised());
    TEST_ASSERT_EQUAL(ConnectionState::Connected, d.esp.state());
}

void test_a_missed_periodic_sync_is_a_failure()
{
    Device d;
    d.esp.ntp().enable();
    d.esp.begin();
    d.connect();
    d.fakes.time.answer(kNov12);
    d.esp.process();
    d.advance(3600000 + 29999);
    TEST_ASSERT_EQUAL_INT(0, d.failures);
    d.advance(1);
    TEST_ASSERT_EQUAL_INT(1, d.failures);
    TEST_ASSERT_TRUE(d.esp.ntp().synchronised());
}

void test_no_failures_while_disconnected_and_a_new_connection_syncs()
{
    Device d;
    d.esp.ntp().enable();
    d.esp.begin();
    d.connect();
    d.fakes.wifi.loseConnection();
    d.esp.process();
    d.advance(60000);
    TEST_ASSERT_EQUAL_INT(0, d.failures);
    // Let the next attempt start, then let it succeed.
    for (int i = 0; i < 600 && d.esp.state() != ConnectionState::Connecting; ++i)
    {
        d.advance(1000);
    }
    d.fakes.wifi.completeConnect(11);
    d.esp.process();
    TEST_ASSERT_EQUAL(ConnectionState::Connected, d.esp.state());
    TEST_ASSERT_EQUAL_INT(1, d.fakes.time.starts);
    TEST_ASSERT_EQUAL_INT(1, d.fakes.time.restarts);
}

void test_sync_now_restarts_the_client()
{
    Device d;
    d.esp.ntp().enable();
    d.esp.begin();
    d.esp.ntp().sync();
    TEST_ASSERT_EQUAL_INT(0, d.fakes.time.restarts);
    d.connect();
    d.esp.ntp().sync();
    TEST_ASSERT_EQUAL_INT(1, d.fakes.time.restarts);
}

void test_disable_stops_the_client_and_keeps_the_state()
{
    Device d;
    d.esp.ntp().enable();
    d.esp.begin();
    d.connect();
    d.fakes.time.answer(kNov12);
    d.esp.process();
    d.esp.ntp().disable();
    TEST_ASSERT_FALSE(d.fakes.time.running);
    TEST_ASSERT_EQUAL_INT(1, d.fakes.time.stops);
    TEST_ASSERT_TRUE(d.esp.ntp().synchronised());
    d.advance(10UL * 3600 * 1000);
    TEST_ASSERT_EQUAL_INT(0, d.failures);
    // An answer that arrives while off isn't reported later.
    d.fakes.time.synced = true;
    d.esp.process();
    d.esp.ntp().enable();
    d.esp.process();
    TEST_ASSERT_EQUAL_UINT(1, d.synced.size());
    TEST_ASSERT_EQUAL_INT(2, d.fakes.time.starts);
}

void test_servers_can_be_replaced_and_added()
{
    Device d;
    NetworkTime& ntp = d.esp.ntp();
    TEST_ASSERT_TRUE(ntp.setServer("ntp.example.com"));
    TEST_ASSERT_EQUAL_UINT(3, ntp.servers().size());
    TEST_ASSERT_EQUAL_STRING("ntp.example.com", ntp.servers()[0].c_str());
    TEST_ASSERT_TRUE(ntp.addFallbackServer("192.168.1.1"));
    TEST_ASSERT_EQUAL_UINT(2, ntp.servers().size());
    TEST_ASSERT_TRUE(ntp.addFallbackServer("time.cloudflare.com"));
    TEST_ASSERT_FALSE(ntp.addFallbackServer("one.too.many"));
    TEST_ASSERT_TRUE(d.fakes.log.contains("at most 3 time servers"));
    const std::vector<std::string> expected{"ntp.example.com", "192.168.1.1",
                                            "time.cloudflare.com"};
    TEST_ASSERT_TRUE(expected == ntp.servers());
}

void test_invalid_server_names_are_refused()
{
    Device d;
    NetworkTime& ntp = d.esp.ntp();
    TEST_ASSERT_FALSE(ntp.setServer(""));
    TEST_ASSERT_FALSE(ntp.setServer("has space"));
    TEST_ASSERT_FALSE(ntp.setServer(std::string(65, 'a')));
    TEST_ASSERT_FALSE(ntp.addFallbackServer("tab\there"));
    TEST_ASSERT_TRUE(ntp.setServer(std::string(64, 'a')));
    TEST_ASSERT_EQUAL_UINT(3, ntp.servers().size());
    TEST_ASSERT_TRUE(d.fakes.log.contains("printable characters without spaces"));
}

void test_changing_servers_while_running_restarts_the_client()
{
    Device d;
    d.esp.ntp().enable();
    d.esp.begin();
    d.connect();
    TEST_ASSERT_TRUE(d.esp.ntp().setServer("ntp.example.com"));
    TEST_ASSERT_EQUAL_INT(2, d.fakes.time.starts);
    TEST_ASSERT_EQUAL_STRING("ntp.example.com", d.fakes.time.servers[0].c_str());
    d.esp.ntp().useDhcpServer(true);
    TEST_ASSERT_EQUAL_INT(3, d.fakes.time.starts);
    TEST_ASSERT_TRUE(d.fakes.time.useDhcp);
}

void test_the_time_zone_applies_at_once_also_while_off()
{
    Device d;
    TEST_ASSERT_EQUAL_STRING("UTC0", d.esp.ntp().timezone().c_str());
    TEST_ASSERT_TRUE(d.esp.ntp().setTimezone("CET-1CEST,M3.5.0,M10.5.0/3"));
    TEST_ASSERT_EQUAL_STRING("CET-1CEST,M3.5.0,M10.5.0/3", d.fakes.time.timezone.c_str());
    TEST_ASSERT_EQUAL_STRING("CET-1CEST,M3.5.0,M10.5.0/3", d.esp.ntp().timezone().c_str());
    TEST_ASSERT_FALSE(d.esp.ntp().setTimezone(""));
    TEST_ASSERT_FALSE(d.esp.ntp().setTimezone(std::string(65, 'A')));
    TEST_ASSERT_FALSE(d.esp.ntp().setTimezone("CET-1\n"));
    TEST_ASSERT_EQUAL_STRING("CET-1CEST,M3.5.0,M10.5.0/3", d.esp.ntp().timezone().c_str());
    TEST_ASSERT_TRUE(d.fakes.log.contains("a time zone must be 1 to 64 printable characters"));
}

void test_the_sync_interval_has_a_minimum()
{
    Device d;
    TEST_ASSERT_FALSE(d.esp.ntp().setSyncInterval(14999));
    TEST_ASSERT_EQUAL_UINT32(3600000, d.esp.ntp().syncInterval());
    TEST_ASSERT_TRUE(d.esp.ntp().setSyncInterval(15000));
    d.esp.ntp().enable();
    d.esp.begin();
    d.connect();
    TEST_ASSERT_EQUAL_UINT32(15000, d.fakes.time.intervalMs);
}

void test_status_has_the_time()
{
    Device d;
    d.esp.ntp().enable().setTimezone("CET-1CEST,M3.5.0,M10.5.0/3");
    d.esp.begin();
    d.connect();
    espmgr::DeviceStatus status = d.esp.status();
    TEST_ASSERT_TRUE(status.time.enabled);
    TEST_ASSERT_FALSE(status.time.synchronised);
    TEST_ASSERT_EQUAL_INT64(0, status.time.epoch);
    TEST_ASSERT_EQUAL_STRING("", status.time.localTime.c_str());
    TEST_ASSERT_EQUAL_STRING("CET-1CEST,M3.5.0,M10.5.0/3", status.time.posix.c_str());

    d.fakes.time.answer(kNov12);
    d.esp.process();
    d.fakes.time.epoch = kNov12 + 60;
    status = d.esp.status();
    TEST_ASSERT_TRUE(status.time.synchronised);
    TEST_ASSERT_EQUAL_INT64(kNov12 + 60, status.time.epoch);
    TEST_ASSERT_EQUAL_INT64(kNov12, status.time.lastSync);
    TEST_ASSERT_EQUAL_STRING("1794484860 CET-1CEST,M3.5.0,M10.5.0/3",
                             status.time.localTime.c_str());
    TEST_ASSERT_EQUAL_STRING("pool.ntp.org", status.time.server.c_str());
}

void test_nothing_syncs_before_begin()
{
    Device d;
    d.esp.ntp().enable();
    d.esp.process();
    TEST_ASSERT_EQUAL_INT(0, d.fakes.time.starts);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_network_time_is_off_by_default);
    RUN_TEST(test_syncing_starts_when_the_station_connects);
    RUN_TEST(test_enabling_while_connected_starts_at_once);
    RUN_TEST(test_a_sync_raises_the_event_and_sets_the_state);
    RUN_TEST(test_no_answer_in_30_s_is_a_failure_once_per_interval);
    RUN_TEST(test_a_missed_periodic_sync_is_a_failure);
    RUN_TEST(test_no_failures_while_disconnected_and_a_new_connection_syncs);
    RUN_TEST(test_sync_now_restarts_the_client);
    RUN_TEST(test_disable_stops_the_client_and_keeps_the_state);
    RUN_TEST(test_servers_can_be_replaced_and_added);
    RUN_TEST(test_invalid_server_names_are_refused);
    RUN_TEST(test_changing_servers_while_running_restarts_the_client);
    RUN_TEST(test_the_time_zone_applies_at_once_also_while_off);
    RUN_TEST(test_the_sync_interval_has_a_minimum);
    RUN_TEST(test_status_has_the_time);
    RUN_TEST(test_nothing_syncs_before_begin);
    return UNITY_END();
}
