// The Owner's time zone and servers through the Time Section (US18.2.4, US18.3.1, F18.9), and
// Synchronise now in the Management Interface (US18.6.1).

#include <DigestClient.h>
#include <EspManager.h>
#include <FakePlatform.h>
#include <unity.h>

#include <cstdio>
#include <string>

using espmgr::DigestClient;
using espmgr::FakeHttpServer;
using espmgr::FakePlatform;
using espmgr::HttpMethod;
using espmgr::HttpRequest;
using espmgr::NetworkTime;
using espmgr::Setting;

namespace
{

const char* const kPassword = "owner password";
const char* const kAmsterdam = "CET-1CEST,M3.5.0,M10.5.0/3";

// An EspManager on fakes. `fakes` is passed in, so a second device can find what the first stored.
struct Device
{
    explicit Device(FakePlatform& platform) : fakes(platform)
    {
        fakes.wifi.scanStartSucceeds = false;
    }

    FakePlatform& fakes;
    EspManager esp{fakes.platform};

    void connect()
    {
        TEST_ASSERT_TRUE(esp.networks().save("Home", "correct horse"));
        esp.begin();
        esp.process();
        fakes.wifi.completeConnect();
        esp.process();
    }

    // Stores values as the web pages would, and lets the events through.
    void apply(std::vector<std::pair<std::string, std::string>> fields)
    {
        TEST_ASSERT_TRUE(esp.settings().apply(fields).empty());
        esp.process();
    }

    // The Management Interface with a password, connected to a Saved Network.
    void openManagement()
    {
        esp.management().enable();
        TEST_ASSERT_TRUE(esp.management().setPassword(kPassword));
        connect();
        TEST_ASSERT_TRUE(esp.management().active());
    }

    // An authorised request on the station's address, as a browser makes it.
    FakeHttpServer::Response station(HttpMethod method, const std::string& path,
                                     HttpRequest::Fields headers = {})
    {
        fakes.http.localIp = fakes.wifi.ip;
        if (nonce.empty())
        {
            const FakeHttpServer::Response challenge = fakes.http.request(HttpMethod::Get, "/");
            TEST_ASSERT_EQUAL_INT(401, challenge.status);
            nonce = DigestClient::nonceFrom(challenge.header("WWW-Authenticate"));
        }
        DigestClient client;
        client.realm = "esp-abcdef";
        client.password = kPassword;
        char count[9];
        std::snprintf(count, sizeof(count), "%08x", ++requests);
        headers.emplace_back(
            "Authorization",
            client.header(nonce, method == HttpMethod::Get ? "GET" : "POST", path, count));
        return fakes.http.request(method, path, {}, std::move(headers));
    }

    FakeHttpServer::Response syncNow()
    {
        return station(HttpMethod::Post, "/api/time/sync", {{"X-Requested-With", "EspManager"}});
    }

    std::string nonce;
    unsigned requests = 0;
};

}  // namespace

void setUp()
{
}

void tearDown()
{
}

void test_without_owner_editing_there_is_no_time_section()
{
    FakePlatform fakes;
    Device d(fakes);
    d.esp.ntp().enable();
    d.connect();
    TEST_ASSERT_NULL(d.esp.settings().find(NetworkTime::kZoneKey));
    TEST_ASSERT_EQUAL_UINT(0, d.esp.settings().count());
}

void test_owner_editing_adds_the_time_section()
{
    FakePlatform fakes;
    Device d(fakes);
    d.esp.ntp().ownerCanEdit();
    d.esp.ntp().ownerCanEdit();
    TEST_ASSERT_EQUAL_UINT(1, d.esp.settings().sections().size());
    TEST_ASSERT_EQUAL_STRING("Time", d.esp.settings().sections()[0].title().c_str());
    TEST_ASSERT_EQUAL_UINT(3, d.esp.settings().count());
    TEST_ASSERT_NOT_NULL(d.esp.settings().find("time-zone"));
    TEST_ASSERT_NOT_NULL(d.esp.settings().find("time-dst"));
    TEST_ASSERT_NOT_NULL(d.esp.settings().find("time-posix"));
    TEST_ASSERT_NULL(d.esp.settings().find("time-server"));
    TEST_ASSERT_EQUAL_STRING("1", d.esp.settings().find("time-dst")->text().c_str());

    d.esp.ntp().ownerCanEditServers();
    TEST_ASSERT_EQUAL_UINT(5, d.esp.settings().count());
    TEST_ASSERT_EQUAL_UINT(1, d.esp.settings().sections().size());
}

void test_the_owners_zone_overrides_the_developers()
{
    FakePlatform fakes;
    Device d(fakes);
    d.esp.ntp().ownerCanEdit().setTimezone("EST5EDT,M3.2.0,M11.1.0");
    d.connect();
    TEST_ASSERT_EQUAL_STRING("EST5EDT,M3.2.0,M11.1.0", fakes.time.timezone.c_str());
    TEST_ASSERT_EQUAL_STRING("", d.esp.ntp().zone().c_str());

    d.apply({{"time-zone", "Europe/Amsterdam"}, {"time-dst", "1"}, {"time-posix", kAmsterdam}});
    TEST_ASSERT_EQUAL_STRING(kAmsterdam, fakes.time.timezone.c_str());
    TEST_ASSERT_EQUAL_STRING(kAmsterdam, d.esp.ntp().timezone().c_str());
    TEST_ASSERT_EQUAL_STRING("Europe/Amsterdam", d.esp.ntp().zone().c_str());
    TEST_ASSERT_TRUE(d.esp.ntp().dst());

    // The Developer's zone waits until the Owner's goes.
    TEST_ASSERT_TRUE(d.esp.ntp().setTimezone("JST-9"));
    TEST_ASSERT_EQUAL_STRING(kAmsterdam, fakes.time.timezone.c_str());

    const espmgr::DeviceStatus status = d.esp.status();
    TEST_ASSERT_EQUAL_STRING("Europe/Amsterdam", status.time.zone.c_str());
    TEST_ASSERT_TRUE(status.time.dst);
    TEST_ASSERT_EQUAL_STRING(kAmsterdam, status.time.posix.c_str());
}

void test_without_daylight_saving_the_standard_offset_applies()
{
    FakePlatform fakes;
    Device d(fakes);
    d.esp.ntp().ownerCanEdit();
    d.connect();
    d.apply({{"time-zone", "Europe/Amsterdam"}, {"time-dst", "0"}, {"time-posix", "CET-1"}});
    TEST_ASSERT_EQUAL_STRING("CET-1", fakes.time.timezone.c_str());
    TEST_ASSERT_FALSE(d.esp.ntp().dst());
    TEST_ASSERT_FALSE(d.esp.status().time.dst);
}

void test_a_stored_zone_applies_at_begin()
{
    FakePlatform fakes;
    {
        Device first(fakes);
        first.esp.ntp().ownerCanEdit();
        first.connect();
        first.apply({{"time-zone", "Asia/Tokyo"}, {"time-posix", "JST-9"}});
    }
    fakes.time.timezone.clear();
    Device second(fakes);
    second.esp.ntp().ownerCanEdit().setTimezone("UTC0");
    TEST_ASSERT_EQUAL_STRING("UTC0", fakes.time.timezone.c_str());
    second.esp.begin();
    TEST_ASSERT_EQUAL_STRING("JST-9", fakes.time.timezone.c_str());
    TEST_ASSERT_EQUAL_STRING("Asia/Tokyo", second.esp.ntp().zone().c_str());
}

void test_a_settings_reset_brings_back_the_developers_zone()
{
    FakePlatform fakes;
    Device d(fakes);
    d.esp.ntp().ownerCanEdit().setTimezone("EST5EDT,M3.2.0,M11.1.0");
    d.connect();
    d.apply({{"time-zone", "Europe/Amsterdam"}, {"time-posix", kAmsterdam}});
    TEST_ASSERT_TRUE(d.esp.settingsReset());
    d.esp.process();
    TEST_ASSERT_EQUAL_STRING("EST5EDT,M3.2.0,M11.1.0", fakes.time.timezone.c_str());
    TEST_ASSERT_EQUAL_STRING("", d.esp.ntp().zone().c_str());
}

void test_an_invalid_posix_string_is_refused()
{
    FakePlatform fakes;
    Device d(fakes);
    d.esp.ntp().ownerCanEdit();
    d.connect();
    const auto problems = d.esp.settings().apply({{"time-posix", "CET-1\x01"}});
    TEST_ASSERT_EQUAL_UINT(1, problems.size());
    TEST_ASSERT_EQUAL_STRING("time-posix", problems[0].key.c_str());
    TEST_ASSERT_EQUAL_STRING("UTC0", d.esp.ntp().timezone().c_str());
}

void test_the_owners_servers_replace_the_first_two()
{
    FakePlatform fakes;
    Device d(fakes);
    d.esp.ntp().enable().ownerCanEditServers();
    d.connect();
    TEST_ASSERT_EQUAL_INT(1, fakes.time.starts);
    d.apply({{"time-server", "192.168.1.1"}});
    TEST_ASSERT_EQUAL_INT(2, fakes.time.starts);
    TEST_ASSERT_EQUAL_STRING("192.168.1.1", fakes.time.servers[0].c_str());
    TEST_ASSERT_EQUAL_STRING("time.google.com", fakes.time.servers[1].c_str());
    d.apply({{"time-fallback", "ntp.example.com"}});
    TEST_ASSERT_EQUAL_STRING("ntp.example.com", fakes.time.servers[1].c_str());
    TEST_ASSERT_EQUAL_STRING("time.cloudflare.com", fakes.time.servers[2].c_str());

    const auto problems = d.esp.settings().apply({{"time-server", "has space"}});
    TEST_ASSERT_EQUAL_UINT(1, problems.size());

    d.apply({{"time-server", ""}, {"time-fallback", ""}});
    TEST_ASSERT_EQUAL_STRING("pool.ntp.org", fakes.time.servers[0].c_str());
    TEST_ASSERT_EQUAL_STRING("time.google.com", fakes.time.servers[1].c_str());
}

void test_the_section_is_in_management_only_when_marked()
{
    FakePlatform fakes;
    Device d(fakes);
    d.esp.ntp().ownerCanEdit();
    d.openManagement();
    auto answer = d.station(HttpMethod::Get, "/api/settings");
    TEST_ASSERT_EQUAL_INT(200, answer.status);
    TEST_ASSERT_TRUE(answer.body.find("time-zone") == std::string::npos);

    FakePlatform fakes2;
    Device e(fakes2);
    e.esp.ntp().editableInManagement().ownerCanEditServers();
    e.openManagement();
    answer = e.station(HttpMethod::Get, "/api/settings");
    TEST_ASSERT_TRUE(answer.body.find("\"time-zone\"") != std::string::npos);
    TEST_ASSERT_TRUE(answer.body.find("\"time-dst\"") != std::string::npos);
    TEST_ASSERT_TRUE(answer.body.find("\"time-posix\"") != std::string::npos);
    TEST_ASSERT_TRUE(answer.body.find("\"time-fallback\"") != std::string::npos);
}

void test_sync_now_in_management()
{
    FakePlatform fakes;
    Device d(fakes);
    d.openManagement();
    auto answer = d.syncNow();
    TEST_ASSERT_EQUAL_INT(409, answer.status);
    TEST_ASSERT_EQUAL_STRING(R"({"error":"time-off"})", answer.body.c_str());

    d.esp.ntp().enable();
    d.esp.process();
    TEST_ASSERT_EQUAL_INT(1, fakes.time.starts);
    answer = d.syncNow();
    TEST_ASSERT_EQUAL_INT(200, answer.status);
    TEST_ASSERT_EQUAL_INT(1, fakes.time.restarts);

    answer = d.station(HttpMethod::Post, "/api/time/sync");
    TEST_ASSERT_EQUAL_INT(403, answer.status);
    TEST_ASSERT_EQUAL_INT(1, fakes.time.restarts);
}

void test_sync_now_needs_the_password()
{
    FakePlatform fakes;
    Device d(fakes);
    d.esp.ntp().enable();
    d.openManagement();
    fakes.http.localIp = fakes.wifi.ip;
    const auto answer = fakes.http.request(HttpMethod::Post, "/api/time/sync", {},
                                           {{"X-Requested-With", "EspManager"}});
    TEST_ASSERT_EQUAL_INT(401, answer.status);
    TEST_ASSERT_EQUAL_INT(0, fakes.time.restarts);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_without_owner_editing_there_is_no_time_section);
    RUN_TEST(test_owner_editing_adds_the_time_section);
    RUN_TEST(test_the_owners_zone_overrides_the_developers);
    RUN_TEST(test_without_daylight_saving_the_standard_offset_applies);
    RUN_TEST(test_a_stored_zone_applies_at_begin);
    RUN_TEST(test_a_settings_reset_brings_back_the_developers_zone);
    RUN_TEST(test_an_invalid_posix_string_is_refused);
    RUN_TEST(test_the_owners_servers_replace_the_first_two);
    RUN_TEST(test_the_section_is_in_management_only_when_marked);
    RUN_TEST(test_sync_now_in_management);
    RUN_TEST(test_sync_now_needs_the_password);
    return UNITY_END();
}
