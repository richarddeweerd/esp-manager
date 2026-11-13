// Firmware updates in the Management Interface (F17.2, F17.4, F17.6, US17.8.3, F17.11): the
// routes behind the password, and the Owner's Updates Section.

#include <DigestClient.h>
#include <EspManager.h>
#include <FakePlatform.h>
#include <unity.h>

#include <string>

using espmgr::DigestClient;
using espmgr::FakeHttpServer;
using espmgr::FakePlatform;
using espmgr::HttpMethod;
using espmgr::HttpRequest;
using espmgr::InstallPolicy;
using espmgr::Setting;
using espmgr::UpdateDriver;
using espmgr::UpdateManager;
using espmgr::UpdateState;

namespace
{

const char* const kOwnerPassword = "owner password";
const HttpRequest::Fields kFromPage = {{"X-Requested-With", "EspManager"}};
const char* const kHashHex = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";

bool has(const std::string& text, const std::string& part)
{
    return text.find(part) != std::string::npos;
}

std::string manifest(const std::string& version)
{
    return R"({"version":")" + version + R"(","url":"fw.bin","size":1000,"sha256":")" + kHashHex +
           R"(","signature":"MEUCIQ==","notes":"Better"})";
}

// An EspManager on fakes with the Management Interface on, running 1.0.0 with updates set up.
// `begin()` is left to the test.
struct Device
{
    FakePlatform fakes;
    EspManager esp{fakes.platform};
    DigestClient client;
    uint32_t nextRandom = 0;

    explicit Device(bool updates = true)
    {
        fakes.wifi.scanStartSucceeds = false;
        client.realm = "esp-abcdef";
        client.password = kOwnerPassword;
        esp.management().enable();
        esp.setFirmwareVersion("1.0.0");
        if (updates)
        {
            TEST_ASSERT_TRUE(esp.updates().setServer("https://updates.example.com/lamp/"));
            esp.updates().setPublicKey("KEY");
        }
    }

    void begin()
    {
        TEST_ASSERT_TRUE(esp.management().setPassword(kOwnerPassword));
        TEST_ASSERT_TRUE(esp.networks().save("Home", "correct horse"));
        esp.begin();
        esp.process();
        fakes.wifi.completeConnect();
        esp.process();
        TEST_ASSERT_TRUE(esp.management().active());
    }

    FakeHttpServer::Response send(HttpMethod method, const std::string& path,
                                  HttpRequest::Fields form, HttpRequest::Fields headers)
    {
        fakes.http.localIp = fakes.wifi.ip;
        for (int word = 0; word < 4; ++word)
        {
            fakes.clock.randomValues.push_back(++nextRandom);
        }
        const FakeHttpServer::Response challenge = fakes.http.request(method, path, form, headers);
        if (challenge.status != 401)
        {
            return challenge;
        }
        const std::string nonce = DigestClient::nonceFrom(challenge.header("WWW-Authenticate"));
        headers.push_back({"Authorization",
                           client.header(nonce, method == HttpMethod::Get ? "GET" : "POST", path)});
        FakeHttpServer::Response response =
            fakes.http.request(method, path, std::move(form), std::move(headers));
        esp.process();
        return response;
    }

    FakeHttpServer::Response get(const std::string& path)
    {
        return send(HttpMethod::Get, path, {}, {});
    }

    FakeHttpServer::Response post(const std::string& path, HttpRequest::Fields headers = kFromPage)
    {
        return send(HttpMethod::Post, path, {}, std::move(headers));
    }

    // Has the device find 2.0.0.
    void findUpdate()
    {
        TEST_ASSERT_EQUAL_INT(200, post("/api/updates/check").status);
        fakes.updates.finishFetch(manifest("2.0.0"));
        esp.process();
        TEST_ASSERT_EQUAL(UpdateState::Available, esp.updates().state());
    }
};

}  // namespace

void setUp()
{
}

void tearDown()
{
}

void test_the_routes_are_behind_the_password()
{
    Device d;
    d.begin();
    d.fakes.http.localIp = d.fakes.wifi.ip;
    TEST_ASSERT_EQUAL_INT(401,
                          d.fakes.http.request(HttpMethod::Get, "/api/updates", {}, {}).status);
    TEST_ASSERT_EQUAL_INT(
        401, d.fakes.http.request(HttpMethod::Post, "/api/updates/check", {}, kFromPage).status);
    // The POSTs also need the page's header.
    TEST_ASSERT_EQUAL_INT(403, d.post("/api/updates/check", {}).status);
    TEST_ASSERT_EQUAL_INT(403, d.post("/api/updates/install", {}).status);
    TEST_ASSERT_EQUAL(0, d.fakes.updates.fetches);
}

void test_without_updates_there_are_no_routes()
{
    Device d(false);
    d.begin();
    TEST_ASSERT_EQUAL_INT(404, d.get("/api/updates").status);
    TEST_ASSERT_TRUE(has(d.get("/api/status").body, R"("update":{"configured":false)"));
}

void test_get_updates_reports_the_state()
{
    Device d;
    d.begin();
    const FakeHttpServer::Response response = d.get("/api/updates");
    TEST_ASSERT_EQUAL_INT(200, response.status);
    TEST_ASSERT_TRUE(
        has(response.body, R"("installed":"1.0.0","channel":"stable","state":"idle")"));
    TEST_ASSERT_TRUE(
        has(response.body, R"("autoCheck":false,"intervalHours":24,"policy":"notify")"));
    TEST_ASSERT_TRUE(has(response.body, R"("last":{"outcome":"none")"));
    TEST_ASSERT_TRUE(has(d.get("/api/status").body, R"("update":{"configured":true)"));
}

void test_check_and_install_from_the_page()
{
    Device d;
    d.begin();
    d.findUpdate();
    const FakeHttpServer::Response found = d.get("/api/updates");
    TEST_ASSERT_TRUE(has(found.body, R"("state":"available")"));
    TEST_ASSERT_TRUE(
        has(found.body, R"("available":{"version":"2.0.0","notes":"Better","size":1000})"));
    TEST_ASSERT_EQUAL_INT(200, d.post("/api/updates/install").status);
    TEST_ASSERT_EQUAL(UpdateState::Downloading, d.esp.updates().state());
    d.fakes.updates.receive(500);
    d.fakes.clock.advance(300);
    d.esp.process();
    TEST_ASSERT_TRUE(has(d.get("/api/updates").body, R"("state":"downloading","progress":50)"));
    // A second check or install while downloading is refused.
    TEST_ASSERT_EQUAL_INT(409, d.post("/api/updates/check").status);
    const FakeHttpServer::Response busy = d.post("/api/updates/install");
    TEST_ASSERT_EQUAL_INT(409, busy.status);
    TEST_ASSERT_TRUE(has(busy.body, R"({"error":"busy"})"));
}

void test_a_refused_install_says_why()
{
    Device d;
    d.begin();
    TEST_ASSERT_TRUE(has(d.post("/api/updates/install").body, R"({"error":"none-available"})"));
    d.findUpdate();
    d.fakes.updates.slot = 10;
    const FakeHttpServer::Response response = d.post("/api/updates/install");
    TEST_ASSERT_EQUAL_INT(409, response.status);
    TEST_ASSERT_TRUE(has(response.body, R"({"error":"no-space"})"));
    TEST_ASSERT_TRUE(has(d.get("/api/updates").body, R"("lastError":{"code":"no-space")"));
}

void test_the_owner_can_edit_the_updates_section()
{
    Device d;
    d.esp.updates().setAutoCheck(true);
    TEST_ASSERT_TRUE(d.esp.updates().setCheckInterval(6UL * 60 * 60 * 1000));
    d.esp.updates().setInstallPolicy(InstallPolicy::Install);
    d.esp.updates().ownerCanEdit().editableInManagement();
    d.begin();
    const FakeHttpServer::Response settings = d.get("/api/settings");
    TEST_ASSERT_TRUE(has(settings.body, R"("title":"Updates")"));
    TEST_ASSERT_TRUE(has(settings.body, R"("key":"upd-auto")"));
    TEST_ASSERT_TRUE(has(settings.body, R"("key":"upd-hours")"));
    TEST_ASSERT_TRUE(has(settings.body, R"("key":"upd-policy")"));
    TEST_ASSERT_TRUE(has(settings.body, R"("key":"upd-channel")"));
    // The Developer's values are the defaults.
    TEST_ASSERT_TRUE(d.esp.updates().autoCheck());
    TEST_ASSERT_EQUAL_UINT32(6UL * 60 * 60 * 1000, d.esp.updates().checkInterval());
    TEST_ASSERT_EQUAL(InstallPolicy::Install, d.esp.updates().installPolicy());

    const FakeHttpServer::Response saved = d.send(HttpMethod::Post, "/api/settings",
                                                  {{"setting.upd-auto", "0"},
                                                   {"setting.upd-hours", "48"},
                                                   {"setting.upd-policy", "notify"},
                                                   {"setting.upd-channel", "beta"}},
                                                  kFromPage);
    TEST_ASSERT_EQUAL_INT(200, saved.status);
    TEST_ASSERT_FALSE(d.esp.updates().autoCheck());
    TEST_ASSERT_EQUAL_UINT32(48UL * 60 * 60 * 1000, d.esp.updates().checkInterval());
    TEST_ASSERT_EQUAL(InstallPolicy::Notify, d.esp.updates().installPolicy());
    TEST_ASSERT_EQUAL_STRING("beta", d.esp.updates().channel().c_str());
    TEST_ASSERT_EQUAL_STRING("https://updates.example.com/lamp/beta.json",
                             d.esp.updates().manifestUrl().c_str());

    // A channel that isn't a channel name is refused.
    const FakeHttpServer::Response bad =
        d.send(HttpMethod::Post, "/api/settings", {{"setting.upd-channel", "be/ta"}}, kFromPage);
    TEST_ASSERT_EQUAL_INT(400, bad.status);
    TEST_ASSERT_EQUAL_STRING("beta", d.esp.updates().channel().c_str());

    // A Settings Reset brings the Developer's values back.
    TEST_ASSERT_TRUE(d.esp.settingsReset());
    d.esp.process();
    TEST_ASSERT_TRUE(d.esp.updates().autoCheck());
    TEST_ASSERT_EQUAL_STRING("stable", d.esp.updates().channel().c_str());
}

void test_the_section_is_hidden_in_management_unless_marked()
{
    Device d;
    d.esp.updates().ownerCanEdit();
    d.begin();
    TEST_ASSERT_FALSE(has(d.get("/api/settings").body, "upd-auto"));
}

void test_turning_auto_check_on_schedules_a_check()
{
    Device d;
    d.esp.updates().ownerCanEdit().editableInManagement();
    d.begin();
    d.send(HttpMethod::Post, "/api/settings", {{"setting.upd-auto", "1"}}, kFromPage);
    d.esp.process();
    d.fakes.clock.advance(UpdateManager::kFirstCheckDelayMs + UpdateManager::kCheckJitterMs);
    d.esp.process();
    TEST_ASSERT_EQUAL(1, d.fakes.updates.fetches);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_the_routes_are_behind_the_password);
    RUN_TEST(test_without_updates_there_are_no_routes);
    RUN_TEST(test_get_updates_reports_the_state);
    RUN_TEST(test_check_and_install_from_the_page);
    RUN_TEST(test_a_refused_install_says_why);
    RUN_TEST(test_the_owner_can_edit_the_updates_section);
    RUN_TEST(test_the_section_is_hidden_in_management_unless_marked);
    RUN_TEST(test_turning_auto_check_on_schedules_a_check);
    return UNITY_END();
}
