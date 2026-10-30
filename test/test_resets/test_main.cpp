// The resets and restart (EP09: US09.1.1, US09.1.2, US09.2.1, US09.2.2, US09.3.1). Each erases
// exactly what the glossary says: a Network Reset only the Saved Networks, a Settings Reset only
// the Settings, a Factory Reset everything.

#include <DigestClient.h>
#include <EspManager.h>
#include <FakePlatform.h>
#include <unity.h>

#include <string>
#include <vector>

using espmgr::ConnectionState;
using espmgr::DigestClient;
using espmgr::FailureReason;
using espmgr::FakeHttpServer;
using espmgr::FakePlatform;
using espmgr::HttpMethod;
using espmgr::HttpRequest;
using espmgr::ManagementInterface;
using espmgr::PortalState;

namespace
{

const char* const kPassword = "owner password";

// A connected EspManager with a Saved Network, a Management Interface password and some of the
// sketch's own data in storage.
struct Device
{
    FakePlatform fakes;
    EspManager esp{fakes.platform};
    std::vector<std::string> events;
    uint32_t logins = 0;

    Device()
    {
        fakes.wifi.scanStartSucceeds = false;
        esp.management().enable();
        esp.onDisconnected([this](FailureReason reason)
                           { events.push_back(std::string("disconnected ") + toString(reason)); })
            .onPortalStarted([this](const std::string&) { events.push_back("portal"); });
        TEST_ASSERT_TRUE(fakes.storage.putString("sketch", "colour", "blue"));
        TEST_ASSERT_TRUE(esp.networks().save("Home", "correct horse"));
        TEST_ASSERT_TRUE(esp.management().setPassword(kPassword));
        esp.begin();
        esp.process();
        fakes.wifi.completeConnect();
        esp.process();
        TEST_ASSERT_EQUAL(ConnectionState::Connected, esp.state());
        events.clear();
    }

    // A POST to the Management Interface, logged in, with or without the page's header.
    FakeHttpServer::Response post(const std::string& path, bool header = true)
    {
        fakes.http.localIp = fakes.wifi.ip;
        ++logins;
        fakes.clock.randomValues = {logins, logins, logins, logins};
        const FakeHttpServer::Response challenge = fakes.http.request(HttpMethod::Post, path);
        TEST_ASSERT_EQUAL_INT(401, challenge.status);
        DigestClient client;
        client.realm = esp.management().realm();
        client.password = kPassword;
        HttpRequest::Fields headers = {
            {"Authorization",
             client.header(DigestClient::nonceFrom(challenge.header("WWW-Authenticate")), "POST",
                           path)}};
        if (header)
        {
            headers.emplace_back("X-Requested-With", "EspManager");
        }
        return fakes.http.request(HttpMethod::Post, path, {}, std::move(headers));
    }

    void wait(uint64_t ms)
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

// --- Network Reset ---

void test_network_reset_forgets_the_networks_and_leaves()
{
    Device d;
    TEST_ASSERT_TRUE(d.esp.networkReset());
    TEST_ASSERT_EQUAL_UINT32(0, d.esp.networks().count());
    TEST_ASSERT_FALSE(d.fakes.storage.getBytes("espmgr-wifi", "networks").has_value());
    TEST_ASSERT_EQUAL_INT(1, d.fakes.wifi.disconnectCalls);
    TEST_ASSERT_EQUAL(ConnectionState::Idle, d.esp.state());
    d.esp.process();
    TEST_ASSERT_EQUAL_STRING("disconnected none", d.events.at(0).c_str());
}

void test_network_reset_keeps_everything_else()
{
    Device d;
    d.esp.networkReset();
    TEST_ASSERT_EQUAL_STRING("blue", d.fakes.storage.getString("sketch", "colour").value().c_str());
    TEST_ASSERT_TRUE(d.esp.management().hasPassword());
    TEST_ASSERT_EQUAL_INT(0, d.fakes.system.restarts);
}

void test_after_a_network_reset_the_portal_opens()
{
    Device d;
    d.esp.networkReset();
    d.esp.process();
    TEST_ASSERT_EQUAL(PortalState::Active, d.esp.portalState());
    // And the device doesn't try to reconnect.
    d.wait(60000);
    TEST_ASSERT_EQUAL(ConnectionState::Idle, d.esp.state());
}

void test_network_reset_while_idle_changes_nothing_else()
{
    FakePlatform fakes;
    EspManager esp{fakes.platform};
    esp.begin();
    TEST_ASSERT_TRUE(esp.networkReset());
    TEST_ASSERT_EQUAL_INT(0, fakes.wifi.disconnectCalls);
    TEST_ASSERT_EQUAL(ConnectionState::Idle, esp.state());
}

// A reset during a test in the Provisioning Portal ends the test instead of leaving the page
// waiting.
void test_network_reset_during_a_portal_test_fails_the_test()
{
    FakePlatform fakes;
    fakes.wifi.scanStartSucceeds = false;
    EspManager esp{fakes.platform};
    esp.begin();
    esp.process();
    fakes.http.request(HttpMethod::Post, "/api/connect",
                       {{"ssid", "Home"}, {"password", "correct horse"}});
    esp.process();
    esp.networkReset();
    esp.process();
    TEST_ASSERT_EQUAL_STRING(R"({"state":"failed","ssid":"Home","reason":"other"})",
                             fakes.http.request(HttpMethod::Get, "/api/result").body.c_str());
}

// --- Settings Reset ---

void test_settings_reset_keeps_networks_and_data_until_there_are_settings()
{
    Device d;
    TEST_ASSERT_TRUE(d.esp.settingsReset());
    TEST_ASSERT_EQUAL_UINT32(1, d.esp.networks().count());
    TEST_ASSERT_EQUAL(ConnectionState::Connected, d.esp.state());
    TEST_ASSERT_EQUAL_STRING("blue", d.fakes.storage.getString("sketch", "colour").value().c_str());
    TEST_ASSERT_TRUE(d.esp.management().hasPassword());
}

// --- Factory Reset ---

void test_factory_reset_erases_everything()
{
    Device d;
    TEST_ASSERT_TRUE(d.esp.factoryReset());
    TEST_ASSERT_TRUE(d.fakes.storage.data.empty());
    TEST_ASSERT_EQUAL_UINT32(0, d.esp.networks().count());
    TEST_ASSERT_FALSE(d.esp.management().hasPassword());
    TEST_ASSERT_EQUAL(ConnectionState::Idle, d.esp.state());
}

void test_factory_reset_restarts_a_moment_later()
{
    Device d;
    d.esp.factoryReset();
    d.esp.process();
    TEST_ASSERT_EQUAL_INT(0, d.fakes.system.restarts);
    d.wait(1000);
    TEST_ASSERT_EQUAL_INT(1, d.fakes.system.restarts);
}

void test_failed_erase_is_reported_and_still_restarts()
{
    Device d;
    struct FailingStorage : espmgr::FakeStorage
    {
        bool eraseAll() override
        {
            return false;
        }
    };
    // Swap the data into storage that can't be erased.
    FailingStorage failing;
    failing.data = d.fakes.storage.data;
    espmgr::Platform platform{d.fakes.wifi, failing,     d.fakes.clock,
                              d.fakes.http, d.fakes.log, d.fakes.system};
    EspManager esp{platform};
    esp.begin();
    TEST_ASSERT_FALSE(esp.factoryReset());
    d.fakes.clock.advance(1000);
    esp.process();
    TEST_ASSERT_EQUAL_INT(1, d.fakes.system.restarts);
}

// --- Restart ---

void test_restart_waits_for_process_and_leaves_the_network_first()
{
    Device d;
    d.esp.restart();
    TEST_ASSERT_EQUAL_INT(0, d.fakes.system.restarts);
    d.wait(100);
    TEST_ASSERT_EQUAL_INT(0, d.fakes.system.restarts);
    d.wait(400);
    TEST_ASSERT_EQUAL_INT(1, d.fakes.system.restarts);
    TEST_ASSERT_EQUAL_INT(1, d.fakes.wifi.disconnectCalls);
    // Nothing is erased.
    TEST_ASSERT_EQUAL_UINT32(1, d.esp.networks().count());
    TEST_ASSERT_TRUE(d.esp.management().hasPassword());
}

// --- From the Management Interface ---

void test_device_tab_actions_run_after_their_answer()
{
    {
        Device d;
        const FakeHttpServer::Response response = d.post("/api/restart");
        TEST_ASSERT_EQUAL_INT(200, response.status);
        TEST_ASSERT_EQUAL_STRING(R"({"ok":true})", response.body.c_str());
        d.esp.process();
        TEST_ASSERT_EQUAL_INT(0, d.fakes.system.restarts);
        d.wait(ManagementInterface::kActionDelayMs);
        d.wait(1000);
        TEST_ASSERT_EQUAL_INT(1, d.fakes.system.restarts);
    }
    {
        Device d;
        TEST_ASSERT_EQUAL_INT(200, d.post("/api/network-reset").status);
        TEST_ASSERT_EQUAL_UINT32(1, d.esp.networks().count());
        d.wait(ManagementInterface::kActionDelayMs);
        TEST_ASSERT_EQUAL_UINT32(0, d.esp.networks().count());
        TEST_ASSERT_EQUAL_INT(0, d.fakes.system.restarts);
    }
    {
        Device d;
        TEST_ASSERT_EQUAL_INT(200, d.post("/api/factory-reset").status);
        d.wait(ManagementInterface::kActionDelayMs);
        TEST_ASSERT_TRUE(d.fakes.storage.data.empty());
        d.wait(1000);
        TEST_ASSERT_EQUAL_INT(1, d.fakes.system.restarts);
    }
}

void test_device_tab_actions_need_the_password_and_the_header()
{
    for (const char* path : {"/api/restart", "/api/network-reset", "/api/factory-reset"})
    {
        Device d;
        d.fakes.http.localIp = d.fakes.wifi.ip;
        TEST_ASSERT_EQUAL_INT(
            401,
            d.fakes.http.request(HttpMethod::Post, path, {}, {{"X-Requested-With", "EspManager"}})
                .status);
        TEST_ASSERT_EQUAL_INT(403, d.post(path, false).status);
        // Not by GET either.
        TEST_ASSERT_EQUAL_INT(404, d.fakes.http.request(HttpMethod::Get, path).status);
        d.wait(5000);
        TEST_ASSERT_EQUAL_INT(0, d.fakes.system.restarts);
        TEST_ASSERT_EQUAL_UINT32(1, d.esp.networks().count());
        TEST_ASSERT_FALSE(d.fakes.storage.data.empty());
    }
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_network_reset_forgets_the_networks_and_leaves);
    RUN_TEST(test_network_reset_keeps_everything_else);
    RUN_TEST(test_after_a_network_reset_the_portal_opens);
    RUN_TEST(test_network_reset_while_idle_changes_nothing_else);
    RUN_TEST(test_network_reset_during_a_portal_test_fails_the_test);
    RUN_TEST(test_settings_reset_keeps_networks_and_data_until_there_are_settings);
    RUN_TEST(test_factory_reset_erases_everything);
    RUN_TEST(test_factory_reset_restarts_a_moment_later);
    RUN_TEST(test_failed_erase_is_reported_and_still_restarts);
    RUN_TEST(test_restart_waits_for_process_and_leaves_the_network_first);
    RUN_TEST(test_device_tab_actions_run_after_their_answer);
    RUN_TEST(test_device_tab_actions_need_the_password_and_the_header);
    return UNITY_END();
}
