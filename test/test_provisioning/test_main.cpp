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
using espmgr::HttpRequest;
using espmgr::PortalState;
using espmgr::ProvisioningPortal;
using espmgr::WifiConnection;

namespace
{

constexpr uint64_t kCloseDelay = ProvisioningPortal::kCloseDelayMs;

// An EspManager on fakes with the Provisioning Portal open (no Saved Network, no scans), which
// records the events as text.
struct Device
{
    FakePlatform fakes;
    EspManager esp{fakes.platform};
    std::vector<std::string> events;

    Device()
    {
        fakes.wifi.scanStartSucceeds = false;
        esp.onProvisioningCompleted([this](const std::string& ssid)
                                    { events.push_back("provisioned " + ssid); })
            .onPortalStopped([this] { events.push_back("stopped"); })
            .onConnectFailed([this](const std::string& ssid, FailureReason)
                             { events.push_back("failed " + ssid); });
    }

    void open()
    {
        esp.begin();
        esp.process();
        TEST_ASSERT_EQUAL(PortalState::Active, esp.portalState());
    }

    FakeHttpServer::Response submit(HttpRequest::Fields form)
    {
        FakeHttpServer::Response response =
            fakes.http.request(HttpMethod::Post, "/api/connect", std::move(form));
        esp.process();
        return response;
    }

    FakeHttpServer::Response submit(const std::string& ssid, const std::string& password)
    {
        return submit({{"ssid", ssid}, {"password", password}});
    }

    std::string result()
    {
        return fakes.http.request(HttpMethod::Get, "/api/result").body;
    }
};

}  // namespace

void setUp()
{
}

void tearDown()
{
}

// --- Testing a network ---

void test_result_is_idle_before_anything_is_submitted()
{
    Device d;
    d.open();
    TEST_ASSERT_EQUAL_STRING(R"({"state":"idle"})", d.result().c_str());
}

void test_submitted_network_is_tried_without_saving_it()
{
    Device d;
    d.open();
    const FakeHttpServer::Response response = d.submit("Home", "correct horse");
    TEST_ASSERT_EQUAL_INT(200, response.status);
    TEST_ASSERT_EQUAL_STRING(R"({"state":"testing","ssid":"Home"})", response.body.c_str());
    TEST_ASSERT_EQUAL(1, d.fakes.wifi.connectCalls.size());
    TEST_ASSERT_EQUAL_STRING("Home", d.fakes.wifi.connectCalls[0].ssid.c_str());
    TEST_ASSERT_EQUAL_STRING("correct horse", d.fakes.wifi.connectCalls[0].password.c_str());
    TEST_ASSERT_EQUAL(ConnectionState::Connecting, d.esp.state());
    TEST_ASSERT_EQUAL(0, d.esp.networks().count());
    // The access point stays up while the station tries.
    TEST_ASSERT_TRUE(d.fakes.wifi.accessPointActive);
    TEST_ASSERT_EQUAL_STRING(R"({"state":"testing","ssid":"Home"})", d.result().c_str());
}

void test_network_that_connects_is_saved_and_the_portal_closes_later()
{
    Device d;
    d.open();
    d.submit("Home", "correct horse");
    d.fakes.wifi.completeConnect(11);
    d.esp.process();

    TEST_ASSERT_EQUAL(ConnectionState::Connected, d.esp.state());
    TEST_ASSERT_EQUAL_STRING(R"({"state":"connected","ssid":"Home","ip":"192.168.1.50"})",
                             d.result().c_str());
    TEST_ASSERT_EQUAL(1, d.esp.networks().count());
    TEST_ASSERT_EQUAL_STRING("Home", d.esp.networks().list()[0].ssid.c_str());
    TEST_ASSERT_EQUAL_STRING("correct horse", d.esp.networks().list()[0].password.c_str());
    TEST_ASSERT_EQUAL(1, d.events.size());
    TEST_ASSERT_EQUAL_STRING("provisioned Home", d.events[0].c_str());

    // Open a little longer, so the page can fetch the result.
    d.fakes.clock.advance(kCloseDelay - 1);
    d.esp.process();
    TEST_ASSERT_EQUAL(PortalState::Active, d.esp.portalState());
    d.fakes.clock.advance(1);
    d.esp.process();
    TEST_ASSERT_EQUAL(PortalState::Off, d.esp.portalState());
    TEST_ASSERT_FALSE(d.fakes.wifi.accessPointActive);
    TEST_ASSERT_EQUAL_STRING("stopped", d.events.back().c_str());
    // Still connected, to the network that is now saved.
    TEST_ASSERT_EQUAL(ConnectionState::Connected, d.esp.state());
}

void test_saved_network_survives_a_restart()
{
    Device d;
    d.open();
    d.submit("Home", "correct horse");
    d.fakes.wifi.completeConnect();
    d.esp.process();

    EspManager restarted(d.fakes.platform);
    restarted.begin();
    TEST_ASSERT_EQUAL(1, restarted.networks().count());
    TEST_ASSERT_EQUAL_STRING("Home", restarted.networks().list()[0].ssid.c_str());
}

void test_failed_network_is_not_saved_and_the_reason_is_shown()
{
    Device d;
    d.open();
    d.submit("Home", "wrong password");
    d.fakes.wifi.failConnect(FailureReason::WrongPassword);
    d.esp.process();

    TEST_ASSERT_EQUAL_STRING(R"({"state":"failed","ssid":"Home","reason":"wrong-password"})",
                             d.result().c_str());
    TEST_ASSERT_EQUAL(0, d.esp.networks().count());
    TEST_ASSERT_EQUAL(PortalState::Active, d.esp.portalState());
    TEST_ASSERT_EQUAL(ConnectionState::Idle, d.esp.state());
    // Not a Saved Network, so no connectFailed, and no provisioning.
    TEST_ASSERT_TRUE(d.events.empty());
}

void test_each_failure_reason_has_its_code()
{
    const std::pair<FailureReason, const char*> reasons[] = {
        {FailureReason::NetworkNotFound, "network-not-found"},
        {FailureReason::WrongPassword, "wrong-password"},
        {FailureReason::ConnectionLost, "connection-lost"},
        {FailureReason::Other, "other"},
        {FailureReason::None, "other"},
    };
    for (const auto& [reason, code] : reasons)
    {
        Device d;
        d.open();
        d.submit("Home", "correct horse");
        d.fakes.wifi.failConnect(reason);
        d.esp.process();
        const std::string expected =
            std::string(R"({"state":"failed","ssid":"Home","reason":")") + code + "\"}";
        TEST_ASSERT_EQUAL_STRING(expected.c_str(), d.result().c_str());
    }
}

void test_attempt_that_never_finishes_fails_as_timeout()
{
    Device d;
    d.open();
    d.submit("Home", "correct horse");
    d.fakes.clock.advance(WifiConnection::kAttemptTimeoutMs);
    d.esp.process();
    TEST_ASSERT_EQUAL_STRING(R"({"state":"failed","ssid":"Home","reason":"timeout"})",
                             d.result().c_str());
}

void test_owner_can_try_again_after_a_failure()
{
    Device d;
    d.open();
    d.submit("Home", "wrong password");
    d.fakes.wifi.failConnect(FailureReason::WrongPassword);
    d.esp.process();

    TEST_ASSERT_EQUAL_INT(200, d.submit("Home", "correct horse").status);
    TEST_ASSERT_EQUAL(2, d.fakes.wifi.connectCalls.size());
    d.fakes.wifi.completeConnect();
    d.esp.process();
    TEST_ASSERT_EQUAL_STRING("correct horse", d.esp.networks().list()[0].password.c_str());
}

void test_second_network_while_testing_is_refused()
{
    Device d;
    d.open();
    d.submit("Home", "correct horse");
    const FakeHttpServer::Response response = d.submit("Other", "correct horse");
    TEST_ASSERT_EQUAL_INT(409, response.status);
    TEST_ASSERT_EQUAL_STRING(R"({"error":"busy"})", response.body.c_str());
    TEST_ASSERT_EQUAL(1, d.fakes.wifi.connectCalls.size());
}

void test_open_network_needs_no_password()
{
    Device d;
    d.open();
    TEST_ASSERT_EQUAL_INT(200, d.submit({{"ssid", "Cafe"}}).status);
    TEST_ASSERT_EQUAL_STRING("", d.fakes.wifi.connectCalls[0].password.c_str());
}

void test_test_does_not_time_the_portal_out()
{
    Device d;
    d.esp.portal().setTimeout(5000);
    d.open();
    d.submit("Home", "correct horse");
    d.fakes.clock.advance(10000);
    d.esp.process();
    TEST_ASSERT_EQUAL(PortalState::Active, d.esp.portalState());
}

// --- Invalid input ---

void test_invalid_networks_are_refused_with_a_code()
{
    const std::pair<HttpRequest::Fields, const char*> cases[] = {
        {{{"password", "correct horse"}}, "ssid-empty"},
        {{{"ssid", ""}, {"password", "correct horse"}}, "ssid-empty"},
        {{{"ssid", std::string(33, 's')}}, "ssid-too-long"},
        {{{"ssid", "Home"}, {"password", "1234567"}}, "password-too-short"},
        {{{"ssid", "Home"}, {"password", std::string(65, 'p')}}, "password-too-long"},
        {{{"ssid", "Home"}, {"password", std::string(64, 'p')}}, "password-invalid"},
        {{{"ssid", "Home"}, {"password", "tab\tinside"}}, "password-invalid"},
    };
    for (const auto& [form, code] : cases)
    {
        Device d;
        d.open();
        const FakeHttpServer::Response response = d.submit(form);
        TEST_ASSERT_EQUAL_INT_MESSAGE(400, response.status, code);
        const std::string expected = std::string(R"({"error":")") + code + "\"}";
        TEST_ASSERT_EQUAL_STRING(expected.c_str(), response.body.c_str());
        TEST_ASSERT_EQUAL(0, d.fakes.wifi.connectCalls.size());
        TEST_ASSERT_EQUAL_STRING(R"({"state":"idle"})", d.result().c_str());
    }
}

void test_submitting_from_the_stations_network_is_refused()
{
    Device d;
    d.open();
    d.fakes.http.localIp = d.fakes.wifi.ip;
    TEST_ASSERT_EQUAL_INT(403, d.submit("Home", "correct horse").status);
    TEST_ASSERT_EQUAL(0, d.fakes.wifi.connectCalls.size());
}

void test_password_is_never_logged()
{
    Device d;
    d.esp.setLogLevel(espmgr::LogLevel::Verbose);
    d.open();
    d.submit("Home", "s3cr3t!pw");
    d.fakes.wifi.failConnect(FailureReason::WrongPassword);
    d.esp.process();
    d.submit("Home", "s3cr3t!");
    d.submit({{"ssid", "Home"}, {"password", "s3cr3t!pw"}, {"saveAnyway", "1"}});
    d.fakes.clock.advance(kCloseDelay);
    d.esp.process();
    TEST_ASSERT_FALSE(d.fakes.log.lines.empty());
    for (const auto& line : d.fakes.log.lines)
    {
        TEST_ASSERT_TRUE_MESSAGE(line.text.find("s3cr3t") == std::string::npos, line.text.c_str());
    }
}

// --- Save anyway ---

void test_save_anyway_saves_without_testing()
{
    Device d;
    d.open();
    d.submit("Home", "wrong password");
    d.fakes.wifi.failConnect(FailureReason::NetworkNotFound);
    d.esp.process();

    const FakeHttpServer::Response response =
        d.submit({{"ssid", "Home"}, {"password", "correct horse"}, {"saveAnyway", "1"}});
    TEST_ASSERT_EQUAL_INT(200, response.status);
    TEST_ASSERT_EQUAL_STRING(R"({"state":"saved","ssid":"Home"})", response.body.c_str());
    TEST_ASSERT_EQUAL(1, d.esp.networks().count());
    TEST_ASSERT_EQUAL_STRING("correct horse", d.esp.networks().list()[0].password.c_str());
    TEST_ASSERT_EQUAL_STRING("provisioned Home", d.events[0].c_str());

    d.fakes.clock.advance(kCloseDelay);
    d.esp.process();
    TEST_ASSERT_EQUAL(PortalState::Off, d.esp.portalState());
    // The device keeps trying the saved network as usual.
    TEST_ASSERT_TRUE(d.esp.state() == ConnectionState::Connecting ||
                     d.esp.state() == ConnectionState::Waiting);
    TEST_ASSERT_EQUAL_STRING("Home", d.fakes.wifi.connectCalls.back().ssid.c_str());
}

void test_nothing_more_is_accepted_after_provisioning()
{
    Device d;
    d.open();
    d.submit({{"ssid", "Home"}, {"saveAnyway", "1"}});
    const FakeHttpServer::Response response = d.submit("Other", "correct horse");
    TEST_ASSERT_EQUAL_INT(409, response.status);
    TEST_ASSERT_EQUAL_STRING("Home", d.esp.networks().list()[0].ssid.c_str());
}

// --- With a Saved Network already ---

void test_trial_replaces_the_connection_to_a_saved_network()
{
    Device d;
    TEST_ASSERT_TRUE(d.esp.networks().save("Old", "correct horse"));
    d.esp.begin();
    d.esp.process();
    d.fakes.wifi.completeConnect();
    d.esp.process();
    d.esp.startPortal();
    d.esp.process();
    TEST_ASSERT_EQUAL(PortalState::Active, d.esp.portalState());

    d.submit("New", "correct horse");
    TEST_ASSERT_EQUAL(ConnectionState::Connecting, d.esp.state());
    TEST_ASSERT_EQUAL_STRING("New", d.fakes.wifi.connectCalls.back().ssid.c_str());
    d.fakes.wifi.completeConnect();
    d.esp.process();
    TEST_ASSERT_EQUAL(1, d.esp.networks().count());
    TEST_ASSERT_EQUAL_STRING("New", d.esp.networks().list()[0].ssid.c_str());
}

void test_failed_trial_goes_back_to_the_saved_network()
{
    Device d;
    TEST_ASSERT_TRUE(d.esp.networks().save("Old", "correct horse"));
    d.esp.begin();
    d.esp.process();
    d.fakes.wifi.completeConnect();
    d.esp.process();
    d.esp.startPortal();
    d.esp.process();

    d.submit("New", "wrong password");
    d.fakes.wifi.failConnect(FailureReason::WrongPassword);
    d.esp.process();
    TEST_ASSERT_EQUAL(ConnectionState::Waiting, d.esp.state());
    TEST_ASSERT_EQUAL_STRING("Old", d.esp.networks().list()[0].ssid.c_str());

    d.fakes.clock.advance(WifiConnection::kDefaultMinRetryDelayMs);
    d.esp.process();
    TEST_ASSERT_EQUAL(ConnectionState::Connecting, d.esp.state());
    TEST_ASSERT_EQUAL_STRING("Old", d.fakes.wifi.connectCalls.back().ssid.c_str());
}

void test_portal_opened_after_failing_waits_for_the_page_before_closing()
{
    Device d;
    TEST_ASSERT_TRUE(d.esp.networks().save("Old", "correct horse"));
    d.esp.portal().setOpenAfterFailingFor(60000);
    d.esp.begin();
    d.esp.process();
    d.fakes.wifi.failConnect(FailureReason::NetworkNotFound);
    d.esp.process();
    d.fakes.clock.advance(60000);
    d.esp.process();
    TEST_ASSERT_EQUAL(PortalState::Active, d.esp.portalState());

    d.submit("New", "correct horse");
    d.fakes.wifi.completeConnect();
    d.esp.process();
    TEST_ASSERT_EQUAL(ConnectionState::Connected, d.esp.state());
    TEST_ASSERT_EQUAL(PortalState::Active, d.esp.portalState());
    TEST_ASSERT_EQUAL_STRING(R"({"state":"connected","ssid":"New","ip":"192.168.1.50"})",
                             d.result().c_str());
    d.fakes.clock.advance(kCloseDelay);
    d.esp.process();
    TEST_ASSERT_EQUAL(PortalState::Off, d.esp.portalState());
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_result_is_idle_before_anything_is_submitted);
    RUN_TEST(test_submitted_network_is_tried_without_saving_it);
    RUN_TEST(test_network_that_connects_is_saved_and_the_portal_closes_later);
    RUN_TEST(test_saved_network_survives_a_restart);
    RUN_TEST(test_failed_network_is_not_saved_and_the_reason_is_shown);
    RUN_TEST(test_each_failure_reason_has_its_code);
    RUN_TEST(test_attempt_that_never_finishes_fails_as_timeout);
    RUN_TEST(test_owner_can_try_again_after_a_failure);
    RUN_TEST(test_second_network_while_testing_is_refused);
    RUN_TEST(test_open_network_needs_no_password);
    RUN_TEST(test_test_does_not_time_the_portal_out);
    RUN_TEST(test_invalid_networks_are_refused_with_a_code);
    RUN_TEST(test_submitting_from_the_stations_network_is_refused);
    RUN_TEST(test_password_is_never_logged);
    RUN_TEST(test_save_anyway_saves_without_testing);
    RUN_TEST(test_nothing_more_is_accepted_after_provisioning);
    RUN_TEST(test_trial_replaces_the_connection_to_a_saved_network);
    RUN_TEST(test_failed_trial_goes_back_to_the_saved_network);
    RUN_TEST(test_portal_opened_after_failing_waits_for_the_page_before_closing);
    return UNITY_END();
}
