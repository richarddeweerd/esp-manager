#include <DigestClient.h>
#include <EspManager.h>
#include <FakePlatform.h>
#include <unity.h>

#include <cstdint>
#include <cstdio>
#include <string>

using espmgr::Branding;
using espmgr::DigestClient;
using espmgr::FakeHttpServer;
using espmgr::FakePlatform;
using espmgr::HttpMethod;
using espmgr::HttpRequest;
using espmgr::PortalState;
using espmgr::Theme;

namespace
{

const char* const kPassword = "owner password";
const uint8_t kSvg[] = {'<', 's', 'v', 'g', '/', '>'};

// An EspManager on fakes with the Provisioning Portal open, as on a new device.
struct Device
{
    explicit Device(FakePlatform& platform) : fakes(platform)
    {
        fakes.wifi.scanStartSucceeds = false;
    }

    FakePlatform& fakes;
    EspManager esp{fakes.platform};

    void openPortal()
    {
        esp.begin();
        esp.process();
        TEST_ASSERT_EQUAL(PortalState::Active, esp.portalState());
    }

    // A request on the access point's address, as from the Owner's phone during setup.
    FakeHttpServer::Response portal(const std::string& path)
    {
        fakes.http.localIp = fakes.wifi.apIp;
        return fakes.http.request(HttpMethod::Get, path);
    }

    // The Management Interface with a password, connected to a Saved Network.
    void openManagement()
    {
        esp.management().enable();
        TEST_ASSERT_TRUE(esp.management().setPassword(kPassword));
        TEST_ASSERT_TRUE(esp.networks().save("Home", "correct horse"));
        esp.begin();
        esp.process();
        fakes.wifi.completeConnect();
        esp.process();
        TEST_ASSERT_TRUE(esp.management().active());
    }

    FakeHttpServer::Response station(const std::string& path, HttpRequest::Fields headers = {})
    {
        fakes.http.localIp = fakes.wifi.ip;
        return fakes.http.request(HttpMethod::Get, path, {}, std::move(headers));
    }

    // What a browser does: take the challenge once, then answer it with a rising count.
    FakeHttpServer::Response login(const std::string& path)
    {
        if (nonce.empty())
        {
            const FakeHttpServer::Response challenge = station(path);
            TEST_ASSERT_EQUAL_INT(401, challenge.status);
            nonce = DigestClient::nonceFrom(challenge.header("WWW-Authenticate"));
        }
        DigestClient client;
        client.realm = "esp-abcdef";
        client.password = kPassword;
        char count[9];
        std::snprintf(count, sizeof(count), "%08x", ++requests);
        return station(path, {{"Authorization", client.header(nonce, "GET", path, count)}});
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

// --- The rules (US15.1.1, US15.1.2) ---

void test_nothing_is_set_by_default()
{
    FakePlatform fakes;
    Device d(fakes);
    TEST_ASSERT_EQUAL_STRING("", d.esp.ui().title().c_str());
    TEST_ASSERT_FALSE(d.esp.ui().hasLogo());
    TEST_ASSERT_EQUAL(Theme::Auto, d.esp.ui().theme());
    TEST_ASSERT_NULL(d.esp.ui().css());
}

void test_a_title_up_to_64_bytes_is_kept()
{
    FakePlatform fakes;
    Device d(fakes);
    const std::string longest(Branding::kMaxTitleLength, 'a');
    TEST_ASSERT_TRUE(d.esp.ui().setTitle(longest));
    TEST_ASSERT_EQUAL_STRING(longest.c_str(), d.esp.ui().title().c_str());
    TEST_ASSERT_TRUE(d.esp.ui().setTitle("Café Thermostat"));
    TEST_ASSERT_EQUAL_STRING("Café Thermostat", d.esp.ui().title().c_str());
}

void test_an_empty_title_clears_it()
{
    FakePlatform fakes;
    Device d(fakes);
    d.esp.ui().setTitle("Acme");
    TEST_ASSERT_TRUE(d.esp.ui().setTitle(""));
    TEST_ASSERT_EQUAL_STRING("", d.esp.ui().title().c_str());
}

void test_a_long_title_is_refused_and_logged()
{
    FakePlatform fakes;
    Device d(fakes);
    d.esp.setLogLevel(espmgr::LogLevel::Error);
    d.esp.ui().setTitle("Acme");
    TEST_ASSERT_FALSE(d.esp.ui().setTitle(std::string(Branding::kMaxTitleLength + 1, 'a')));
    TEST_ASSERT_EQUAL_STRING("Acme", d.esp.ui().title().c_str());
    TEST_ASSERT_TRUE(fakes.log.contains("over the limit of 64"));
}

void test_a_title_with_a_control_character_is_refused()
{
    FakePlatform fakes;
    Device d(fakes);
    d.esp.setLogLevel(espmgr::LogLevel::Error);
    TEST_ASSERT_FALSE(d.esp.ui().setTitle("Acme\nThermostat"));
    TEST_ASSERT_FALSE(d.esp.ui().setTitle(std::string("Acme\0", 5)));
    TEST_ASSERT_FALSE(d.esp.ui().setTitle("Acme\x7f"));
    TEST_ASSERT_EQUAL_STRING("", d.esp.ui().title().c_str());
    TEST_ASSERT_TRUE(fakes.log.contains("control character"));
}

void test_a_logo_of_a_known_type_is_kept()
{
    FakePlatform fakes;
    Device d(fakes);
    for (const char* type : {"image/svg+xml", "image/png", "image/jpeg", "image/webp"})
    {
        TEST_ASSERT_TRUE_MESSAGE(d.esp.ui().setLogo(kSvg, sizeof(kSvg), type), type);
        TEST_ASSERT_TRUE(d.esp.ui().hasLogo());
    }
}

void test_a_logo_of_another_type_or_without_data_is_refused()
{
    FakePlatform fakes;
    Device d(fakes);
    d.esp.setLogLevel(espmgr::LogLevel::Error);
    TEST_ASSERT_FALSE(d.esp.ui().setLogo(kSvg, sizeof(kSvg), "image/gif"));
    TEST_ASSERT_FALSE(d.esp.ui().setLogo(kSvg, sizeof(kSvg), nullptr));
    TEST_ASSERT_FALSE(d.esp.ui().setLogo(kSvg, 0, "image/png"));
    TEST_ASSERT_FALSE(d.esp.ui().setLogo(nullptr, 10, "image/png"));
    TEST_ASSERT_FALSE(d.esp.ui().hasLogo());
    TEST_ASSERT_TRUE(fakes.log.contains("logo"));
}

void test_a_refused_logo_keeps_the_one_before()
{
    FakePlatform fakes;
    Device d(fakes);
    d.esp.ui().setLogo(kSvg, sizeof(kSvg), "image/svg+xml");
    TEST_ASSERT_FALSE(d.esp.ui().setLogo(kSvg, sizeof(kSvg), "text/html"));
    d.openPortal();
    const FakeHttpServer::Response response = d.portal("/logo");
    TEST_ASSERT_EQUAL_INT(200, response.status);
    TEST_ASSERT_EQUAL_STRING("image/svg+xml", response.contentType.c_str());
}

void test_a_null_logo_removes_it()
{
    FakePlatform fakes;
    Device d(fakes);
    d.esp.ui().setLogo(kSvg, sizeof(kSvg), "image/svg+xml");
    TEST_ASSERT_TRUE(d.esp.ui().setLogo(nullptr, 0, nullptr));
    TEST_ASSERT_FALSE(d.esp.ui().hasLogo());
}

void test_theme_names()
{
    TEST_ASSERT_EQUAL_STRING("auto", espmgr::toString(Theme::Auto));
    TEST_ASSERT_EQUAL_STRING("light", espmgr::toString(Theme::Light));
    TEST_ASSERT_EQUAL_STRING("dark", espmgr::toString(Theme::Dark));
}

// --- In the Provisioning Portal ---

void test_portal_ui_without_branding()
{
    FakePlatform fakes;
    Device d(fakes);
    d.openPortal();
    const FakeHttpServer::Response response = d.portal("/api/ui");
    TEST_ASSERT_EQUAL_INT(200, response.status);
    TEST_ASSERT_EQUAL_STRING("application/json", response.contentType.c_str());
    TEST_ASSERT_EQUAL_STRING("no-cache", response.header("Cache-Control").c_str());
    TEST_ASSERT_EQUAL_STRING(R"({"title":"","logo":false,"theme":"auto"})", response.body.c_str());
}

void test_portal_ui_with_branding()
{
    FakePlatform fakes;
    Device d(fakes);
    d.esp.ui().setTitle("Acme \"Thermo\"");
    d.esp.ui().setLogo(kSvg, sizeof(kSvg), "image/svg+xml");
    d.esp.ui().setTheme(Theme::Dark);
    d.openPortal();
    TEST_ASSERT_EQUAL_STRING(R"({"title":"Acme \"Thermo\"","logo":true,"theme":"dark"})",
                             d.portal("/api/ui").body.c_str());
}

void test_changes_after_begin_show_on_the_next_load()
{
    FakePlatform fakes;
    Device d(fakes);
    d.openPortal();
    d.esp.ui().setTitle("Later");
    d.esp.ui().setTheme(Theme::Light);
    TEST_ASSERT_EQUAL_STRING(R"({"title":"Later","logo":false,"theme":"light"})",
                             d.portal("/api/ui").body.c_str());
}

void test_portal_css_is_empty_without_a_string()
{
    FakePlatform fakes;
    Device d(fakes);
    d.openPortal();
    const FakeHttpServer::Response response = d.portal("/custom.css");
    TEST_ASSERT_EQUAL_INT(200, response.status);
    TEST_ASSERT_EQUAL_STRING("text/css", response.contentType.c_str());
    TEST_ASSERT_EQUAL_STRING("no-cache", response.header("Cache-Control").c_str());
    TEST_ASSERT_EQUAL_STRING("", response.body.c_str());
}

void test_portal_css_is_the_developers_string()
{
    FakePlatform fakes;
    Device d(fakes);
    d.esp.ui().setCss(":root { --accent: #e11d48; }");
    d.openPortal();
    TEST_ASSERT_EQUAL_STRING(":root { --accent: #e11d48; }", d.portal("/custom.css").body.c_str());
    TEST_ASSERT_FALSE(d.portal("/custom.css").gzipped);
    d.esp.ui().setCss(nullptr);
    TEST_ASSERT_EQUAL_STRING("", d.portal("/custom.css").body.c_str());
}

void test_portal_logo_is_404_without_one()
{
    FakePlatform fakes;
    Device d(fakes);
    d.openPortal();
    TEST_ASSERT_EQUAL_INT(404, d.portal("/logo").status);
}

void test_portal_logo_is_the_developers_bytes()
{
    FakePlatform fakes;
    Device d(fakes);
    d.esp.ui().setLogo(kSvg, sizeof(kSvg), "image/svg+xml");
    d.openPortal();
    const FakeHttpServer::Response response = d.portal("/logo");
    TEST_ASSERT_EQUAL_INT(200, response.status);
    TEST_ASSERT_EQUAL_STRING("image/svg+xml", response.contentType.c_str());
    TEST_ASSERT_EQUAL_STRING("no-cache", response.header("Cache-Control").c_str());
    TEST_ASSERT_EQUAL_STRING("<svg/>", response.body.c_str());
    TEST_ASSERT_FALSE(response.gzipped);
}

// --- In the Management Interface ---

void test_management_needs_the_password()
{
    FakePlatform fakes;
    Device d(fakes);
    d.esp.ui().setLogo(kSvg, sizeof(kSvg), "image/svg+xml");
    d.openManagement();
    TEST_ASSERT_EQUAL_INT(401, d.station("/api/ui").status);
    TEST_ASSERT_EQUAL_INT(401, d.station("/custom.css").status);
    TEST_ASSERT_EQUAL_INT(401, d.station("/logo").status);
}

void test_management_serves_the_same_branding()
{
    FakePlatform fakes;
    Device d(fakes);
    d.esp.ui().setTitle("Acme");
    d.esp.ui().setCss("h1 { color: red; }");
    d.esp.ui().setLogo(kSvg, sizeof(kSvg), "image/svg+xml");
    d.openManagement();
    TEST_ASSERT_EQUAL_STRING(R"({"title":"Acme","logo":true,"theme":"auto"})",
                             d.login("/api/ui").body.c_str());
    TEST_ASSERT_EQUAL_STRING("h1 { color: red; }", d.login("/custom.css").body.c_str());
    const FakeHttpServer::Response logo = d.login("/logo");
    TEST_ASSERT_EQUAL_INT(200, logo.status);
    TEST_ASSERT_EQUAL_STRING("<svg/>", logo.body.c_str());
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_nothing_is_set_by_default);
    RUN_TEST(test_a_title_up_to_64_bytes_is_kept);
    RUN_TEST(test_an_empty_title_clears_it);
    RUN_TEST(test_a_long_title_is_refused_and_logged);
    RUN_TEST(test_a_title_with_a_control_character_is_refused);
    RUN_TEST(test_a_logo_of_a_known_type_is_kept);
    RUN_TEST(test_a_logo_of_another_type_or_without_data_is_refused);
    RUN_TEST(test_a_refused_logo_keeps_the_one_before);
    RUN_TEST(test_a_null_logo_removes_it);
    RUN_TEST(test_theme_names);
    RUN_TEST(test_portal_ui_without_branding);
    RUN_TEST(test_portal_ui_with_branding);
    RUN_TEST(test_changes_after_begin_show_on_the_next_load);
    RUN_TEST(test_portal_css_is_empty_without_a_string);
    RUN_TEST(test_portal_css_is_the_developers_string);
    RUN_TEST(test_portal_logo_is_404_without_one);
    RUN_TEST(test_portal_logo_is_the_developers_bytes);
    RUN_TEST(test_management_needs_the_password);
    RUN_TEST(test_management_serves_the_same_branding);
    return UNITY_END();
}
