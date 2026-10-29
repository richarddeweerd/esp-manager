#include <EspManager.h>
#include <FakePlatform.h>
#include <unity.h>

#include <string>
#include <vector>

using espmgr::ConnectionState;
using espmgr::FailureReason;
using espmgr::FakePlatform;
using espmgr::NetworkScanner;
using espmgr::SavedNetwork;
using espmgr::ScanResult;
using espmgr::WifiConnection;

namespace
{

constexpr uint32_t kMin = WifiConnection::kDefaultMinRetryDelayMs;
constexpr uint32_t kMax = WifiConnection::kDefaultMaxRetryDelayMs;

SavedNetwork network(const std::string& ssid, uint8_t priority)
{
    SavedNetwork n;
    n.ssid = ssid;
    n.password = "password " + ssid;
    n.priority = priority;
    return n;
}

ScanResult seen(const std::string& ssid, int8_t rssi)
{
    return {ssid, rssi, 6, true};
}

struct Device
{
    FakePlatform fakes;
    EspManager esp{fakes.platform};
    std::vector<std::string> failures;

    Device()
    {
        esp.onConnectFailed([this](const std::string& ssid, FailureReason reason)
                            { failures.push_back(ssid + ": " + espmgr::toString(reason)); });
        TEST_ASSERT_TRUE(esp.networks().save("Home", "correct horse"));
        esp.begin();
        esp.process();  // starts the first attempt's scan
    }

    // Ends the running scan with Home in range and lets the attempt connect.
    void scanFindsHome()
    {
        TEST_ASSERT_FALSE_MESSAGE(fakes.wifi.scanComplete(), "no scan running");
        fakes.wifi.completeScan({seen("Home", -60)});
        esp.process();
    }

    // Fails the attempt in progress and returns how long the retry waits.
    uint64_t failAndMeasureWait(FailureReason reason = FailureReason::WrongPassword)
    {
        scanFindsHome();
        fakes.wifi.failConnect(reason);
        esp.process();
        TEST_ASSERT_EQUAL(ConnectionState::Waiting, esp.state());
        uint64_t waited = 0;
        while (esp.state() == ConnectionState::Waiting)
        {
            fakes.clock.advance(100);
            waited += 100;
            esp.process();
            TEST_ASSERT_TRUE_MESSAGE(waited <= kMax, "waited longer than the maximum");
        }
        return waited;
    }
};

}  // namespace

void setUp()
{
}

void tearDown()
{
}

// --- Scanning and choosing ---

void test_each_attempt_scans_before_connecting()
{
    Device d;
    TEST_ASSERT_EQUAL(ConnectionState::Connecting, d.esp.state());
    TEST_ASSERT_EQUAL(1, d.fakes.wifi.scansStarted);
    TEST_ASSERT_EQUAL(0, d.fakes.wifi.connectCalls.size());
    d.esp.process();
    TEST_ASSERT_EQUAL(0, d.fakes.wifi.connectCalls.size());

    d.scanFindsHome();
    TEST_ASSERT_EQUAL(1, d.fakes.wifi.connectCalls.size());
    TEST_ASSERT_EQUAL_STRING("Home", d.fakes.wifi.connectCalls[0].ssid.c_str());

    d.fakes.wifi.failConnect(FailureReason::WrongPassword);
    d.esp.process();
    d.fakes.clock.advance(kMin);
    d.esp.process();
    TEST_ASSERT_EQUAL(2, d.fakes.wifi.scansStarted);
}

void test_chooses_the_lowest_priority_number_in_range()
{
    const std::vector<SavedNetwork> saved{network("Cafe", 5), network("Home", 1),
                                          network("Office", 2)};
    const auto chosen =
        WifiConnection::choose(saved, {seen("Cafe", -40), seen("Office", -80), seen("Other", -30)});
    TEST_ASSERT_EQUAL_STRING("Office", chosen.ssid.c_str());
}

void test_signal_strength_breaks_ties()
{
    const std::vector<SavedNetwork> saved{network("Home", 1), network("Office", 1)};
    TEST_ASSERT_EQUAL_STRING(
        "Office",
        WifiConnection::choose(saved, {seen("Home", -75), seen("Office", -50)}).ssid.c_str());
    TEST_ASSERT_EQUAL_STRING(
        "Home",
        WifiConnection::choose(saved, {seen("Home", -40), seen("Office", -50)}).ssid.c_str());
}

void test_strongest_access_point_of_an_ssid_counts()
{
    const std::vector<SavedNetwork> saved{network("Home", 1), network("Office", 1)};
    const auto chosen =
        WifiConnection::choose(saved, {seen("Home", -85), seen("Office", -60), seen("Home", -45)});
    TEST_ASSERT_EQUAL_STRING("Home", chosen.ssid.c_str());
}

void test_tries_the_first_network_when_none_is_in_range()
{
    // A hidden network never shows up in a scan, so it still gets an attempt.
    const std::vector<SavedNetwork> saved{network("Office", 3), network("Hidden", 0)};
    TEST_ASSERT_EQUAL_STRING("Hidden",
                             WifiConnection::choose(saved, {seen("Other", -30)}).ssid.c_str());
    TEST_ASSERT_EQUAL_STRING("Hidden", WifiConnection::choose(saved, {}).ssid.c_str());
}

void test_connects_without_a_scan_when_it_cannot_start()
{
    FakePlatform fakes;
    fakes.wifi.scanStartSucceeds = false;
    EspManager esp(fakes.platform);
    TEST_ASSERT_TRUE(esp.networks().save("Home", "correct horse"));
    esp.begin();
    esp.process();
    TEST_ASSERT_EQUAL(1, fakes.wifi.connectCalls.size());
}

void test_scan_that_never_finishes_is_given_up()
{
    Device d;
    d.fakes.clock.advance(NetworkScanner::kScanTimeoutMs);
    d.esp.process();
    // Without results it tries the first network by priority, as for a hidden network.
    TEST_ASSERT_EQUAL(ConnectionState::Connecting, d.esp.state());
    TEST_ASSERT_EQUAL(1, d.fakes.wifi.connectCalls.size());

    // The scan counts towards the attempt's time.
    d.fakes.clock.advance(WifiConnection::kAttemptTimeoutMs - NetworkScanner::kScanTimeoutMs);
    d.esp.process();
    TEST_ASSERT_EQUAL(ConnectionState::Waiting, d.esp.state());
    TEST_ASSERT_EQUAL(FailureReason::Timeout, d.esp.lastFailure());
}

void test_networks_cleared_during_the_scan_go_idle()
{
    Device d;
    TEST_ASSERT_TRUE(d.esp.networks().clear());
    d.fakes.wifi.completeScan({seen("Home", -60)});
    d.esp.process();
    TEST_ASSERT_EQUAL(ConnectionState::Idle, d.esp.state());
    TEST_ASSERT_EQUAL(0, d.fakes.wifi.connectCalls.size());
}

// --- Backoff ---

void test_base_delay_doubles_up_to_the_maximum()
{
    Device d;
    const uint64_t expected[] = {1000,  2000,   4000,   8000,   16000, 32000,
                                 64000, 128000, 256000, 300000, 300000};
    espmgr::Logger log(d.fakes.log, d.fakes.clock);
    espmgr::Events events(log);
    NetworkScanner scanner(d.fakes.wifi, d.fakes.clock, log);
    WifiConnection connection(d.fakes.wifi, d.fakes.clock, scanner, d.esp.networks(), events, log);
    for (uint32_t failures = 1; failures <= 11; ++failures)
    {
        TEST_ASSERT_EQUAL_UINT64(expected[failures - 1], connection.baseRetryDelay(failures));
    }
    TEST_ASSERT_EQUAL_UINT64(kMax, connection.baseRetryDelay(1000));
}

void test_retries_wait_longer_after_each_failure_in_a_row()
{
    Device d;
    TEST_ASSERT_EQUAL_UINT64(1000, d.failAndMeasureWait());
    TEST_ASSERT_EQUAL_UINT64(2000, d.failAndMeasureWait());
    TEST_ASSERT_EQUAL_UINT64(4000, d.failAndMeasureWait());
    TEST_ASSERT_EQUAL_UINT64(8000, d.failAndMeasureWait(FailureReason::NetworkNotFound));
    TEST_ASSERT_EQUAL(4, d.failures.size());
    TEST_ASSERT_EQUAL_STRING("Home: network not found", d.failures.back().c_str());
}

void test_random_variation_stays_between_75_and_100_percent()
{
    Device d;
    d.failAndMeasureWait();  // 1 s
    d.failAndMeasureWait();  // 2 s
    // Third failure: base 4 s, so up to 1 s less. The fake returns the queued value.
    d.fakes.clock.randomValues = {1000};
    TEST_ASSERT_EQUAL_UINT64(3000, d.failAndMeasureWait());
    d.fakes.clock.randomValues = {500};
    TEST_ASSERT_EQUAL_UINT64(7500, d.failAndMeasureWait());  // base 8 s minus 500 ms
}

void test_variation_never_goes_below_the_minimum()
{
    Device d;
    d.fakes.clock.randomValues = {250};
    TEST_ASSERT_EQUAL_UINT64(1000, d.failAndMeasureWait());
}

void test_successful_connection_resets_the_backoff()
{
    Device d;
    d.failAndMeasureWait();
    d.failAndMeasureWait();
    d.failAndMeasureWait();
    d.scanFindsHome();
    d.fakes.wifi.completeConnect();
    d.esp.process();
    TEST_ASSERT_EQUAL(ConnectionState::Connected, d.esp.state());

    d.fakes.wifi.loseConnection();
    d.esp.process();
    TEST_ASSERT_EQUAL(ConnectionState::Waiting, d.esp.state());
    d.fakes.clock.advance(kMin);
    d.esp.process();
    TEST_ASSERT_EQUAL(ConnectionState::Connecting, d.esp.state());
}

void test_retry_delays_can_be_changed()
{
    Device d;
    TEST_ASSERT_TRUE(d.esp.setRetryDelays(500, 1500));
    TEST_ASSERT_EQUAL_UINT64(500, d.failAndMeasureWait());
    TEST_ASSERT_EQUAL_UINT64(1000, d.failAndMeasureWait());
    TEST_ASSERT_EQUAL_UINT64(1500, d.failAndMeasureWait());
    TEST_ASSERT_EQUAL_UINT64(1500, d.failAndMeasureWait());
}

void test_invalid_retry_delays_are_refused()
{
    Device d;
    TEST_ASSERT_FALSE(d.esp.setRetryDelays(0, 1000));
    TEST_ASSERT_FALSE(d.esp.setRetryDelays(2000, 1000));
    TEST_ASSERT_EQUAL_UINT64(1000, d.failAndMeasureWait());
}

// --- Last failure reason ---

void test_last_failure_is_none_until_something_fails()
{
    Device d;
    TEST_ASSERT_EQUAL(FailureReason::None, d.esp.lastFailure());
    d.scanFindsHome();
    d.fakes.wifi.completeConnect();
    d.esp.process();
    TEST_ASSERT_EQUAL(FailureReason::None, d.esp.lastFailure());
}

void test_last_failure_follows_each_failure_and_is_kept_after_reconnecting()
{
    Device d;
    d.scanFindsHome();
    d.fakes.wifi.failConnect(FailureReason::WrongPassword);
    d.esp.process();
    TEST_ASSERT_EQUAL(FailureReason::WrongPassword, d.esp.lastFailure());

    d.fakes.clock.advance(kMin);
    d.esp.process();
    d.scanFindsHome();
    d.fakes.wifi.completeConnect();
    d.esp.process();
    TEST_ASSERT_EQUAL(FailureReason::WrongPassword, d.esp.lastFailure());

    d.fakes.wifi.loseConnection();
    d.esp.process();
    TEST_ASSERT_EQUAL(FailureReason::ConnectionLost, d.esp.lastFailure());
}

void test_failure_reasons_have_readable_names()
{
    TEST_ASSERT_EQUAL_STRING("wrong password", espmgr::toString(FailureReason::WrongPassword));
    TEST_ASSERT_EQUAL_STRING("network not found", espmgr::toString(FailureReason::NetworkNotFound));
    TEST_ASSERT_EQUAL_STRING("timed out", espmgr::toString(FailureReason::Timeout));
    TEST_ASSERT_EQUAL_STRING("connection lost", espmgr::toString(FailureReason::ConnectionLost));
    TEST_ASSERT_EQUAL_STRING("other", espmgr::toString(FailureReason::Other));
    TEST_ASSERT_EQUAL_STRING("none", espmgr::toString(FailureReason::None));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_each_attempt_scans_before_connecting);
    RUN_TEST(test_chooses_the_lowest_priority_number_in_range);
    RUN_TEST(test_signal_strength_breaks_ties);
    RUN_TEST(test_strongest_access_point_of_an_ssid_counts);
    RUN_TEST(test_tries_the_first_network_when_none_is_in_range);
    RUN_TEST(test_connects_without_a_scan_when_it_cannot_start);
    RUN_TEST(test_scan_that_never_finishes_is_given_up);
    RUN_TEST(test_networks_cleared_during_the_scan_go_idle);
    RUN_TEST(test_base_delay_doubles_up_to_the_maximum);
    RUN_TEST(test_retries_wait_longer_after_each_failure_in_a_row);
    RUN_TEST(test_random_variation_stays_between_75_and_100_percent);
    RUN_TEST(test_variation_never_goes_below_the_minimum);
    RUN_TEST(test_successful_connection_resets_the_backoff);
    RUN_TEST(test_retry_delays_can_be_changed);
    RUN_TEST(test_invalid_retry_delays_are_refused);
    RUN_TEST(test_last_failure_is_none_until_something_fails);
    RUN_TEST(test_last_failure_follows_each_failure_and_is_kept_after_reconnecting);
    RUN_TEST(test_failure_reasons_have_readable_names);
    return UNITY_END();
}
