#include <EspManager.h>
#include <FakePlatform.h>
#include <unity.h>

#include <string>
#include <vector>

using espmgr::ConnectionState;
using espmgr::FailureReason;
using espmgr::FakeHttpServer;
using espmgr::FakePlatform;
using espmgr::HttpMethod;
using espmgr::Ipv4Address;
using espmgr::PortalState;
using espmgr::ProvisioningPortal;
using espmgr::SavedNetwork;
using espmgr::ScanResult;

namespace
{

constexpr uint32_t kTimeout = ProvisioningPortal::kDefaultTimeoutMs;

ScanResult seen(const std::string& ssid, int8_t rssi, uint8_t channel)
{
    ScanResult result;
    result.ssid = ssid;
    result.rssi = rssi;
    result.channel = channel;
    result.secured = true;
    return result;
}

// An EspManager on fakes that records the portal events as text.
struct Device
{
    FakePlatform fakes;
    EspManager esp{fakes.platform};
    std::vector<std::string> events;

    Device()
    {
        esp.onPortalStarted([this](const std::string& ssid)
                            { events.push_back("started " + ssid); })
            .onPortalStopped([this] { events.push_back("stopped"); });
    }

    // begin(), then the first process(), which starts the portal's scan when there's no network.
    void begin()
    {
        esp.begin();
        esp.process();
    }

    // Finishes the running scan with `results` and lets process() see it.
    void finishScan(std::vector<ScanResult> results = {})
    {
        fakes.wifi.completeScan(std::move(results));
        esp.process();
    }

    FakeHttpServer::Response get(const std::string& path)
    {
        return fakes.http.request(HttpMethod::Get, path);
    }
};

}  // namespace

void setUp()
{
}

void tearDown()
{
}

// --- When it opens ---

void test_opens_by_itself_without_a_saved_network()
{
    Device d;
    d.begin();
    // The scan for the channel comes first.
    TEST_ASSERT_EQUAL(1, d.fakes.wifi.scansStarted);
    TEST_ASSERT_EQUAL(PortalState::Off, d.esp.portalState());
    TEST_ASSERT_FALSE(d.fakes.wifi.accessPointActive);

    d.finishScan({seen("Neighbours", -70, 11)});
    TEST_ASSERT_EQUAL(PortalState::Active, d.esp.portalState());
    TEST_ASSERT_TRUE(d.fakes.wifi.accessPointActive);
    TEST_ASSERT_EQUAL_STRING("esp-abcdef", d.fakes.wifi.accessPointSsid.c_str());
    TEST_ASSERT_EQUAL_STRING("", d.fakes.wifi.accessPointPassword.c_str());
    TEST_ASSERT_EQUAL_UINT8(11, d.fakes.wifi.channel());
    TEST_ASSERT_TRUE(d.fakes.http.running);
    TEST_ASSERT_EQUAL_UINT16(80, d.fakes.http.port);
    TEST_ASSERT_EQUAL(1, d.events.size());
    TEST_ASSERT_EQUAL_STRING("started esp-abcdef", d.events[0].c_str());
}

void test_nothing_happens_before_begin()
{
    Device d;
    d.esp.process();
    d.esp.process();
    TEST_ASSERT_EQUAL(0, d.fakes.wifi.scansStarted);
    TEST_ASSERT_EQUAL(PortalState::Off, d.esp.portalState());
}

void test_stays_closed_with_a_saved_network()
{
    Device d;
    TEST_ASSERT_TRUE(d.esp.networks().save("Home", "correct horse"));
    d.begin();
    d.finishScan({seen("Home", -50, 6)});
    TEST_ASSERT_EQUAL(PortalState::Off, d.esp.portalState());
    TEST_ASSERT_FALSE(d.fakes.wifi.accessPointActive);
    TEST_ASSERT_TRUE(d.events.empty());
}

void test_start_portal_opens_it_with_a_saved_network_too()
{
    Device d;
    TEST_ASSERT_TRUE(d.esp.networks().save("Home", "correct horse"));
    d.begin();
    d.esp.startPortal();
    // Returns at once; process() does the work.
    TEST_ASSERT_EQUAL(PortalState::Off, d.esp.portalState());
    d.esp.process();
    // Shares the connection attempt's scan.
    TEST_ASSERT_EQUAL(1, d.fakes.wifi.scansStarted);
    d.finishScan({seen("Home", -50, 6)});
    TEST_ASSERT_EQUAL(PortalState::Active, d.esp.portalState());
    TEST_ASSERT_EQUAL_UINT8(6, d.fakes.wifi.channel());
}

void test_start_portal_before_begin_waits_for_it()
{
    Device d;
    TEST_ASSERT_TRUE(d.esp.networks().save("Home", "correct horse"));
    d.esp.startPortal();
    d.esp.process();
    TEST_ASSERT_EQUAL(0, d.fakes.wifi.scansStarted);
    d.begin();
    d.finishScan();
    TEST_ASSERT_EQUAL(PortalState::Active, d.esp.portalState());
}

void test_while_connected_it_opens_on_the_stations_channel_without_a_scan()
{
    Device d;
    d.fakes.wifi.scanStartSucceeds = false;
    TEST_ASSERT_TRUE(d.esp.networks().save("Home", "correct horse"));
    d.begin();
    d.fakes.wifi.completeConnect(9);
    d.esp.process();
    TEST_ASSERT_EQUAL(ConnectionState::Connected, d.esp.state());

    d.fakes.wifi.scanStartSucceeds = true;
    d.esp.startPortal();
    d.esp.process();
    TEST_ASSERT_EQUAL(0, d.fakes.wifi.scansStarted);
    TEST_ASSERT_EQUAL(PortalState::Active, d.esp.portalState());
    TEST_ASSERT_EQUAL_UINT8(9, d.fakes.wifi.channel());
}

void test_opens_at_once_when_the_scan_cannot_start()
{
    Device d;
    d.fakes.wifi.scanStartSucceeds = false;
    d.begin();
    TEST_ASSERT_EQUAL(PortalState::Active, d.esp.portalState());
    TEST_ASSERT_EQUAL_UINT8(ProvisioningPortal::kDefaultChannel, d.fakes.wifi.channel());
}

void test_access_point_that_fails_to_start_leaves_it_closed()
{
    Device d;
    d.fakes.wifi.accessPointStartSucceeds = false;
    d.begin();
    d.finishScan();
    TEST_ASSERT_EQUAL(PortalState::Off, d.esp.portalState());
    TEST_ASSERT_FALSE(d.fakes.http.running);
    TEST_ASSERT_TRUE(d.events.empty());
}

// --- Channel ---

void test_channel_of_the_strongest_saved_network_wins()
{
    SavedNetwork home;
    home.ssid = "Home";
    const std::vector<SavedNetwork> saved{home};
    const std::vector<ScanResult> scan{seen("Neighbours", -40, 1), seen("Home", -80, 6),
                                       seen("Home", -60, 11)};
    TEST_ASSERT_EQUAL_UINT8(11, ProvisioningPortal::chooseChannel(saved, scan));
}

void test_channel_of_the_strongest_network_without_a_saved_one()
{
    const std::vector<ScanResult> scan{seen("A", -70, 1), seen("B", -40, 6), seen("C", -60, 11)};
    TEST_ASSERT_EQUAL_UINT8(6, ProvisioningPortal::chooseChannel({}, scan));
}

void test_default_channel_when_the_scan_finds_nothing()
{
    TEST_ASSERT_EQUAL_UINT8(ProvisioningPortal::kDefaultChannel,
                            ProvisioningPortal::chooseChannel({}, {}));
    TEST_ASSERT_EQUAL_UINT8(ProvisioningPortal::kDefaultChannel,
                            ProvisioningPortal::chooseChannel({}, {seen("No channel", -40, 0)}));
}

// --- Closing ---

void test_stop_portal_closes_it_and_it_stays_closed()
{
    Device d;
    d.begin();
    d.finishScan();
    d.esp.stopPortal();
    d.esp.process();
    TEST_ASSERT_EQUAL(PortalState::Off, d.esp.portalState());
    TEST_ASSERT_FALSE(d.fakes.wifi.accessPointActive);
    TEST_ASSERT_FALSE(d.fakes.http.running);
    TEST_ASSERT_EQUAL(2, d.events.size());
    TEST_ASSERT_EQUAL_STRING("stopped", d.events[1].c_str());

    // Still no Saved Network, but the Developer closed it.
    d.fakes.clock.advance(kTimeout * 2);
    d.esp.process();
    TEST_ASSERT_EQUAL(PortalState::Off, d.esp.portalState());
}

void test_stop_portal_while_starting_cancels_it()
{
    Device d;
    d.begin();
    d.esp.stopPortal();
    d.finishScan();
    TEST_ASSERT_EQUAL(PortalState::Off, d.esp.portalState());
    TEST_ASSERT_FALSE(d.fakes.wifi.accessPointActive);
    TEST_ASSERT_TRUE(d.events.empty());
}

void test_timeout_without_a_network_reopens_it()
{
    Device d;
    d.begin();
    d.finishScan();
    d.fakes.clock.advance(kTimeout);
    d.esp.process();
    TEST_ASSERT_EQUAL(PortalState::Off, d.esp.portalState());
    TEST_ASSERT_EQUAL(2, d.fakes.wifi.scansStarted);
    d.finishScan();
    TEST_ASSERT_EQUAL(PortalState::Active, d.esp.portalState());
    TEST_ASSERT_EQUAL(3, d.events.size());
    TEST_ASSERT_EQUAL_STRING("stopped", d.events[1].c_str());
    TEST_ASSERT_EQUAL_STRING("started esp-abcdef", d.events[2].c_str());
}

void test_timeout_without_restart_leaves_it_closed()
{
    Device d;
    d.esp.portal().setRestartOnTimeout(false);
    d.begin();
    d.finishScan();
    d.fakes.clock.advance(kTimeout);
    d.esp.process();
    d.fakes.clock.advance(kTimeout);
    d.esp.process();
    TEST_ASSERT_EQUAL(PortalState::Off, d.esp.portalState());
    TEST_ASSERT_EQUAL(1, d.fakes.wifi.scansStarted);

    // startPortal() still opens it.
    d.esp.startPortal();
    d.esp.process();
    d.finishScan();
    TEST_ASSERT_EQUAL(PortalState::Active, d.esp.portalState());
}

void test_timeout_with_a_saved_network_closes_it()
{
    Device d;
    d.fakes.wifi.scanStartSucceeds = false;
    TEST_ASSERT_TRUE(d.esp.networks().save("Home", "correct horse"));
    d.begin();
    d.esp.startPortal();
    d.esp.process();
    TEST_ASSERT_EQUAL(PortalState::Active, d.esp.portalState());
    d.fakes.clock.advance(kTimeout);
    d.esp.process();
    TEST_ASSERT_EQUAL(PortalState::Off, d.esp.portalState());
    TEST_ASSERT_FALSE(d.fakes.wifi.accessPointActive);
}

void test_each_request_restarts_the_timeout()
{
    Device d;
    d.begin();
    d.finishScan();
    for (int i = 0; i < 5; ++i)
    {
        d.fakes.clock.advance(kTimeout - 1000);
        d.get("/");
        d.esp.process();
    }
    TEST_ASSERT_EQUAL(PortalState::Active, d.esp.portalState());
    d.fakes.clock.advance(kTimeout);
    d.esp.process();
    TEST_ASSERT_EQUAL(PortalState::Off, d.esp.portalState());
}

void test_timeout_zero_keeps_it_open()
{
    Device d;
    d.esp.portal().setTimeout(0);
    d.begin();
    d.finishScan();
    d.fakes.clock.advance(kTimeout * 100);
    d.esp.process();
    TEST_ASSERT_EQUAL(PortalState::Active, d.esp.portalState());
}

// --- Opening after failing to connect ---

void test_opens_after_failing_for_the_set_time_and_closes_once_connected()
{
    Device d;
    d.fakes.wifi.scanStartSucceeds = false;
    TEST_ASSERT_TRUE(d.esp.networks().save("Home", "wrong password"));
    d.esp.portal().setOpenAfterFailingFor(60000);
    d.begin();
    d.fakes.wifi.failConnect(FailureReason::WrongPassword);
    d.esp.process();

    d.fakes.clock.advance(59000);
    d.esp.process();
    TEST_ASSERT_EQUAL(PortalState::Off, d.esp.portalState());
    d.fakes.clock.advance(1000);
    d.esp.process();
    TEST_ASSERT_EQUAL(PortalState::Active, d.esp.portalState());

    // The retries go on meanwhile; once one succeeds the portal closes.
    TEST_ASSERT_EQUAL(ConnectionState::Connecting, d.esp.state());
    d.fakes.wifi.completeConnect();
    d.esp.process();
    TEST_ASSERT_EQUAL(ConnectionState::Connected, d.esp.state());
    TEST_ASSERT_EQUAL(PortalState::Off, d.esp.portalState());
}

void test_opened_by_start_portal_it_stays_open_once_connected()
{
    Device d;
    d.fakes.wifi.scanStartSucceeds = false;
    TEST_ASSERT_TRUE(d.esp.networks().save("Home", "correct horse"));
    d.begin();
    d.esp.startPortal();
    d.esp.process();
    d.fakes.wifi.completeConnect();
    d.esp.process();
    TEST_ASSERT_EQUAL(ConnectionState::Connected, d.esp.state());
    TEST_ASSERT_EQUAL(PortalState::Active, d.esp.portalState());
}

void test_never_opens_after_failing_by_default()
{
    Device d;
    d.fakes.wifi.scanStartSucceeds = false;
    TEST_ASSERT_TRUE(d.esp.networks().save("Home", "wrong password"));
    d.begin();
    for (int i = 0; i < 20; ++i)
    {
        d.fakes.wifi.failConnect(FailureReason::WrongPassword);
        d.fakes.clock.advance(5 * 60 * 1000);
        d.esp.process();
    }
    TEST_ASSERT_EQUAL(PortalState::Off, d.esp.portalState());
}

// --- Settings ---

void test_ssid_follows_the_device_name()
{
    Device d;
    TEST_ASSERT_TRUE(d.esp.setNamePrefix("lamp"));
    TEST_ASSERT_EQUAL_STRING("lamp-abcdef", d.esp.portal().ssid().c_str());
    TEST_ASSERT_TRUE(d.esp.setHostname("kitchen"));
    TEST_ASSERT_EQUAL_STRING("kitchen", d.esp.portal().ssid().c_str());
}

void test_ssid_can_be_set()
{
    Device d;
    TEST_ASSERT_TRUE(d.esp.portal().setSsid("Lamp setup"));
    d.begin();
    d.finishScan();
    TEST_ASSERT_EQUAL_STRING("Lamp setup", d.fakes.wifi.accessPointSsid.c_str());
    TEST_ASSERT_EQUAL_STRING("started Lamp setup", d.events[0].c_str());
}

void test_ssid_longer_than_32_bytes_is_refused()
{
    Device d;
    TEST_ASSERT_TRUE(d.esp.portal().setSsid(std::string(32, 's')));
    TEST_ASSERT_FALSE(d.esp.portal().setSsid(std::string(33, 's')));
    TEST_ASSERT_EQUAL_STRING(std::string(32, 's').c_str(), d.esp.portal().ssid().c_str());
    // Empty goes back to the device name.
    TEST_ASSERT_TRUE(d.esp.portal().setSsid(""));
    TEST_ASSERT_EQUAL_STRING("esp-abcdef", d.esp.portal().ssid().c_str());
}

void test_password_protects_the_access_point()
{
    Device d;
    TEST_ASSERT_TRUE(d.esp.portal().setPassword("12345678"));
    d.begin();
    d.finishScan();
    TEST_ASSERT_EQUAL_STRING("12345678", d.fakes.wifi.accessPointPassword.c_str());
}

void test_invalid_passwords_are_refused_and_change_nothing()
{
    Device d;
    TEST_ASSERT_TRUE(d.esp.portal().setPassword("12345678"));
    TEST_ASSERT_FALSE(d.esp.portal().setPassword("1234567"));
    TEST_ASSERT_FALSE(d.esp.portal().setPassword(std::string(64, 'p')));
    TEST_ASSERT_FALSE(d.esp.portal().setPassword("tab\tinside"));
    TEST_ASSERT_TRUE(d.esp.portal().setPassword(std::string(63, 'p')));
    d.begin();
    d.finishScan();
    TEST_ASSERT_EQUAL_STRING(std::string(63, 'p').c_str(),
                             d.fakes.wifi.accessPointPassword.c_str());
}

void test_password_is_never_logged()
{
    Device d;
    d.esp.setLogLevel(espmgr::LogLevel::Verbose);
    TEST_ASSERT_TRUE(d.esp.portal().setPassword("s3cr3t!pw"));
    TEST_ASSERT_FALSE(d.esp.portal().setPassword("s3cr3t!"));
    d.begin();
    d.finishScan();
    TEST_ASSERT_FALSE(d.fakes.log.lines.empty());
    for (const auto& line : d.fakes.log.lines)
    {
        TEST_ASSERT_TRUE_MESSAGE(line.text.find("s3cr3t") == std::string::npos, line.text.c_str());
    }
}

// --- Pages ---

void test_serves_the_page_on_its_access_point()
{
    Device d;
    d.begin();
    d.finishScan();
    const FakeHttpServer::Response response = d.get("/");
    TEST_ASSERT_EQUAL_INT(200, response.status);
    TEST_ASSERT_TRUE(response.gzipped);
    TEST_ASSERT_EQUAL_STRING("text/html; charset=utf-8", response.contentType.c_str());
}

void test_refuses_requests_from_the_stations_network()
{
    Device d;
    d.fakes.wifi.scanStartSucceeds = false;
    TEST_ASSERT_TRUE(d.esp.networks().save("Home", "correct horse"));
    d.begin();
    d.fakes.wifi.completeConnect();
    d.esp.startPortal();
    d.esp.process();
    TEST_ASSERT_EQUAL(PortalState::Active, d.esp.portalState());

    d.fakes.http.localIp = d.fakes.wifi.ip;
    TEST_ASSERT_EQUAL_INT(403, d.get("/").status);
    TEST_ASSERT_EQUAL_INT(403, d.get("/anything").status);
}

void test_unknown_paths_are_not_found()
{
    Device d;
    d.begin();
    d.finishScan();
    TEST_ASSERT_EQUAL_INT(404, d.get("/nothing-here").status);
}

void test_requests_from_another_network_do_not_keep_it_open()
{
    Device d;
    d.begin();
    d.finishScan();
    d.fakes.http.localIp = Ipv4Address(10, 0, 0, 5);
    d.fakes.clock.advance(kTimeout - 1000);
    d.get("/");
    d.fakes.clock.advance(1000);
    d.esp.process();
    TEST_ASSERT_EQUAL(PortalState::Off, d.esp.portalState());
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_opens_by_itself_without_a_saved_network);
    RUN_TEST(test_nothing_happens_before_begin);
    RUN_TEST(test_stays_closed_with_a_saved_network);
    RUN_TEST(test_start_portal_opens_it_with_a_saved_network_too);
    RUN_TEST(test_start_portal_before_begin_waits_for_it);
    RUN_TEST(test_while_connected_it_opens_on_the_stations_channel_without_a_scan);
    RUN_TEST(test_opens_at_once_when_the_scan_cannot_start);
    RUN_TEST(test_access_point_that_fails_to_start_leaves_it_closed);
    RUN_TEST(test_channel_of_the_strongest_saved_network_wins);
    RUN_TEST(test_channel_of_the_strongest_network_without_a_saved_one);
    RUN_TEST(test_default_channel_when_the_scan_finds_nothing);
    RUN_TEST(test_stop_portal_closes_it_and_it_stays_closed);
    RUN_TEST(test_stop_portal_while_starting_cancels_it);
    RUN_TEST(test_timeout_without_a_network_reopens_it);
    RUN_TEST(test_timeout_without_restart_leaves_it_closed);
    RUN_TEST(test_timeout_with_a_saved_network_closes_it);
    RUN_TEST(test_each_request_restarts_the_timeout);
    RUN_TEST(test_timeout_zero_keeps_it_open);
    RUN_TEST(test_opens_after_failing_for_the_set_time_and_closes_once_connected);
    RUN_TEST(test_opened_by_start_portal_it_stays_open_once_connected);
    RUN_TEST(test_never_opens_after_failing_by_default);
    RUN_TEST(test_ssid_follows_the_device_name);
    RUN_TEST(test_ssid_can_be_set);
    RUN_TEST(test_ssid_longer_than_32_bytes_is_refused);
    RUN_TEST(test_password_protects_the_access_point);
    RUN_TEST(test_invalid_passwords_are_refused_and_change_nothing);
    RUN_TEST(test_password_is_never_logged);
    RUN_TEST(test_serves_the_page_on_its_access_point);
    RUN_TEST(test_refuses_requests_from_the_stations_network);
    RUN_TEST(test_unknown_paths_are_not_found);
    RUN_TEST(test_requests_from_another_network_do_not_keep_it_open);
    return UNITY_END();
}
