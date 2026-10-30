#include <DigestClient.h>
#include <EspManager.h>
#include <FakePlatform.h>
#include <unity.h>

#include <string>

using espmgr::DigestAuth;
using espmgr::DigestClient;
using espmgr::FakeHttpServer;
using espmgr::FakePlatform;
using espmgr::HttpMethod;
using espmgr::HttpRequest;
using espmgr::ManagementInterface;
using espmgr::PortalState;

namespace
{

const char* const kPassword = "owner password";

// An EspManager on fakes with the Management Interface enabled and a Saved Network.
struct Device
{
    explicit Device(FakePlatform& platform, bool enable = true) : fakes(platform)
    {
        fakes.wifi.scanStartSucceeds = false;
        if (enable)
        {
            esp.management().enable();
        }
    }

    FakePlatform& fakes;
    EspManager esp{fakes.platform};
    DigestClient client = owner();

    static DigestClient owner()
    {
        DigestClient client;
        client.realm = "esp-abcdef";
        client.password = kPassword;
        return client;
    }

    // begin() and a connection to the Saved Network.
    void connect()
    {
        if (esp.networks().count() == 0)
        {
            TEST_ASSERT_TRUE(esp.networks().save("Home", "correct horse"));
        }
        esp.begin();
        esp.process();
        fakes.wifi.completeConnect();
        esp.process();
    }

    // A request on the station's address, as from the Owner's own network.
    FakeHttpServer::Response get(const std::string& path, HttpRequest::Fields headers = {})
    {
        fakes.http.localIp = fakes.wifi.ip;
        return fakes.http.request(HttpMethod::Get, path, {}, std::move(headers));
    }

    // What a browser does: ask, take the challenge, and ask again with the password.
    FakeHttpServer::Response login(const std::string& path)
    {
        const FakeHttpServer::Response challenge = get(path);
        TEST_ASSERT_EQUAL_INT(401, challenge.status);
        const std::string nonce = DigestClient::nonceFrom(challenge.header("WWW-Authenticate"));
        return get(path, {{"Authorization", client.header(nonce, "GET", path)}});
    }
};

}  // namespace

void setUp()
{
}

void tearDown()
{
}

// --- When it answers ---

void test_off_unless_enabled()
{
    FakePlatform fakes;
    Device d(fakes, false);
    TEST_ASSERT_FALSE(d.esp.management().setPassword(kPassword) && d.esp.management().enabled());
    d.connect();
    TEST_ASSERT_FALSE(d.esp.management().active());
    TEST_ASSERT_FALSE(fakes.http.running);
}

void test_enabled_without_a_password_stays_off_and_warns()
{
    FakePlatform fakes;
    Device d(fakes);
    d.esp.setLogLevel(espmgr::LogLevel::Warn);
    d.connect();
    TEST_ASSERT_FALSE(d.esp.management().hasPassword());
    TEST_ASSERT_FALSE(d.esp.management().active());
    TEST_ASSERT_FALSE(fakes.http.running);
    TEST_ASSERT_TRUE(fakes.log.contains("no password yet"));
}

void test_answers_once_connected_with_a_password()
{
    FakePlatform fakes;
    Device d(fakes);
    TEST_ASSERT_TRUE(d.esp.management().setPassword(kPassword));
    TEST_ASSERT_TRUE(d.esp.networks().save("Home", "correct horse"));
    d.esp.begin();
    d.esp.process();
    TEST_ASSERT_FALSE(d.esp.management().active());

    d.fakes.wifi.completeConnect();
    d.esp.process();
    TEST_ASSERT_TRUE(d.esp.management().active());
    TEST_ASSERT_TRUE(fakes.http.running);
    TEST_ASSERT_EQUAL_UINT16(80, fakes.http.port);
}

void test_stops_answering_when_the_connection_drops()
{
    FakePlatform fakes;
    Device d(fakes);
    d.esp.management().setPassword(kPassword);
    d.connect();
    d.fakes.wifi.loseConnection();
    d.esp.process();
    TEST_ASSERT_FALSE(d.esp.management().active());
    TEST_ASSERT_FALSE(fakes.http.running);
}

// --- Logging in ---

void test_asks_for_the_password_with_a_digest_challenge()
{
    FakePlatform fakes;
    Device d(fakes);
    d.esp.management().setPassword(kPassword);
    d.connect();
    for (const char* path : {"/", "/api/status", "/api/networks"})
    {
        const FakeHttpServer::Response response = d.get(path);
        TEST_ASSERT_EQUAL_INT(401, response.status);
        TEST_ASSERT_EQUAL_STRING_LEN("Digest realm=\"esp-abcdef\", qop=\"auth\"",
                                     response.header("WWW-Authenticate").c_str(), 36);
    }
}

void test_right_password_gets_the_page_gzipped()
{
    FakePlatform fakes;
    Device d(fakes);
    d.esp.management().setPassword(kPassword);
    d.connect();
    const FakeHttpServer::Response response = d.login("/");
    TEST_ASSERT_EQUAL_INT(200, response.status);
    TEST_ASSERT_TRUE(response.gzipped);
    TEST_ASSERT_EQUAL_STRING("text/html; charset=utf-8", response.contentType.c_str());
}

void test_right_password_gets_the_status()
{
    FakePlatform fakes;
    Device d(fakes);
    d.esp.management().setPassword(kPassword);
    d.connect();
    const FakeHttpServer::Response response = d.login("/api/status");
    TEST_ASSERT_EQUAL_INT(200, response.status);
    TEST_ASSERT_TRUE(response.body.find("\"connection\":\"Connected\"") != std::string::npos);
    TEST_ASSERT_TRUE(response.body.find("\"ssid\":\"Home\"") != std::string::npos);
}

void test_wrong_password_is_refused_and_logged()
{
    FakePlatform fakes;
    Device d(fakes);
    d.esp.setLogLevel(espmgr::LogLevel::Info);
    d.esp.management().setPassword(kPassword);
    d.connect();
    d.client.password = "guessed password";
    TEST_ASSERT_EQUAL_INT(401, d.login("/api/status").status);
    TEST_ASSERT_TRUE(fakes.log.contains("refused a login"));
    TEST_ASSERT_FALSE(fakes.log.contains("guessed password"));
}

// The first request of every login has no credentials yet; that's no refused login.
void test_a_request_without_credentials_is_not_logged_as_refused()
{
    FakePlatform fakes;
    Device d(fakes);
    d.esp.setLogLevel(espmgr::LogLevel::Info);
    d.esp.management().setPassword(kPassword);
    d.connect();
    TEST_ASSERT_EQUAL_INT(401, d.get("/api/status").status);
    TEST_ASSERT_EQUAL_INT(401, d.get("/api/status", {{"Authorization", ""}}).status);
    TEST_ASSERT_FALSE(fakes.log.contains("refused a login"));
}

void test_saved_networks_are_listed_without_passwords()
{
    FakePlatform fakes;
    Device d(fakes);
    d.esp.management().setPassword(kPassword);
    d.connect();
    const FakeHttpServer::Response response = d.login("/api/networks");
    TEST_ASSERT_EQUAL_INT(200, response.status);
    TEST_ASSERT_EQUAL_STRING(
        "{\"connected\":\"Home\",\"max\":5,\"networks\":[{\"ssid\":\"Home\",\"priority\":0}]}",
        response.body.c_str());
    TEST_ASSERT_TRUE(response.body.find("correct horse") == std::string::npos);
}

void test_unknown_path_is_not_found()
{
    FakePlatform fakes;
    Device d(fakes);
    d.esp.management().setPassword(kPassword);
    d.connect();
    TEST_ASSERT_EQUAL_INT(404, d.get("/nowhere").status);
}

void test_user_and_realm_can_be_changed()
{
    FakePlatform fakes;
    Device d(fakes);
    TEST_ASSERT_TRUE(d.esp.management().setUser("owner"));
    TEST_ASSERT_TRUE(d.esp.management().setRealm("Kitchen sensor"));
    d.esp.management().setPassword(kPassword);
    d.connect();
    d.client.user = "owner";
    d.client.realm = "Kitchen sensor";
    TEST_ASSERT_EQUAL_INT(200, d.login("/api/status").status);
}

void test_invalid_user_realm_and_password_are_refused()
{
    FakePlatform fakes;
    Device d(fakes);
    ManagementInterface& management = d.esp.management();
    TEST_ASSERT_FALSE(management.setUser(""));
    TEST_ASSERT_FALSE(management.setUser("a:b"));
    TEST_ASSERT_FALSE(management.setUser("say \"hi\""));
    TEST_ASSERT_FALSE(management.setRealm("back\\slash"));
    TEST_ASSERT_FALSE(management.setPassword("short"));
    TEST_ASSERT_FALSE(management.setPassword(std::string(64, 'x')));
    TEST_ASSERT_EQUAL_STRING("admin", management.user().c_str());
    TEST_ASSERT_EQUAL_STRING("esp-abcdef", management.realm().c_str());
    TEST_ASSERT_FALSE(management.hasPassword());
}

// --- Storage ---

void test_only_the_digest_hash_is_stored()
{
    FakePlatform fakes;
    Device d(fakes);
    d.esp.management().setPassword(kPassword);
    TEST_ASSERT_EQUAL_STRING(DigestAuth::ha1("admin", "esp-abcdef", kPassword).c_str(),
                             fakes.storage.getString("espmgr-mgmt", "ha1").value().c_str());
    TEST_ASSERT_EQUAL_STRING("admin:esp-abcdef",
                             fakes.storage.getString("espmgr-mgmt", "id").value().c_str());
}

void test_password_is_kept_across_a_restart()
{
    FakePlatform fakes;
    {
        Device first(fakes);
        first.esp.management().setPassword(kPassword);
    }
    Device d(fakes);
    d.connect();
    TEST_ASSERT_TRUE(d.esp.management().hasPassword());
    TEST_ASSERT_EQUAL_INT(200, d.login("/api/status").status);
}

void test_a_new_user_makes_the_stored_password_useless()
{
    FakePlatform fakes;
    {
        Device first(fakes);
        first.esp.management().setPassword(kPassword);
    }
    Device d(fakes);
    d.esp.setLogLevel(espmgr::LogLevel::Warn);
    d.esp.management().setUser("owner");
    d.connect();
    TEST_ASSERT_FALSE(d.esp.management().hasPassword());
    TEST_ASSERT_FALSE(d.esp.management().active());
    TEST_ASSERT_TRUE(fakes.log.contains("another user or realm"));
}

// --- Next to the Provisioning Portal ---

void test_each_address_gets_its_own_site()
{
    FakePlatform fakes;
    Device d(fakes);
    d.esp.management().setPassword(kPassword);
    d.connect();
    d.esp.startPortal();
    d.esp.process();
    TEST_ASSERT_EQUAL(PortalState::Active, d.esp.portalState());

    // The station's address: the Management Interface, behind its password.
    TEST_ASSERT_EQUAL_INT(401, d.get("/api/status").status);
    // The access point's: the Provisioning Portal, open to whoever joined it.
    fakes.http.localIp = fakes.wifi.apIp;
    TEST_ASSERT_EQUAL_INT(200, fakes.http.request(HttpMethod::Get, "/api/status").status);
    TEST_ASSERT_EQUAL_INT(200, fakes.http.request(HttpMethod::Get, "/api/scan").status);
    // The portal's own paths don't exist on the station's address.
    TEST_ASSERT_EQUAL_INT(404, d.get("/api/scan").status);

    // Closing the portal leaves the server running for the Management Interface.
    d.esp.stopPortal();
    d.esp.process();
    TEST_ASSERT_TRUE(fakes.http.running);
    TEST_ASSERT_EQUAL_INT(200, d.login("/api/status").status);
}

// --- Choosing the password in the Provisioning Portal ---

namespace
{

// A Device with the Provisioning Portal open, as without a Saved Network.
struct Portal : Device
{
    explicit Portal(FakePlatform& platform, bool enable = true) : Device(platform, enable)
    {
        esp.begin();
        esp.process();
        TEST_ASSERT_EQUAL(PortalState::Active, esp.portalState());
    }

    FakeHttpServer::Response setup()
    {
        fakes.http.localIp = fakes.wifi.apIp;
        return fakes.http.request(HttpMethod::Get, "/api/setup");
    }

    FakeHttpServer::Response submit(HttpRequest::Fields form)
    {
        fakes.http.localIp = fakes.wifi.apIp;
        FakeHttpServer::Response response =
            fakes.http.request(HttpMethod::Post, "/api/connect", std::move(form));
        esp.process();
        return response;
    }
};

}  // namespace

void test_setup_tells_the_page_about_the_password_step()
{
    {
        FakePlatform fakes;
        Portal p(fakes, false);
        TEST_ASSERT_EQUAL_STRING("{\"management\":\"off\",\"user\":\"\"}", p.setup().body.c_str());
    }
    FakePlatform fakes;
    Portal p(fakes);
    TEST_ASSERT_EQUAL_STRING("{\"management\":\"new\",\"user\":\"admin\"}", p.setup().body.c_str());
    p.esp.management().setPassword(kPassword);
    TEST_ASSERT_EQUAL_STRING("{\"management\":\"keep\",\"user\":\"admin\"}",
                             p.setup().body.c_str());
}

void test_a_network_without_a_valid_management_password_is_refused()
{
    FakePlatform fakes;
    Portal p(fakes);
    for (const char* password : {"", "short", "with\ttab"})
    {
        const FakeHttpServer::Response response = p.submit(
            {{"ssid", "Home"}, {"password", "correct horse"}, {"managementPassword", password}});
        TEST_ASSERT_EQUAL_INT(400, response.status);
        TEST_ASSERT_EQUAL_STRING("{\"error\":\"management-password-invalid\"}",
                                 response.body.c_str());
    }
    TEST_ASSERT_EQUAL_UINT32(0, p.esp.networks().count());
}

void test_the_password_is_stored_once_the_network_works()
{
    FakePlatform fakes;
    Portal p(fakes);
    TEST_ASSERT_EQUAL_INT(200, p.submit({{"ssid", "Home"},
                                         {"password", "correct horse"},
                                         {"managementPassword", kPassword}})
                                   .status);
    TEST_ASSERT_FALSE(p.esp.management().hasPassword());

    fakes.wifi.completeConnect();
    p.esp.process();
    TEST_ASSERT_TRUE(p.esp.management().hasPassword());
    TEST_ASSERT_TRUE(p.esp.management().active());
    TEST_ASSERT_EQUAL_INT(200, p.login("/api/status").status);
}

void test_a_failed_test_stores_no_password()
{
    FakePlatform fakes;
    Portal p(fakes);
    p.submit({{"ssid", "Home"}, {"password", "wrong password"}, {"managementPassword", kPassword}});
    fakes.wifi.failConnect(espmgr::FailureReason::WrongPassword);
    p.esp.process();
    TEST_ASSERT_FALSE(p.esp.management().hasPassword());
    TEST_ASSERT_FALSE(fakes.storage.getString("espmgr-mgmt", "ha1").has_value());
}

void test_save_anyway_stores_the_password_too()
{
    FakePlatform fakes;
    Portal p(fakes);
    p.submit({{"ssid", "Home"},
              {"password", "correct horse"},
              {"managementPassword", kPassword},
              {"saveAnyway", "1"}});
    TEST_ASSERT_TRUE(p.esp.management().hasPassword());
}

void test_an_empty_password_keeps_the_stored_one()
{
    FakePlatform fakes;
    Portal p(fakes);
    p.esp.management().setPassword(kPassword);
    TEST_ASSERT_EQUAL_INT(
        200, p.submit({{"ssid", "Home"}, {"password", "correct horse"}, {"managementPassword", ""}})
                 .status);
    fakes.wifi.completeConnect();
    p.esp.process();
    TEST_ASSERT_EQUAL_INT(200, p.login("/api/status").status);
}

void test_without_management_interface_the_field_is_ignored()
{
    FakePlatform fakes;
    Portal p(fakes, false);
    p.submit({{"ssid", "Home"},
              {"password", "correct horse"},
              {"managementPassword", kPassword},
              {"saveAnyway", "1"}});
    TEST_ASSERT_EQUAL_UINT32(1, p.esp.networks().count());
    TEST_ASSERT_FALSE(fakes.storage.getString("espmgr-mgmt", "ha1").has_value());
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_off_unless_enabled);
    RUN_TEST(test_enabled_without_a_password_stays_off_and_warns);
    RUN_TEST(test_answers_once_connected_with_a_password);
    RUN_TEST(test_stops_answering_when_the_connection_drops);
    RUN_TEST(test_asks_for_the_password_with_a_digest_challenge);
    RUN_TEST(test_right_password_gets_the_page_gzipped);
    RUN_TEST(test_right_password_gets_the_status);
    RUN_TEST(test_wrong_password_is_refused_and_logged);
    RUN_TEST(test_a_request_without_credentials_is_not_logged_as_refused);
    RUN_TEST(test_saved_networks_are_listed_without_passwords);
    RUN_TEST(test_unknown_path_is_not_found);
    RUN_TEST(test_user_and_realm_can_be_changed);
    RUN_TEST(test_invalid_user_realm_and_password_are_refused);
    RUN_TEST(test_only_the_digest_hash_is_stored);
    RUN_TEST(test_password_is_kept_across_a_restart);
    RUN_TEST(test_a_new_user_makes_the_stored_password_useless);
    RUN_TEST(test_each_address_gets_its_own_site);
    RUN_TEST(test_setup_tells_the_page_about_the_password_step);
    RUN_TEST(test_a_network_without_a_valid_management_password_is_refused);
    RUN_TEST(test_the_password_is_stored_once_the_network_works);
    RUN_TEST(test_a_failed_test_stores_no_password);
    RUN_TEST(test_save_anyway_stores_the_password_too);
    RUN_TEST(test_an_empty_password_keeps_the_stored_one);
    RUN_TEST(test_without_management_interface_the_field_is_ignored);
    return UNITY_END();
}
