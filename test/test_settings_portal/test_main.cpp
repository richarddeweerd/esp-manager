// Settings in the Provisioning Portal (US06.1.1, US06.1.3, US06.2.1): what the page gets, checking
// and saving through /api/settings, and first setup, where they come with the network.

#include <EspManager.h>
#include <FakePlatform.h>
#include <unity.h>

#include <string>
#include <vector>

using espmgr::FakeHttpServer;
using espmgr::FakePlatform;
using espmgr::HttpMethod;
using espmgr::HttpRequest;
using espmgr::LogLevel;
using espmgr::PortalState;
using espmgr::ProvisioningPortal;
using espmgr::Setting;

namespace
{

const HttpRequest::Fields kFromPage = {{"X-Requested-With", "EspManager"}};
const char* const kWifiPassword = "correct horse";

// An EspManager on fakes with some Settings; the sketch's handles are at hand.
struct Device
{
    FakePlatform fakes;
    EspManager esp{fakes.platform};
    espmgr::TextSetting& host = esp.settings().section("MQTT").addText("host", "Server").required();
    espmgr::NumberSetting& port =
        esp.settings().section("MQTT").addNumber("port", "Port", 1883).range(1, 65535);
    espmgr::PasswordSetting& secret =
        esp.settings().section("MQTT").addPassword("secret", "Secret");
    espmgr::CheckboxSetting& discovery = esp.settings().addCheckbox("ha", "Discovery", true);
    espmgr::SelectSetting& zone =
        esp.settings().addSelect("zone", "Zone", "eu").option("eu", "Europe").option("us", "US");
    std::vector<std::string> changes;

    Device()
    {
        fakes.wifi.scanStartSucceeds = false;
        esp.setLogLevel(LogLevel::Verbose);
        esp.onSettingChanged([this](const Setting& setting) { changes.push_back(setting.key()); });
    }

    void open()
    {
        esp.begin();
        esp.process();
        TEST_ASSERT_EQUAL(PortalState::Active, esp.portalState());
    }

    // With a Saved Network, so it isn't first setup any more.
    void openWithNetwork()
    {
        TEST_ASSERT_TRUE(esp.networks().save("Home", kWifiPassword));
        esp.begin();
        esp.startPortal();
        esp.process();
        TEST_ASSERT_EQUAL(PortalState::Active, esp.portalState());
    }

    FakeHttpServer::Response get()
    {
        return fakes.http.request(HttpMethod::Get, "/api/settings");
    }

    FakeHttpServer::Response post(const std::string& path, HttpRequest::Fields form)
    {
        FakeHttpServer::Response response =
            fakes.http.request(HttpMethod::Post, path, std::move(form), kFromPage);
        esp.process();
        return response;
    }

    FakeHttpServer::Response connect(HttpRequest::Fields more = {})
    {
        HttpRequest::Fields form = {{"ssid", "Home"}, {"password", kWifiPassword}};
        form.insert(form.end(), more.begin(), more.end());
        FakeHttpServer::Response response =
            fakes.http.request(HttpMethod::Post, "/api/connect", std::move(form));
        esp.process();
        return response;
    }

    // Lets the network on trial connect.
    void completeTrial()
    {
        fakes.wifi.completeConnect(11);
        esp.process();
    }
};

bool has(const std::string& text, const std::string& part)
{
    return text.find(part) != std::string::npos;
}

}  // namespace

void setUp()
{
}

void tearDown()
{
}

// --- GET /api/settings ---

void test_the_page_gets_the_sections_and_fields()
{
    Device d;
    d.open();
    const FakeHttpServer::Response response = d.get();
    TEST_ASSERT_EQUAL_INT(200, response.status);
    TEST_ASSERT_EQUAL_STRING(
        R"({"complete":false,"hasSettings":true,"sections":[)"
        R"({"title":"MQTT","settings":[)"
        R"({"key":"host","label":"Server","type":"text","required":true,"value":"","maxLength":64},)"
        R"({"key":"port","label":"Port","type":"number","required":false,"value":"1883","min":1,"max":65535},)"
        R"({"key":"secret","label":"Secret","type":"password","required":false,"set":false,"maxLength":64}]},)"
        R"({"title":"","settings":[)"
        R"({"key":"ha","label":"Discovery","type":"checkbox","required":false,"value":"1"},)"
        R"({"key":"zone","label":"Zone","type":"select","required":false,"value":"eu","options":[)"
        R"({"value":"eu","label":"Europe"},{"value":"us","label":"US"}]}]}]})",
        response.body.c_str());
}

void test_a_password_only_says_whether_it_is_set()
{
    Device d;
    d.openWithNetwork();
    TEST_ASSERT_TRUE(d.secret.set("hunter22"));
    const std::string body = d.get().body;
    TEST_ASSERT_TRUE(has(body, R"("set":true)"));
    TEST_ASSERT_FALSE(has(body, "hunter22"));
}

void test_a_sketch_without_settings_gets_an_empty_list()
{
    FakePlatform fakes;
    fakes.wifi.scanStartSucceeds = false;
    EspManager esp(fakes.platform);
    esp.begin();
    esp.process();
    TEST_ASSERT_EQUAL_STRING(R"({"complete":true,"hasSettings":false,"sections":[]})",
                             fakes.http.request(HttpMethod::Get, "/api/settings").body.c_str());
}

void test_a_refused_declaration_is_not_shown()
{
    Device d;
    d.esp.settings().addText("host", "Duplicate");
    d.open();
    TEST_ASSERT_FALSE(has(d.get().body, "Duplicate"));
}

// --- POST /api/settings ---

void test_a_save_stores_the_values_and_answers_with_the_settings()
{
    Device d;
    d.openWithNetwork();
    const FakeHttpServer::Response response = d.post("/api/settings", {{"setting.host", "broker"},
                                                                       {"setting.port", "8883"},
                                                                       {"setting.ha", "0"},
                                                                       {"setting.zone", "us"},
                                                                       {"other", "ignored"}});
    TEST_ASSERT_EQUAL_INT(200, response.status);
    TEST_ASSERT_TRUE(has(response.body, R"("complete":true)"));
    TEST_ASSERT_TRUE(has(response.body, R"("value":"8883")"));
    TEST_ASSERT_EQUAL_STRING("broker", d.host.value().c_str());
    TEST_ASSERT_EQUAL_INT32(8883, d.port.value());
    TEST_ASSERT_FALSE(d.discovery.value());
    TEST_ASSERT_EQUAL_STRING("us", d.zone.value().c_str());
    TEST_ASSERT_EQUAL_UINT(4, d.changes.size());
}

void test_a_refused_save_stores_nothing_and_lists_every_problem()
{
    Device d;
    d.openWithNetwork();
    const FakeHttpServer::Response response = d.post("/api/settings", {{"setting.host", "broker"},
                                                                       {"setting.port", "0"},
                                                                       {"setting.zone", "mars"},
                                                                       {"setting.x", "1"}});
    TEST_ASSERT_EQUAL_INT(400, response.status);
    TEST_ASSERT_EQUAL_STRING(R"({"error":"settings","errors":[)"
                             R"({"key":"port","error":"out-of-range","message":""},)"
                             R"({"key":"zone","error":"unknown-option","message":""},)"
                             R"({"key":"x","error":"unknown-setting","message":""}]})",
                             response.body.c_str());
    TEST_ASSERT_TRUE(d.host.value().empty());
    TEST_ASSERT_TRUE(d.changes.empty());
}

void test_the_developers_message_reaches_the_page()
{
    Device d;
    d.host.validate([](const std::string&) -> std::string { return "Use a full \"name\""; });
    d.openWithNetwork();
    const std::string body = d.post("/api/settings", {{"setting.host", "broker"}}).body;
    TEST_ASSERT_TRUE(has(body, R"("error":"rejected","message":"Use a full \"name\"")"));
}

void test_a_failed_store_answers_500()
{
    Device d;
    d.openWithNetwork();
    d.fakes.storage.writable = false;
    const FakeHttpServer::Response response = d.post("/api/settings", {{"setting.host", "broker"}});
    TEST_ASSERT_EQUAL_INT(500, response.status);
    TEST_ASSERT_TRUE(has(response.body, R"("error":"not-stored")"));
}

void test_changes_need_the_pages_own_header()
{
    Device d;
    d.openWithNetwork();
    for (const char* path : {"/api/settings", "/api/settings/check"})
    {
        const FakeHttpServer::Response response =
            d.fakes.http.request(HttpMethod::Post, path, {{"setting.host", "broker"}});
        TEST_ASSERT_EQUAL_INT(403, response.status);
    }
    TEST_ASSERT_TRUE(d.host.value().empty());
}

void test_an_empty_password_keeps_the_stored_one_and_clear_removes_it()
{
    Device d;
    d.openWithNetwork();
    TEST_ASSERT_TRUE(d.secret.set("hunter22"));

    TEST_ASSERT_EQUAL_INT(200, d.post("/api/settings", {{"setting.secret", ""}}).status);
    TEST_ASSERT_EQUAL_STRING("hunter22", d.secret.value().c_str());

    TEST_ASSERT_EQUAL_INT(200, d.post("/api/settings", {{"setting.secret", "hunter33"}}).status);
    TEST_ASSERT_EQUAL_STRING("hunter33", d.secret.value().c_str());

    TEST_ASSERT_EQUAL_INT(200, d.post("/api/settings", {{"clear.secret", "1"}}).status);
    TEST_ASSERT_TRUE(d.secret.value().empty());
    TEST_ASSERT_TRUE(has(d.get().body, R"("set":false)"));
}

void test_clear_only_applies_to_a_password()
{
    Device d;
    d.openWithNetwork();
    TEST_ASSERT_TRUE(d.host.set("broker"));
    TEST_ASSERT_EQUAL_INT(200, d.post("/api/settings", {{"clear.host", "1"}}).status);
    TEST_ASSERT_EQUAL_STRING("broker", d.host.value().c_str());
}

void test_a_required_password_needs_a_value_until_one_is_stored()
{
    Device d;
    espmgr::PasswordSetting& required = d.esp.settings().addPassword("token", "Token").required();
    d.openWithNetwork();

    FakeHttpServer::Response response = d.post("/api/settings", {{"setting.token", ""}});
    TEST_ASSERT_EQUAL_INT(400, response.status);
    TEST_ASSERT_TRUE(has(response.body, R"("error":"required")"));

    TEST_ASSERT_EQUAL_INT(200, d.post("/api/settings", {{"setting.token", "abc"}}).status);
    TEST_ASSERT_EQUAL_INT(200, d.post("/api/settings", {{"setting.token", ""}}).status);
    TEST_ASSERT_EQUAL_STRING("abc", required.value().c_str());

    response = d.post("/api/settings", {{"clear.token", "1"}});
    TEST_ASSERT_EQUAL_INT(400, response.status);
    TEST_ASSERT_EQUAL_STRING("abc", required.value().c_str());
}

// --- POST /api/settings/check ---

void test_check_answers_without_storing()
{
    Device d;
    d.open();
    FakeHttpServer::Response response = d.post("/api/settings/check", {{"setting.host", "broker"}});
    TEST_ASSERT_EQUAL_INT(200, response.status);
    TEST_ASSERT_EQUAL_STRING(R"({"ok":true})", response.body.c_str());
    TEST_ASSERT_TRUE(d.host.value().empty());
    TEST_ASSERT_TRUE(d.changes.empty());

    response = d.post("/api/settings/check", {{"setting.host", ""}});
    TEST_ASSERT_EQUAL_INT(400, response.status);
    TEST_ASSERT_TRUE(has(response.body, R"("key":"host","error":"required")"));
}

void test_check_only_looks_at_the_fields_it_gets()
{
    Device d;
    d.open();
    // The required host is on another screen of the wizard.
    TEST_ASSERT_EQUAL_INT(200, d.post("/api/settings/check", {{"setting.port", "80"}}).status);
}

// --- First setup ---

void test_settings_come_with_the_network_and_are_stored_when_it_works()
{
    Device d;
    d.open();
    FakeHttpServer::Response response =
        d.connect({{"setting.host", "broker"}, {"setting.secret", "hunter22"}});
    TEST_ASSERT_EQUAL_INT(200, response.status);
    TEST_ASSERT_TRUE(d.host.value().empty());
    TEST_ASSERT_TRUE(d.secret.isEmpty());

    d.completeTrial();

    TEST_ASSERT_EQUAL_STRING("broker", d.host.value().c_str());
    TEST_ASSERT_EQUAL_STRING("hunter22", d.secret.value().c_str());
    TEST_ASSERT_EQUAL_STRING("host", d.changes[0].c_str());
    TEST_ASSERT_TRUE(d.esp.settings().complete());
}

void test_a_failed_test_stores_nothing_and_try_again_sends_them_again()
{
    Device d;
    d.open();
    d.connect({{"setting.host", "first"}});
    d.fakes.wifi.failConnect(espmgr::FailureReason::WrongPassword);
    d.esp.process();
    TEST_ASSERT_TRUE(d.host.value().empty());
    TEST_ASSERT_TRUE(d.changes.empty());

    // The minimum delay passes, and the Owner tries again.
    d.fakes.clock.advance(2000);
    d.esp.process();
    TEST_ASSERT_EQUAL_INT(200, d.connect({{"setting.host", "second"}}).status);
    d.completeTrial();
    TEST_ASSERT_EQUAL_STRING("second", d.host.value().c_str());
}

void test_save_anyway_stores_them_too()
{
    Device d;
    d.open();
    const FakeHttpServer::Response response =
        d.connect({{"setting.host", "broker"}, {"saveAnyway", "1"}});
    TEST_ASSERT_EQUAL_INT(200, response.status);
    TEST_ASSERT_EQUAL_STRING("broker", d.host.value().c_str());
    TEST_ASSERT_EQUAL_UINT(1, d.esp.networks().count());
}

void test_a_bad_setting_refuses_the_network_before_testing()
{
    Device d;
    d.open();
    const FakeHttpServer::Response response =
        d.connect({{"setting.host", "broker"}, {"setting.port", "0"}});
    TEST_ASSERT_EQUAL_INT(400, response.status);
    TEST_ASSERT_TRUE(has(response.body, R"("error":"settings")"));
    TEST_ASSERT_TRUE(has(response.body, R"("key":"port","error":"out-of-range")"));
    TEST_ASSERT_EQUAL_UINT(0, d.fakes.wifi.connectCalls.size());
}

void test_first_setup_needs_the_required_settings()
{
    Device d;
    d.open();
    const FakeHttpServer::Response response = d.connect();
    TEST_ASSERT_EQUAL_INT(400, response.status);
    TEST_ASSERT_TRUE(has(response.body, R"("key":"host","error":"required")"));
    TEST_ASSERT_EQUAL_UINT(0, d.fakes.wifi.connectCalls.size());
}

// --- A device that already has Saved Networks ---

void test_adding_a_network_doesnt_need_the_settings()
{
    Device d;
    d.openWithNetwork();
    HttpRequest::Fields form = {{"ssid", "Office"}, {"password", kWifiPassword}};
    const FakeHttpServer::Response response =
        d.fakes.http.request(HttpMethod::Post, "/api/connect", std::move(form));
    TEST_ASSERT_EQUAL_INT(200, response.status);
    TEST_ASSERT_FALSE(d.esp.settings().complete());
}

void test_a_required_setting_added_later_doesnt_open_the_portal()
{
    Device d;
    TEST_ASSERT_TRUE(d.esp.networks().save("Home", kWifiPassword));
    d.esp.begin();
    d.esp.process();
    TEST_ASSERT_FALSE(d.esp.settings().complete());
    TEST_ASSERT_EQUAL(PortalState::Off, d.esp.portalState());
}

void test_a_sketch_without_settings_connects_as_before()
{
    FakePlatform fakes;
    fakes.wifi.scanStartSucceeds = false;
    EspManager esp(fakes.platform);
    esp.begin();
    esp.process();
    const FakeHttpServer::Response response = fakes.http.request(
        HttpMethod::Post, "/api/connect", {{"ssid", "Home"}, {"password", kWifiPassword}});
    TEST_ASSERT_EQUAL_INT(200, response.status);
}

void test_passwords_stay_out_of_the_log()
{
    Device d;
    d.open();
    d.connect({{"setting.host", "broker"}, {"setting.secret", "hunter22"}});
    d.completeTrial();
    TEST_ASSERT_FALSE(d.fakes.log.contains("hunter22"));
    TEST_ASSERT_FALSE(has(d.get().body, "hunter22"));
    TEST_ASSERT_FALSE(has(d.fakes.http.request(HttpMethod::Get, "/api/result").body, "hunter22"));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_the_page_gets_the_sections_and_fields);
    RUN_TEST(test_a_password_only_says_whether_it_is_set);
    RUN_TEST(test_a_sketch_without_settings_gets_an_empty_list);
    RUN_TEST(test_a_refused_declaration_is_not_shown);
    RUN_TEST(test_a_save_stores_the_values_and_answers_with_the_settings);
    RUN_TEST(test_a_refused_save_stores_nothing_and_lists_every_problem);
    RUN_TEST(test_the_developers_message_reaches_the_page);
    RUN_TEST(test_a_failed_store_answers_500);
    RUN_TEST(test_changes_need_the_pages_own_header);
    RUN_TEST(test_an_empty_password_keeps_the_stored_one_and_clear_removes_it);
    RUN_TEST(test_clear_only_applies_to_a_password);
    RUN_TEST(test_a_required_password_needs_a_value_until_one_is_stored);
    RUN_TEST(test_check_answers_without_storing);
    RUN_TEST(test_check_only_looks_at_the_fields_it_gets);
    RUN_TEST(test_settings_come_with_the_network_and_are_stored_when_it_works);
    RUN_TEST(test_a_failed_test_stores_nothing_and_try_again_sends_them_again);
    RUN_TEST(test_save_anyway_stores_them_too);
    RUN_TEST(test_a_bad_setting_refuses_the_network_before_testing);
    RUN_TEST(test_first_setup_needs_the_required_settings);
    RUN_TEST(test_adding_a_network_doesnt_need_the_settings);
    RUN_TEST(test_a_required_setting_added_later_doesnt_open_the_portal);
    RUN_TEST(test_a_sketch_without_settings_connects_as_before);
    RUN_TEST(test_passwords_stay_out_of_the_log);
    return UNITY_END();
}
