// Passwords never leave the device and never reach the log (US08.2.1, US08.2.3): every answer
// of the Provisioning Portal and the Management Interface, and every log line, is searched for
// each password the device knows.

#include <DigestClient.h>
#include <EspManager.h>
#include <FakePlatform.h>
#include <unity.h>
#include <web/Pages.h>

#include <string>
#include <vector>

using espmgr::DigestClient;
using espmgr::FakeHttpServer;
using espmgr::FakePlatform;
using espmgr::HttpMethod;
using espmgr::HttpRequest;
using espmgr::LogLevel;
using espmgr::PortalState;

namespace
{

const char* const kWifiPassword = "wifi secret 123";
const char* const kManagementPassword = "management secret";
const char* const kPortalPassword = "portal secret";

const char* const kSecrets[] = {kWifiPassword, kManagementPassword, kPortalPassword};

// An EspManager with every password set, provisioned through the portal at full logging.
struct Device
{
    FakePlatform fakes;
    EspManager esp{fakes.platform};
    std::vector<std::string> answers;
    uint32_t logins = 0;

    Device()
    {
        fakes.wifi.scanStartSucceeds = false;
        esp.setLogLevel(LogLevel::Verbose);
        esp.management().enable();
        TEST_ASSERT_TRUE(esp.portal().setPassword(kPortalPassword));
        esp.begin();
        esp.process();
        TEST_ASSERT_EQUAL(PortalState::Active, esp.portalState());
    }

    // Sends a request on the access point's address and keeps the answer.
    FakeHttpServer::Response portal(HttpMethod method, const std::string& path,
                                    HttpRequest::Fields args = {})
    {
        fakes.http.localIp = fakes.wifi.apIp;
        return keep(fakes.http.request(method, path, std::move(args)));
    }

    // Logs in on the station's address, as the Owner's browser would, and keeps both answers.
    FakeHttpServer::Response management(const std::string& path)
    {
        fakes.http.localIp = fakes.wifi.ip;
        // A new nonce for each login; the fake clock's random numbers are 0 unless queued.
        ++logins;
        fakes.clock.randomValues = {logins, logins, logins, logins};
        const FakeHttpServer::Response challenge = keep(fakes.http.request(HttpMethod::Get, path));
        DigestClient client;
        client.realm = esp.management().realm();
        client.password = kManagementPassword;
        const std::string nonce = DigestClient::nonceFrom(challenge.header("WWW-Authenticate"));
        return keep(fakes.http.request(HttpMethod::Get, path, {},
                                       {{"Authorization", client.header(nonce, "GET", path)}}));
    }

    FakeHttpServer::Response keep(FakeHttpServer::Response response)
    {
        std::string all = response.body + response.location;
        for (const auto& [name, value] : response.headers)
        {
            all += name + ": " + value + "\n";
        }
        answers.push_back(all);
        return response;
    }

    void submit(const std::string& wifiPassword, bool saveAnyway = false)
    {
        HttpRequest::Fields form = {{"ssid", "Home"},
                                    {"password", wifiPassword},
                                    {"managementPassword", kManagementPassword}};
        if (saveAnyway)
        {
            form.emplace_back("saveAnyway", "1");
        }
        portal(HttpMethod::Post, "/api/connect", form);
        esp.process();
    }

    // Every portal page and API call.
    void walkPortal()
    {
        portal(HttpMethod::Get, "/");
        portal(HttpMethod::Get, "/api/scan");
        portal(HttpMethod::Get, "/api/setup");
        portal(HttpMethod::Get, "/api/status");
        portal(HttpMethod::Get, "/api/result");
        portal(HttpMethod::Get, "/generate_204");
    }

    void walkManagement()
    {
        TEST_ASSERT_EQUAL_INT(200, management("/").status);
        TEST_ASSERT_EQUAL_INT(200, management("/api/status").status);
        TEST_ASSERT_EQUAL_INT(200, management("/api/networks").status);
    }

    void assertNoSecretAnywhere()
    {
        for (const char* secret : kSecrets)
        {
            for (const std::string& answer : answers)
            {
                TEST_ASSERT_TRUE_MESSAGE(answer.find(secret) == std::string::npos, secret);
            }
            for (const auto& line : fakes.log.lines)
            {
                TEST_ASSERT_TRUE_MESSAGE(line.text.find(secret) == std::string::npos,
                                         line.text.c_str());
            }
            // Only the Digest hash of the management password is stored.
            for (const char* key : {"ha1", "id"})
            {
                const std::string stored = fakes.storage.getString("espmgr-mgmt", key).value_or("");
                TEST_ASSERT_TRUE(stored.find(kManagementPassword) == std::string::npos);
            }
        }
        TEST_ASSERT_FALSE(answers.empty());
        TEST_ASSERT_FALSE(fakes.log.lines.empty());
    }
};

}  // namespace

void setUp()
{
}

void tearDown()
{
}

void test_no_secret_leaves_the_device_during_a_successful_setup()
{
    Device d;
    d.walkPortal();
    d.submit(kWifiPassword);
    d.walkPortal();
    d.fakes.wifi.completeConnect();
    d.esp.process();
    d.walkPortal();
    d.walkManagement();
    d.assertNoSecretAnywhere();
}

void test_no_secret_leaves_the_device_after_a_failed_test()
{
    Device d;
    d.submit(kWifiPassword);
    d.fakes.wifi.failConnect(espmgr::FailureReason::WrongPassword);
    d.esp.process();
    d.walkPortal();
    d.assertNoSecretAnywhere();
}

void test_no_secret_leaves_the_device_with_save_anyway()
{
    Device d;
    d.submit(kWifiPassword, true);
    d.walkPortal();
    d.fakes.wifi.completeConnect();
    d.esp.process();
    d.walkManagement();
    d.assertNoSecretAnywhere();
}

// A refused value is never echoed back or logged either.
void test_refused_passwords_are_not_echoed()
{
    Device d;
    const std::string tooLong(70, 'x');
    d.portal(HttpMethod::Post, "/api/connect", {{"ssid", "Home"}, {"password", tooLong}});
    d.portal(HttpMethod::Post, "/api/connect",
             {{"ssid", "Home"}, {"password", kWifiPassword}, {"managementPassword", "short"}});
    for (const std::string& answer : d.answers)
    {
        TEST_ASSERT_TRUE(answer.find(tooLong) == std::string::npos);
        TEST_ASSERT_TRUE(answer.find("short\"") == std::string::npos);
    }
    d.assertNoSecretAnywhere();
}

// The pages are fixed bytes from flash: a stored password can't end up in them.
void test_pages_are_the_same_bytes_with_passwords_stored()
{
    Device d;
    d.submit(kWifiPassword, true);
    TEST_ASSERT_EQUAL_size_t(espmgr::web::kPortalPageSize,
                             d.portal(HttpMethod::Get, "/").body.size());
    TEST_ASSERT_TRUE(d.answers.back() ==
                     std::string(reinterpret_cast<const char*>(espmgr::web::kPortalPage),
                                 espmgr::web::kPortalPageSize));
    d.fakes.wifi.completeConnect();
    d.esp.process();
    const FakeHttpServer::Response page = d.management("/");
    TEST_ASSERT_TRUE(page.body ==
                     std::string(reinterpret_cast<const char*>(espmgr::web::kManagementPage),
                                 espmgr::web::kManagementPageSize));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_no_secret_leaves_the_device_during_a_successful_setup);
    RUN_TEST(test_no_secret_leaves_the_device_after_a_failed_test);
    RUN_TEST(test_no_secret_leaves_the_device_with_save_anyway);
    RUN_TEST(test_refused_passwords_are_not_echoed);
    RUN_TEST(test_pages_are_the_same_bytes_with_passwords_stored);
    return UNITY_END();
}
