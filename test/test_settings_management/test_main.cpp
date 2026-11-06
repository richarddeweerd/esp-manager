// Settings in the Management Interface (US06.1.1, US06.1.4, EP09): only the Settings marked
// .editableInManagement() are shown or accepted there, while the Provisioning Portal keeps seeing
// them all, and the Device tab's Settings Reset.

#include <DigestClient.h>
#include <EspManager.h>
#include <FakePlatform.h>
#include <unity.h>

#include <string>
#include <vector>

using espmgr::DigestClient;
using espmgr::FakeHttpServer;
using espmgr::FakePlatform;
using espmgr::HttpMethod;
using espmgr::HttpRequest;
using espmgr::PortalState;
using espmgr::Setting;

namespace
{

const char* const kOwnerPassword = "owner password";
const char* const kWifiPassword = "correct horse";
const HttpRequest::Fields kFromPage = {{"X-Requested-With", "EspManager"}};

bool has(const std::string& text, const std::string& part)
{
    return text.find(part) != std::string::npos;
}

// An EspManager on fakes with the Management Interface on, connected, and the Owner logged in.
// `begin()` is left to the test, so it can declare more Settings first.
struct Device
{
    FakePlatform fakes;
    EspManager esp{fakes.platform};
    DigestClient client;
    std::vector<std::string> changes;
    uint32_t nextRandom = 0;

    Device()
    {
        fakes.wifi.scanStartSucceeds = false;
        client.realm = "esp-abcdef";
        client.password = kOwnerPassword;
        esp.management().enable();
        esp.onSettingChanged([this](const Setting& setting) { changes.push_back(setting.key()); });
    }

    void begin()
    {
        TEST_ASSERT_TRUE(esp.management().setPassword(kOwnerPassword));
        TEST_ASSERT_TRUE(esp.networks().save("Home", kWifiPassword));
        esp.begin();
        esp.process();
        fakes.wifi.completeConnect();
        esp.process();
        TEST_ASSERT_TRUE(esp.management().active());
    }

    // What a browser does: ask, take the challenge, and ask again with the password.
    FakeHttpServer::Response send(HttpMethod method, const std::string& path,
                                  HttpRequest::Fields form, HttpRequest::Fields headers)
    {
        fakes.http.localIp = fakes.wifi.ip;
        // The fake clock's random values are 0 by default, which would repeat the nonce.
        for (int word = 0; word < 4; ++word)
        {
            fakes.clock.randomValues.push_back(++nextRandom);
        }
        const FakeHttpServer::Response challenge = fakes.http.request(method, path, form, headers);
        TEST_ASSERT_EQUAL_INT(401, challenge.status);
        const std::string nonce = DigestClient::nonceFrom(challenge.header("WWW-Authenticate"));
        headers.push_back({"Authorization", client.header(nonce, toName(method), path)});
        FakeHttpServer::Response response =
            fakes.http.request(method, path, std::move(form), std::move(headers));
        esp.process();
        return response;
    }

    static const char* toName(HttpMethod method)
    {
        return method == HttpMethod::Get ? "GET" : "POST";
    }

    FakeHttpServer::Response get()
    {
        return send(HttpMethod::Get, "/api/settings", {}, {});
    }

    FakeHttpServer::Response post(const std::string& path, HttpRequest::Fields form = {})
    {
        return send(HttpMethod::Post, path, std::move(form), kFromPage);
    }
};

}  // namespace

void setUp()
{
}

void tearDown()
{
}

// --- What it shows ---

void test_only_marked_settings_are_shown()
{
    Device d;
    d.esp.settings().section("MQTT").addText("host", "Server").editableInManagement();
    d.esp.settings().section("MQTT").addText("topic", "Topic", "hidden-value");
    d.esp.settings().section("Private").addText("token", "Token", "hidden-too");
    d.begin();
    const FakeHttpServer::Response response = d.get();
    TEST_ASSERT_EQUAL_INT(200, response.status);
    TEST_ASSERT_TRUE(has(response.body, R"("key":"host")"));
    TEST_ASSERT_FALSE(has(response.body, "topic"));
    TEST_ASSERT_FALSE(has(response.body, "hidden-value"));
    // A Section with nothing to show isn't there at all.
    TEST_ASSERT_FALSE(has(response.body, "Private"));
    TEST_ASSERT_FALSE(has(response.body, "token"));
}

void test_without_a_marked_setting_there_is_nothing_for_the_tab()
{
    Device d;
    d.esp.settings().addText("host", "Server");
    d.begin();
    TEST_ASSERT_EQUAL_STRING(R"({"complete":true,"hasSettings":true,"sections":[]})",
                             d.get().body.c_str());
}

void test_a_sketch_without_settings_says_so()
{
    Device d;
    d.begin();
    TEST_ASSERT_EQUAL_STRING(R"({"complete":true,"hasSettings":false,"sections":[]})",
                             d.get().body.c_str());
}

void test_the_badge_only_counts_marked_settings()
{
    Device hiddenEmpty;
    hiddenEmpty.esp.settings().addText("host", "Server").required();
    hiddenEmpty.esp.settings().addText("name", "Name").editableInManagement();
    hiddenEmpty.begin();
    TEST_ASSERT_TRUE(has(hiddenEmpty.get().body, R"("complete":true)"));
    TEST_ASSERT_FALSE(hiddenEmpty.esp.settings().complete());

    Device markedEmpty;
    markedEmpty.esp.settings().addText("host", "Server").required().editableInManagement();
    markedEmpty.begin();
    TEST_ASSERT_TRUE(has(markedEmpty.get().body, R"("complete":false)"));
}

void test_a_password_value_never_goes_to_the_page()
{
    Device d;
    auto& secret = d.esp.settings().addPassword("secret", "Secret").editableInManagement();
    d.begin();
    TEST_ASSERT_TRUE(secret.set("stored secret"));
    const FakeHttpServer::Response response = d.get();
    TEST_ASSERT_TRUE(has(response.body, R"("set":true)"));
    TEST_ASSERT_FALSE(has(response.body, "stored secret"));
}

// --- What it accepts ---

void test_a_marked_setting_can_be_saved()
{
    Device d;
    auto& host = d.esp.settings().addText("host", "Server").editableInManagement();
    d.begin();
    const FakeHttpServer::Response response =
        d.post("/api/settings", {{"setting.host", "mqtt.local"}});
    TEST_ASSERT_EQUAL_INT(200, response.status);
    TEST_ASSERT_EQUAL_STRING("mqtt.local", host.value().c_str());
    TEST_ASSERT_TRUE(has(response.body, R"("value":"mqtt.local")"));
    TEST_ASSERT_EQUAL_size_t(1, d.changes.size());
}

void test_saving_answers_with_marked_settings_only()
{
    Device d;
    d.esp.settings().addText("host", "Server").editableInManagement();
    d.esp.settings().addText("topic", "Topic", "hidden-value");
    d.begin();
    const FakeHttpServer::Response response =
        d.post("/api/settings", {{"setting.host", "mqtt.local"}});
    TEST_ASSERT_EQUAL_INT(200, response.status);
    TEST_ASSERT_FALSE(has(response.body, "hidden-value"));
}

void test_an_unmarked_setting_is_refused()
{
    Device d;
    auto& topic = d.esp.settings().addText("topic", "Topic", "before");
    d.begin();
    const FakeHttpServer::Response response = d.post("/api/settings", {{"setting.topic", "after"}});
    TEST_ASSERT_EQUAL_INT(400, response.status);
    TEST_ASSERT_TRUE(has(response.body, R"("key":"topic","error":"unknown-setting")"));
    TEST_ASSERT_EQUAL_STRING("before", topic.value().c_str());
    TEST_ASSERT_TRUE(d.changes.empty());
}

void test_an_unmarked_setting_answers_like_one_that_doesnt_exist()
{
    Device d;
    d.esp.settings().addText("topic", "Topic");
    d.begin();
    const std::string hidden = d.post("/api/settings", {{"setting.topic", "x"}}).body;
    const std::string unknown = d.post("/api/settings", {{"setting.nope", "x"}}).body;
    TEST_ASSERT_EQUAL_STRING(hidden.substr(hidden.find("\"error\":\"unknown")).c_str(),
                             unknown.substr(unknown.find("\"error\":\"unknown")).c_str());
}

void test_a_refused_setting_stops_the_whole_save()
{
    Device d;
    auto& host = d.esp.settings().addText("host", "Server").editableInManagement();
    d.esp.settings().addText("topic", "Topic");
    d.begin();
    const FakeHttpServer::Response response =
        d.post("/api/settings", {{"setting.host", "mqtt.local"}, {"setting.topic", "after"}});
    TEST_ASSERT_EQUAL_INT(400, response.status);
    TEST_ASSERT_TRUE(host.value().empty());
}

void test_an_unmarked_password_cant_be_cleared()
{
    Device d;
    auto& secret = d.esp.settings().addPassword("secret", "Secret");
    d.begin();
    TEST_ASSERT_TRUE(secret.set("stored secret"));
    const FakeHttpServer::Response response = d.post("/api/settings", {{"clear.secret", "1"}});
    TEST_ASSERT_EQUAL_INT(400, response.status);
    TEST_ASSERT_EQUAL_STRING("stored secret", secret.value().c_str());
}

void test_an_empty_value_for_an_unmarked_password_is_refused_too()
{
    Device d;
    auto& secret = d.esp.settings().addPassword("secret", "Secret");
    d.begin();
    TEST_ASSERT_TRUE(secret.set("stored secret"));
    // fieldsFrom() would drop this one, and the request would look like a success.
    TEST_ASSERT_EQUAL_INT(400, d.post("/api/settings", {{"setting.secret", ""}}).status);
    TEST_ASSERT_EQUAL_STRING("stored secret", secret.value().c_str());
}

void test_a_marked_password_can_be_kept_and_cleared()
{
    Device d;
    auto& secret = d.esp.settings().addPassword("secret", "Secret").editableInManagement();
    d.begin();
    TEST_ASSERT_TRUE(secret.set("stored secret"));
    TEST_ASSERT_EQUAL_INT(200, d.post("/api/settings", {{"setting.secret", ""}}).status);
    TEST_ASSERT_EQUAL_STRING("stored secret", secret.value().c_str());
    TEST_ASSERT_EQUAL_INT(200, d.post("/api/settings", {{"clear.secret", "1"}}).status);
    TEST_ASSERT_TRUE(secret.value().empty());
}

void test_check_refuses_an_unmarked_setting()
{
    Device d;
    d.esp.settings().addText("topic", "Topic");
    d.esp.settings().addText("host", "Server").editableInManagement();
    d.begin();
    TEST_ASSERT_EQUAL_INT(400, d.post("/api/settings/check", {{"setting.topic", "x"}}).status);
    TEST_ASSERT_EQUAL_INT(200, d.post("/api/settings/check", {{"setting.host", "x"}}).status);
}

void test_a_bad_value_gives_the_setting_its_error()
{
    Device d;
    d.esp.settings().addNumber("port", "Port", 1883).range(1, 65535).editableInManagement();
    d.begin();
    const FakeHttpServer::Response response = d.post("/api/settings", {{"setting.port", "0"}});
    TEST_ASSERT_EQUAL_INT(400, response.status);
    TEST_ASSERT_TRUE(has(response.body, R"("key":"port","error":"out-of-range")"));
}

void test_the_settings_routes_need_the_password()
{
    Device d;
    auto& host = d.esp.settings().addText("host", "Server", "before").editableInManagement();
    d.begin();
    d.fakes.http.localIp = d.fakes.wifi.ip;
    TEST_ASSERT_EQUAL_INT(401, d.fakes.http.request(HttpMethod::Get, "/api/settings").status);
    TEST_ASSERT_EQUAL_INT(
        401, d.fakes.http
                 .request(HttpMethod::Post, "/api/settings", {{"setting.host", "after"}}, kFromPage)
                 .status);
    TEST_ASSERT_EQUAL_STRING("before", host.value().c_str());
}

void test_a_save_needs_the_header()
{
    Device d;
    auto& host = d.esp.settings().addText("host", "Server", "before").editableInManagement();
    d.begin();
    const FakeHttpServer::Response response =
        d.send(HttpMethod::Post, "/api/settings", {{"setting.host", "after"}}, {});
    TEST_ASSERT_EQUAL_INT(403, response.status);
    TEST_ASSERT_EQUAL_STRING("before", host.value().c_str());
}

// --- The Provisioning Portal sees them all ---

void test_the_portal_still_shows_and_accepts_every_setting()
{
    Device d;
    auto& topic = d.esp.settings().addText("topic", "Topic", "before");
    d.esp.settings().addText("host", "Server").editableInManagement();
    d.begin();
    d.esp.startPortal();
    d.esp.process();
    TEST_ASSERT_EQUAL(PortalState::Active, d.esp.portalState());
    d.fakes.http.localIp = d.fakes.wifi.apIp;
    const FakeHttpServer::Response shown = d.fakes.http.request(HttpMethod::Get, "/api/settings");
    TEST_ASSERT_TRUE(has(shown.body, R"("key":"topic")"));
    TEST_ASSERT_TRUE(has(shown.body, R"("key":"host")"));
    const FakeHttpServer::Response saved = d.fakes.http.request(
        HttpMethod::Post, "/api/settings", {{"setting.topic", "after"}}, kFromPage);
    TEST_ASSERT_EQUAL_INT(200, saved.status);
    TEST_ASSERT_EQUAL_STRING("after", topic.value().c_str());
}

// --- Settings Reset ---

void test_a_settings_reset_restores_every_setting()
{
    Device d;
    auto& host = d.esp.settings().addText("host", "Server", "default").editableInManagement();
    auto& topic = d.esp.settings().addText("topic", "Topic", "default");
    d.begin();
    TEST_ASSERT_TRUE(host.set("changed"));
    TEST_ASSERT_TRUE(topic.set("changed"));
    d.esp.process();
    d.changes.clear();

    const FakeHttpServer::Response response = d.post("/api/settings-reset");
    TEST_ASSERT_EQUAL_INT(200, response.status);
    TEST_ASSERT_EQUAL_STRING(R"({"ok":true})", response.body.c_str());
    // It has already happened when the answer goes out, so the page can load the new values.
    TEST_ASSERT_EQUAL_STRING("default", host.value().c_str());
    TEST_ASSERT_EQUAL_STRING("default", topic.value().c_str());
    TEST_ASSERT_EQUAL_size_t(2, d.changes.size());
}

void test_a_settings_reset_keeps_the_network_and_the_password()
{
    Device d;
    d.esp.settings().addText("host", "Server").editableInManagement();
    d.begin();
    TEST_ASSERT_EQUAL_INT(200, d.post("/api/settings-reset").status);
    TEST_ASSERT_EQUAL_size_t(1, d.esp.networks().count());
    TEST_ASSERT_TRUE(d.esp.management().hasPassword());
    TEST_ASSERT_TRUE(d.esp.management().active());
    TEST_ASSERT_EQUAL_INT(200, d.get().status);
}

void test_a_settings_reset_that_fails_says_so()
{
    Device d;
    auto& host = d.esp.settings().addText("host", "Server", "default");
    d.begin();
    TEST_ASSERT_TRUE(host.set("changed"));
    d.fakes.storage.clearable = false;
    const FakeHttpServer::Response response = d.post("/api/settings-reset");
    TEST_ASSERT_EQUAL_INT(500, response.status);
    TEST_ASSERT_EQUAL_STRING("changed", host.value().c_str());
}

void test_a_settings_reset_needs_the_password_and_the_header()
{
    Device d;
    auto& host = d.esp.settings().addText("host", "Server", "default");
    d.begin();
    TEST_ASSERT_TRUE(host.set("changed"));
    d.fakes.http.localIp = d.fakes.wifi.ip;
    TEST_ASSERT_EQUAL_INT(
        401, d.fakes.http.request(HttpMethod::Post, "/api/settings-reset", {}, kFromPage).status);
    TEST_ASSERT_EQUAL_INT(403, d.send(HttpMethod::Post, "/api/settings-reset", {}, {}).status);
    TEST_ASSERT_EQUAL_STRING("changed", host.value().c_str());
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_only_marked_settings_are_shown);
    RUN_TEST(test_without_a_marked_setting_there_is_nothing_for_the_tab);
    RUN_TEST(test_a_sketch_without_settings_says_so);
    RUN_TEST(test_the_badge_only_counts_marked_settings);
    RUN_TEST(test_a_password_value_never_goes_to_the_page);
    RUN_TEST(test_a_marked_setting_can_be_saved);
    RUN_TEST(test_saving_answers_with_marked_settings_only);
    RUN_TEST(test_an_unmarked_setting_is_refused);
    RUN_TEST(test_an_unmarked_setting_answers_like_one_that_doesnt_exist);
    RUN_TEST(test_a_refused_setting_stops_the_whole_save);
    RUN_TEST(test_an_unmarked_password_cant_be_cleared);
    RUN_TEST(test_an_empty_value_for_an_unmarked_password_is_refused_too);
    RUN_TEST(test_a_marked_password_can_be_kept_and_cleared);
    RUN_TEST(test_check_refuses_an_unmarked_setting);
    RUN_TEST(test_a_bad_value_gives_the_setting_its_error);
    RUN_TEST(test_the_settings_routes_need_the_password);
    RUN_TEST(test_a_save_needs_the_header);
    RUN_TEST(test_the_portal_still_shows_and_accepts_every_setting);
    RUN_TEST(test_a_settings_reset_restores_every_setting);
    RUN_TEST(test_a_settings_reset_keeps_the_network_and_the_password);
    RUN_TEST(test_a_settings_reset_that_fails_says_so);
    RUN_TEST(test_a_settings_reset_needs_the_password_and_the_header);
    return UNITY_END();
}
