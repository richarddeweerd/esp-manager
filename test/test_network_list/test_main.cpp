#include <EspManager.h>
#include <FakePlatform.h>
#include <unity.h>

#include <string>
#include <vector>

using espmgr::FakeHttpServer;
using espmgr::FakePlatform;
using espmgr::HttpMethod;
using espmgr::NetworkScanner;
using espmgr::ProvisioningPortal;
using espmgr::ScanResult;

namespace
{

ScanResult seen(const std::string& ssid, int8_t rssi, bool secured = true, uint8_t channel = 6)
{
    ScanResult result;
    result.ssid = ssid;
    result.rssi = rssi;
    result.channel = channel;
    result.secured = secured;
    return result;
}

std::string names(const std::vector<ScanResult>& list)
{
    std::string text;
    for (const ScanResult& result : list)
    {
        text += (text.empty() ? "" : ",") + result.ssid + " " + std::to_string(result.rssi);
    }
    return text;
}

// An EspManager on fakes whose Provisioning Portal is open, after a scan that found `found`.
struct Device
{
    FakePlatform fakes;
    EspManager esp{fakes.platform};

    void openPortal(std::vector<ScanResult> found)
    {
        esp.begin();
        esp.process();
        fakes.wifi.completeScan(std::move(found));
        esp.process();
    }

    FakeHttpServer::Response scan(bool refresh = false)
    {
        FakeHttpServer::Response response =
            fakes.http.request(HttpMethod::Get, "/api/scan",
                               refresh ? espmgr::HttpRequest::Fields{{"refresh", "1"}}
                                       : espmgr::HttpRequest::Fields{});
        esp.process();
        return response;
    }
};

}  // namespace

void setUp()
{
}

void tearDown()
{
}

// --- The list ---

void test_list_merges_each_ssid_at_its_strongest()
{
    const std::vector<ScanResult> list =
        NetworkScanner::networkList({seen("Home", -80, true, 1), seen("Cafe", -60, false),
                                     seen("Home", -50, true, 11), seen("Home", -70)},
                                    -90);
    TEST_ASSERT_EQUAL_STRING("Home -50,Cafe -60", names(list).c_str());
    TEST_ASSERT_EQUAL_UINT8(11, list[0].channel);
}

void test_list_leaves_out_hidden_and_weak_networks()
{
    const std::vector<ScanResult> list = NetworkScanner::networkList(
        {seen("", -30), seen("Far", -91), seen("Edge", -90), seen("Near", -40)}, -90);
    TEST_ASSERT_EQUAL_STRING("Near -40,Edge -90", names(list).c_str());
}

void test_list_is_strongest_first()
{
    const std::vector<ScanResult> list = NetworkScanner::networkList(
        {seen("C", -70), seen("A", -40), seen("D", -85), seen("B", -55)}, -90);
    TEST_ASSERT_EQUAL_STRING("A -40,B -55,C -70,D -85", names(list).c_str());
}

void test_weak_access_point_does_not_hide_a_strong_one_of_the_same_network()
{
    const std::vector<ScanResult> list =
        NetworkScanner::networkList({seen("Home", -95), seen("Home", -60)}, -90);
    TEST_ASSERT_EQUAL_STRING("Home -60", names(list).c_str());
}

// --- The scan API ---

void test_scan_api_returns_the_cleaned_list()
{
    Device d;
    d.fakes.wifi.scanStartSucceeds = false;
    TEST_ASSERT_TRUE(d.esp.networks().save("Home", "correct horse"));
    d.esp.begin();
    d.esp.process();

    d.fakes.wifi.scanStartSucceeds = true;
    TEST_ASSERT_TRUE(d.esp.startScan());
    d.esp.process();
    TEST_ASSERT_FALSE(d.esp.scanComplete());
    d.fakes.wifi.completeScan({seen("B", -70), seen("A", -50), seen("B", -60), seen("", -40)});
    d.esp.process();
    TEST_ASSERT_TRUE(d.esp.scanComplete());
    TEST_ASSERT_EQUAL_STRING("A -50,B -60", names(d.esp.scanResults()).c_str());
}

void test_scan_api_uses_the_portals_minimum_signal()
{
    Device d;
    d.fakes.wifi.scanStartSucceeds = true;
    d.esp.portal().setMinSignal(-65);
    TEST_ASSERT_EQUAL_INT8(-65, d.esp.portal().minSignal());
    TEST_ASSERT_TRUE(d.esp.startScan());
    d.fakes.wifi.completeScan({seen("Near", -60), seen("Far", -70)});
    d.esp.process();
    TEST_ASSERT_EQUAL_STRING("Near -60", names(d.esp.scanResults()).c_str());
}

void test_scan_started_during_a_connection_attempt_shares_its_scan()
{
    Device d;
    TEST_ASSERT_TRUE(d.esp.networks().save("Home", "correct horse"));
    d.esp.begin();
    d.esp.process();
    TEST_ASSERT_EQUAL(1, d.fakes.wifi.scansStarted);
    TEST_ASSERT_TRUE(d.esp.startScan());
    TEST_ASSERT_EQUAL(1, d.fakes.wifi.scansStarted);
    d.fakes.wifi.completeScan({seen("Home", -50)});
    d.esp.process();
    TEST_ASSERT_TRUE(d.esp.scanComplete());
    TEST_ASSERT_EQUAL_STRING("Home -50", names(d.esp.scanResults()).c_str());
    TEST_ASSERT_EQUAL(1, d.fakes.wifi.connectCalls.size());
}

void test_scan_api_reports_a_scan_that_cannot_start()
{
    Device d;
    d.fakes.wifi.scanStartSucceeds = false;
    TEST_ASSERT_FALSE(d.esp.startScan());
    TEST_ASSERT_TRUE(d.esp.scanComplete());
    TEST_ASSERT_TRUE(d.esp.scanResults().empty());
}

// --- The page's list ---

void test_page_gets_the_list_from_the_portals_own_scan_at_once()
{
    Device d;
    d.openPortal({seen("Home", -50), seen("Cafe \"Corner\"", -75, false), seen("Home", -60)});
    const FakeHttpServer::Response response = d.scan();
    TEST_ASSERT_EQUAL_INT(200, response.status);
    TEST_ASSERT_EQUAL_STRING("application/json", response.contentType.c_str());
    TEST_ASSERT_EQUAL_STRING(R"({"scanning":false,"networks":[)"
                             R"({"ssid":"Home","rssi":-50,"secured":true},)"
                             R"({"ssid":"Cafe \"Corner\"","rssi":-75,"secured":false}]})",
                             response.body.c_str());
    TEST_ASSERT_EQUAL(1, d.fakes.wifi.scansStarted);
}

void test_old_list_is_scanned_again_and_shown_meanwhile()
{
    Device d;
    d.openPortal({seen("Home", -50)});
    d.fakes.clock.advance(ProvisioningPortal::kScanMaxAgeMs);
    FakeHttpServer::Response response = d.scan();
    TEST_ASSERT_EQUAL(2, d.fakes.wifi.scansStarted);
    TEST_ASSERT_EQUAL_STRING(
        R"({"scanning":true,"networks":[{"ssid":"Home","rssi":-50,"secured":true}]})",
        response.body.c_str());

    // Asking again while it runs starts nothing new.
    d.scan();
    d.scan(true);
    TEST_ASSERT_EQUAL(2, d.fakes.wifi.scansStarted);

    d.fakes.wifi.completeScan({seen("Office", -45)});
    d.esp.process();
    response = d.scan();
    TEST_ASSERT_EQUAL_STRING(
        R"({"scanning":false,"networks":[{"ssid":"Office","rssi":-45,"secured":true}]})",
        response.body.c_str());
}

void test_refresh_scans_again_even_when_the_list_is_new()
{
    Device d;
    d.openPortal({seen("Home", -50)});
    d.scan(true);
    TEST_ASSERT_EQUAL(2, d.fakes.wifi.scansStarted);
}

void test_page_list_leaves_out_weak_networks()
{
    Device d;
    d.esp.portal().setMinSignal(-70);
    d.openPortal({seen("Near", -60), seen("Far", -80)});
    TEST_ASSERT_EQUAL_STRING(
        R"({"scanning":false,"networks":[{"ssid":"Near","rssi":-60,"secured":true}]})",
        d.scan().body.c_str());
}

void test_page_list_is_refused_from_the_stations_network()
{
    Device d;
    d.openPortal({seen("Home", -50)});
    d.fakes.http.localIp = d.fakes.wifi.ip;
    TEST_ASSERT_EQUAL_INT(403, d.scan().status);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_list_merges_each_ssid_at_its_strongest);
    RUN_TEST(test_list_leaves_out_hidden_and_weak_networks);
    RUN_TEST(test_list_is_strongest_first);
    RUN_TEST(test_weak_access_point_does_not_hide_a_strong_one_of_the_same_network);
    RUN_TEST(test_scan_api_returns_the_cleaned_list);
    RUN_TEST(test_scan_api_uses_the_portals_minimum_signal);
    RUN_TEST(test_scan_started_during_a_connection_attempt_shares_its_scan);
    RUN_TEST(test_scan_api_reports_a_scan_that_cannot_start);
    RUN_TEST(test_page_gets_the_list_from_the_portals_own_scan_at_once);
    RUN_TEST(test_old_list_is_scanned_again_and_shown_meanwhile);
    RUN_TEST(test_refresh_scans_again_even_when_the_list_is_new);
    RUN_TEST(test_page_list_leaves_out_weak_networks);
    RUN_TEST(test_page_list_is_refused_from_the_stations_network);
    return UNITY_END();
}
