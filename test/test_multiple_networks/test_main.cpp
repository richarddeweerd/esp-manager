// Several Saved Networks in the Owner's order (US05.2.1-US05.2.5): the limit, where a new
// network goes, removing and moving, which one the device connects to, and the list as both web
// sites show and change it.

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
using espmgr::PortalState;
using espmgr::SavedNetwork;
using espmgr::SavedNetworks;
using espmgr::ScanResult;

namespace
{

const char* const kManagementPassword = "owner password";
const HttpRequest::Fields kFromPage = {{"X-Requested-With", "EspManager"}};

ScanResult seen(const std::string& ssid, int8_t rssi = -60)
{
    return {ssid, rssi, 6, true};
}

std::string password(const std::string& ssid)
{
    return "password " + ssid;
}

struct Device
{
    explicit Device(FakePlatform& platform) : fakes(platform)
    {
    }

    FakePlatform& fakes;
    EspManager esp{fakes.platform};
    uint32_t challenges = 0;

    void save(std::initializer_list<const char*> ssids)
    {
        for (const char* ssid : ssids)
        {
            TEST_ASSERT_TRUE(esp.networks().save(ssid, password(ssid)));
        }
    }

    // The SSIDs in the order they are tried.
    std::string order()
    {
        std::string text;
        for (const SavedNetwork& network : esp.networks().list())
        {
            text += (text.empty() ? "" : ",") + network.ssid;
        }
        return text;
    }

    // Each network's priority is its place in the list.
    void assertNumbered()
    {
        const std::vector<SavedNetwork> list = esp.networks().list();
        for (size_t i = 0; i < list.size(); ++i)
        {
            TEST_ASSERT_EQUAL_UINT8(i, list[i].priority);
        }
    }

    // begin(), the first scan finding `inRange`, and a successful connection.
    void connectSeeing(std::vector<ScanResult> inRange)
    {
        esp.begin();
        esp.process();
        fakes.wifi.completeScan(std::move(inRange));
        esp.process();
        fakes.wifi.completeConnect();
        esp.process();
        TEST_ASSERT_EQUAL(ConnectionState::Connected, esp.state());
    }

    // --- The Management Interface, on the station's address ---

    FakeHttpServer::Response station(HttpMethod method, const std::string& path,
                                     HttpRequest::Fields args = {},
                                     HttpRequest::Fields headers = {})
    {
        fakes.http.localIp = fakes.wifi.ip;
        return fakes.http.request(method, path, std::move(args), std::move(headers));
    }

    // Logs in as a browser would; `headers` go with the second request.
    FakeHttpServer::Response manage(HttpMethod method, const std::string& path,
                                    HttpRequest::Fields args = {},
                                    HttpRequest::Fields headers = kFromPage)
    {
        // A new nonce for each challenge; the fake clock's random numbers are 0 unless queued.
        ++challenges;
        fakes.clock.randomValues = {challenges, challenges, challenges, challenges};
        const FakeHttpServer::Response challenge = station(method, path, args);
        TEST_ASSERT_EQUAL_INT(401, challenge.status);
        DigestClient client;
        client.realm = esp.management().realm();
        client.password = kManagementPassword;
        const std::string nonce = DigestClient::nonceFrom(challenge.header("WWW-Authenticate"));
        headers.emplace_back(
            "Authorization",
            client.header(nonce, method == HttpMethod::Get ? "GET" : "POST", path));
        return station(method, path, std::move(args), std::move(headers));
    }

    // --- The Provisioning Portal, on the access point's address ---

    FakeHttpServer::Response portal(HttpMethod method, const std::string& path,
                                    HttpRequest::Fields args = {},
                                    HttpRequest::Fields headers = kFromPage)
    {
        fakes.http.localIp = fakes.wifi.apIp;
        return fakes.http.request(method, path, std::move(args), std::move(headers));
    }
};

// A Device with the Management Interface on and connected to the first of `ssids`.
struct Managed : Device
{
    explicit Managed(FakePlatform& platform, std::initializer_list<const char*> ssids)
        : Device(platform)
    {
        esp.management().enable();
        TEST_ASSERT_TRUE(esp.management().setPassword(kManagementPassword));
        save(ssids);
        connectSeeing({seen(esp.networks().list()[0].ssid)});
    }
};

// A Device with the Provisioning Portal open next to a connection.
struct Portal : Device
{
    explicit Portal(FakePlatform& platform, std::initializer_list<const char*> ssids)
        : Device(platform)
    {
        save(ssids);
        connectSeeing({seen(esp.networks().list()[0].ssid)});
        esp.startPortal();
        esp.process();
        TEST_ASSERT_EQUAL(PortalState::Active, esp.portalState());
    }
};

}  // namespace

void setUp()
{
}

void tearDown()
{
}

// --- The list (US05.2.1, US05.2.5) ---

void test_five_networks_fit_by_default_and_a_sixth_is_refused()
{
    FakePlatform fakes;
    Device d(fakes);
    TEST_ASSERT_EQUAL_size_t(SavedNetworks::kDefaultMaxNetworks, d.esp.networks().maxNetworks());
    d.save({"A", "B", "C", "D", "E"});
    TEST_ASSERT_TRUE(d.esp.networks().full());
    TEST_ASSERT_FALSE(d.esp.networks().save("F", password("F")));
    TEST_ASSERT_EQUAL_STRING("E,D,C,B,A", d.order().c_str());
    d.assertNumbered();
}

void test_the_developer_sets_the_limit()
{
    FakePlatform fakes;
    Device d(fakes);
    TEST_ASSERT_FALSE(d.esp.networks().setMaxNetworks(0));
    TEST_ASSERT_FALSE(d.esp.networks().setMaxNetworks(SavedNetworks::kMaxNetworksLimit + 1));
    TEST_ASSERT_EQUAL_size_t(5, d.esp.networks().maxNetworks());
    TEST_ASSERT_TRUE(d.esp.networks().setMaxNetworks(2));
    d.save({"A", "B"});
    TEST_ASSERT_FALSE(d.esp.networks().save("C", password("C")));
    TEST_ASSERT_TRUE(d.esp.networks().setMaxNetworks(SavedNetworks::kMaxNetworksLimit));
    TEST_ASSERT_TRUE(d.esp.networks().save("C", password("C")));
}

void test_a_lower_limit_keeps_a_longer_stored_list()
{
    FakePlatform fakes;
    {
        Device before(fakes);
        before.save({"A", "B", "C"});
    }
    Device d(fakes);
    TEST_ASSERT_TRUE(d.esp.networks().setMaxNetworks(2));
    d.esp.begin();
    TEST_ASSERT_EQUAL_STRING("C,B,A", d.order().c_str());
    TEST_ASSERT_FALSE(d.esp.networks().save("D", password("D")));
    TEST_ASSERT_TRUE(d.esp.networks().remove("A"));
    TEST_ASSERT_FALSE(d.esp.networks().save("D", password("D")));
    TEST_ASSERT_TRUE(d.esp.networks().remove("B"));
    TEST_ASSERT_TRUE(d.esp.networks().save("D", password("D")));
}

void test_saving_a_known_ssid_updates_it_in_its_place()
{
    FakePlatform fakes;
    Device d(fakes);
    TEST_ASSERT_TRUE(d.esp.networks().setMaxNetworks(3));
    d.save({"A", "B", "C"});
    TEST_ASSERT_TRUE(d.esp.networks().save("B", "new password"));
    TEST_ASSERT_EQUAL_STRING("C,B,A", d.order().c_str());
    TEST_ASSERT_EQUAL_STRING("new password", d.esp.networks().list()[1].password.c_str());
}

void test_priority_given_to_save_is_ignored()
{
    FakePlatform fakes;
    Device d(fakes);
    d.save({"A"});
    SavedNetwork network;
    network.ssid = "B";
    network.password = password("B");
    network.priority = 9;
    TEST_ASSERT_TRUE(d.esp.networks().save(network));
    TEST_ASSERT_EQUAL_STRING("B,A", d.order().c_str());
    d.assertNumbered();
}

// --- Removing and moving (US05.2.3, US05.2.4) ---

void test_remove_forgets_one_network()
{
    FakePlatform fakes;
    Device d(fakes);
    d.save({"A", "B", "C"});
    TEST_ASSERT_TRUE(d.esp.networks().remove("B"));
    TEST_ASSERT_FALSE(d.esp.networks().remove("B"));
    TEST_ASSERT_FALSE(d.esp.networks().contains("B"));
    TEST_ASSERT_EQUAL_STRING("C,A", d.order().c_str());
    d.assertNumbered();
}

void test_move_puts_a_network_in_another_place()
{
    FakePlatform fakes;
    Device d(fakes);
    d.save({"A", "B", "C", "D"});  // D,C,B,A
    TEST_ASSERT_TRUE(d.esp.networks().move("A", 0));
    TEST_ASSERT_EQUAL_STRING("A,D,C,B", d.order().c_str());
    TEST_ASSERT_TRUE(d.esp.networks().move("D", 3));
    TEST_ASSERT_EQUAL_STRING("A,C,B,D", d.order().c_str());
    TEST_ASSERT_TRUE(d.esp.networks().move("C", 1));
    TEST_ASSERT_EQUAL_STRING("A,C,B,D", d.order().c_str());
    TEST_ASSERT_FALSE(d.esp.networks().move("C", 4));
    TEST_ASSERT_FALSE(d.esp.networks().move("X", 0));
    d.assertNumbered();
}

void test_changes_are_stored_for_the_next_start()
{
    FakePlatform fakes;
    {
        Device before(fakes);
        before.save({"A", "B", "C"});
        TEST_ASSERT_TRUE(before.esp.networks().move("A", 0));
        TEST_ASSERT_TRUE(before.esp.networks().remove("B"));
    }
    Device d(fakes);
    d.esp.begin();
    TEST_ASSERT_EQUAL_STRING("A,C", d.order().c_str());
    d.assertNumbered();
}

void test_failed_write_changes_nothing()
{
    FakePlatform fakes;
    Device d(fakes);
    d.save({"A", "B"});
    fakes.storage.writable = false;
    TEST_ASSERT_FALSE(d.esp.networks().move("A", 0));
    TEST_ASSERT_FALSE(d.esp.networks().remove("A"));
    TEST_ASSERT_FALSE(d.esp.networks().save("C", password("C")));
    TEST_ASSERT_EQUAL_STRING("B,A", d.order().c_str());
}

// --- Which network it connects to (US05.2.2) ---

void test_connects_to_the_first_network_in_range()
{
    FakePlatform fakes;
    Device d(fakes);
    d.save({"C", "B", "A"});  // A,B,C
    d.esp.begin();
    d.esp.process();
    fakes.wifi.completeScan({seen("C", -40), seen("B", -80), seen("Other", -30)});
    d.esp.process();
    TEST_ASSERT_EQUAL_STRING("B", fakes.wifi.connectCalls.back().ssid.c_str());
    TEST_ASSERT_EQUAL_STRING(password("B").c_str(),
                             fakes.wifi.connectCalls.back().password.c_str());
}

void test_moves_to_another_network_when_the_first_goes_away()
{
    FakePlatform fakes;
    Device d(fakes);
    d.save({"B", "A"});  // A,B
    d.connectSeeing({seen("A"), seen("B")});
    TEST_ASSERT_EQUAL_STRING("A", fakes.wifi.connectCalls.back().ssid.c_str());

    fakes.wifi.loseConnection();
    d.esp.process();
    fakes.clock.advance(espmgr::WifiConnection::kDefaultMinRetryDelayMs);
    d.esp.process();
    fakes.wifi.completeScan({seen("B")});
    d.esp.process();
    TEST_ASSERT_EQUAL_STRING("B", fakes.wifi.connectCalls.back().ssid.c_str());
    fakes.wifi.completeConnect();
    d.esp.process();
    TEST_ASSERT_EQUAL(ConnectionState::Connected, d.esp.state());

    // A is back but the device stays on B until it loses that connection.
    fakes.wifi.loseConnection();
    d.esp.process();
    fakes.clock.advance(espmgr::WifiConnection::kDefaultMinRetryDelayMs);
    d.esp.process();
    fakes.wifi.completeScan({seen("A"), seen("B")});
    d.esp.process();
    TEST_ASSERT_EQUAL_STRING("A", fakes.wifi.connectCalls.back().ssid.c_str());
}

void test_after_a_move_the_new_first_is_tried_first()
{
    FakePlatform fakes;
    Device d(fakes);
    d.save({"B", "A"});  // A,B
    TEST_ASSERT_TRUE(d.esp.networks().move("B", 0));
    d.esp.begin();
    d.esp.process();
    fakes.wifi.completeScan({seen("A", -30), seen("B", -85)});
    d.esp.process();
    TEST_ASSERT_EQUAL_STRING("B", fakes.wifi.connectCalls.back().ssid.c_str());
}

void test_removing_the_connected_network_keeps_the_connection_until_it_drops()
{
    FakePlatform fakes;
    Device d(fakes);
    d.save({"B", "A"});  // A,B
    d.connectSeeing({seen("A"), seen("B")});
    TEST_ASSERT_TRUE(d.esp.networks().remove("A"));
    d.esp.process();
    TEST_ASSERT_EQUAL(ConnectionState::Connected, d.esp.state());
    TEST_ASSERT_EQUAL(0, fakes.wifi.disconnectCalls);

    fakes.wifi.loseConnection();
    d.esp.process();
    fakes.clock.advance(espmgr::WifiConnection::kDefaultMinRetryDelayMs);
    d.esp.process();
    fakes.wifi.completeScan({seen("A"), seen("B")});
    d.esp.process();
    TEST_ASSERT_EQUAL_STRING("B", fakes.wifi.connectCalls.back().ssid.c_str());
}

// --- In the Management Interface ---

void test_management_lists_the_networks_in_order_without_passwords()
{
    FakePlatform fakes;
    Managed d(fakes, {"B", "A"});
    const FakeHttpServer::Response response = d.manage(HttpMethod::Get, "/api/networks", {}, {});
    TEST_ASSERT_EQUAL_INT(200, response.status);
    TEST_ASSERT_EQUAL_STRING("{\"connected\":\"A\",\"max\":5,\"networks\":[{\"ssid\":\"A\","
                             "\"priority\":0},{\"ssid\":\"B\",\"priority\":1}]}",
                             response.body.c_str());
    TEST_ASSERT_TRUE(response.body.find("password") == std::string::npos);
}

void test_management_adds_without_testing()
{
    FakePlatform fakes;
    Managed d(fakes, {"A"});
    const size_t attempts = fakes.wifi.connectCalls.size();
    const FakeHttpServer::Response response = d.manage(
        HttpMethod::Post, "/api/networks/add", {{"ssid", "B"}, {"password", password("B")}});
    TEST_ASSERT_EQUAL_INT(200, response.status);
    TEST_ASSERT_TRUE(response.body.find("\"networks\":[{\"ssid\":\"B\"") != std::string::npos);
    TEST_ASSERT_TRUE(response.body.find(password("B")) == std::string::npos);
    TEST_ASSERT_EQUAL_STRING("B,A", d.order().c_str());
    d.esp.process();
    TEST_ASSERT_EQUAL(ConnectionState::Connected, d.esp.state());
    TEST_ASSERT_EQUAL_size_t(attempts, fakes.wifi.connectCalls.size());
}

void test_management_refuses_an_invalid_network_or_a_full_list()
{
    FakePlatform fakes;
    Managed d(fakes, {"A"});
    TEST_ASSERT_TRUE(d.esp.networks().setMaxNetworks(2));
    FakeHttpServer::Response response =
        d.manage(HttpMethod::Post, "/api/networks/add", {{"ssid", "B"}, {"password", "short"}});
    TEST_ASSERT_EQUAL_INT(400, response.status);
    TEST_ASSERT_EQUAL_STRING("{\"error\":\"password-too-short\"}", response.body.c_str());

    d.save({"B"});
    response = d.manage(HttpMethod::Post, "/api/networks/add",
                        {{"ssid", "C"}, {"password", password("C")}});
    TEST_ASSERT_EQUAL_INT(409, response.status);
    TEST_ASSERT_EQUAL_STRING("{\"error\":\"full\"}", response.body.c_str());

    // A known SSID still fits.
    response = d.manage(HttpMethod::Post, "/api/networks/add",
                        {{"ssid", "A"}, {"password", "another password"}});
    TEST_ASSERT_EQUAL_INT(200, response.status);
    TEST_ASSERT_EQUAL_STRING("B,A", d.order().c_str());
}

void test_management_removes_and_moves()
{
    FakePlatform fakes;
    Managed d(fakes, {"C", "B", "A"});  // A,B,C
    FakeHttpServer::Response response =
        d.manage(HttpMethod::Post, "/api/networks/move", {{"ssid", "C"}, {"position", "0"}});
    TEST_ASSERT_EQUAL_INT(200, response.status);
    TEST_ASSERT_EQUAL_STRING("C,A,B", d.order().c_str());
    TEST_ASSERT_TRUE(response.body.find("\"networks\":[{\"ssid\":\"C\",\"priority\":0}") !=
                     std::string::npos);

    response = d.manage(HttpMethod::Post, "/api/networks/remove", {{"ssid", "A"}});
    TEST_ASSERT_EQUAL_INT(200, response.status);
    TEST_ASSERT_EQUAL_STRING("C,B", d.order().c_str());
}

void test_management_refuses_bad_moves_and_unknown_networks()
{
    FakePlatform fakes;
    Managed d(fakes, {"B", "A"});
    for (const char* position : {"", "2", "-1", "1x", "99999999999999999999"})
    {
        const FakeHttpServer::Response response = d.manage(HttpMethod::Post, "/api/networks/move",
                                                           {{"ssid", "A"}, {"position", position}});
        TEST_ASSERT_EQUAL_INT_MESSAGE(400, response.status, position);
        TEST_ASSERT_EQUAL_STRING("{\"error\":\"position\"}", response.body.c_str());
    }
    FakeHttpServer::Response response =
        d.manage(HttpMethod::Post, "/api/networks/move", {{"ssid", "X"}, {"position", "0"}});
    TEST_ASSERT_EQUAL_INT(404, response.status);
    response = d.manage(HttpMethod::Post, "/api/networks/remove", {{"ssid", "X"}});
    TEST_ASSERT_EQUAL_INT(404, response.status);
    TEST_ASSERT_EQUAL_STRING("{\"error\":\"not-found\"}", response.body.c_str());
    TEST_ASSERT_EQUAL_STRING("A,B", d.order().c_str());
}

void test_management_keeps_the_last_network()
{
    FakePlatform fakes;
    Managed d(fakes, {"A"});
    const FakeHttpServer::Response response =
        d.manage(HttpMethod::Post, "/api/networks/remove", {{"ssid", "A"}});
    TEST_ASSERT_EQUAL_INT(409, response.status);
    TEST_ASSERT_EQUAL_STRING("{\"error\":\"last-network\"}", response.body.c_str());
    TEST_ASSERT_EQUAL_size_t(1, d.esp.networks().count());
}

void test_management_changes_need_the_password_and_the_page_header()
{
    FakePlatform fakes;
    Managed d(fakes, {"B", "A"});
    TEST_ASSERT_EQUAL_INT(
        401,
        d.station(HttpMethod::Post, "/api/networks/remove", {{"ssid", "A"}}, kFromPage).status);
    for (const char* path : {"/api/networks/remove", "/api/networks/move", "/api/networks/add"})
    {
        const FakeHttpServer::Response response =
            d.manage(HttpMethod::Post, path,
                     {{"ssid", "A"}, {"position", "1"}, {"password", password("A")}}, {});
        TEST_ASSERT_EQUAL_INT_MESSAGE(403, response.status, path);
    }
    TEST_ASSERT_EQUAL_STRING("A,B", d.order().c_str());
}

// --- In the Provisioning Portal ---

void test_portal_lists_removes_and_moves()
{
    FakePlatform fakes;
    Portal d(fakes, {"C", "B", "A"});  // A,B,C
    FakeHttpServer::Response response = d.portal(HttpMethod::Get, "/api/networks", {}, {});
    TEST_ASSERT_EQUAL_INT(200, response.status);
    TEST_ASSERT_TRUE(response.body.find("\"max\":5") != std::string::npos);

    response = d.portal(HttpMethod::Post, "/api/networks/move", {{"ssid", "C"}, {"position", "0"}});
    TEST_ASSERT_EQUAL_INT(200, response.status);
    response = d.portal(HttpMethod::Post, "/api/networks/remove", {{"ssid", "A"}});
    TEST_ASSERT_EQUAL_INT(200, response.status);
    TEST_ASSERT_EQUAL_STRING("C,B", d.order().c_str());
}

void test_portal_may_remove_every_network_but_has_no_add()
{
    FakePlatform fakes;
    Portal d(fakes, {"A"});
    TEST_ASSERT_EQUAL_INT(
        200, d.portal(HttpMethod::Post, "/api/networks/remove", {{"ssid", "A"}}).status);
    TEST_ASSERT_EQUAL_size_t(0, d.esp.networks().count());
    TEST_ASSERT_NOT_EQUAL(200, d.portal(HttpMethod::Post, "/api/networks/add",
                                        {{"ssid", "B"}, {"password", password("B")}})
                                   .status);
    TEST_ASSERT_EQUAL_size_t(0, d.esp.networks().count());
}

void test_portal_changes_need_the_page_header()
{
    FakePlatform fakes;
    Portal d(fakes, {"B", "A"});
    TEST_ASSERT_EQUAL_INT(
        403, d.portal(HttpMethod::Post, "/api/networks/remove", {{"ssid", "A"}}, {}).status);
    TEST_ASSERT_EQUAL_STRING("A,B", d.order().c_str());
}

void test_portal_refuses_a_new_network_when_full_before_testing_it()
{
    FakePlatform fakes;
    Portal d(fakes, {"A"});
    TEST_ASSERT_TRUE(d.esp.networks().setMaxNetworks(1));
    const size_t attempts = fakes.wifi.connectCalls.size();
    FakeHttpServer::Response response = d.portal(HttpMethod::Post, "/api/connect",
                                                 {{"ssid", "B"}, {"password", password("B")}}, {});
    TEST_ASSERT_EQUAL_INT(409, response.status);
    TEST_ASSERT_EQUAL_STRING("{\"error\":\"full\"}", response.body.c_str());
    TEST_ASSERT_EQUAL_size_t(attempts, fakes.wifi.connectCalls.size());

    // The Saved Network itself can still be tested again, for a new password say.
    response = d.portal(HttpMethod::Post, "/api/connect",
                        {{"ssid", "A"}, {"password", "another password"}}, {});
    TEST_ASSERT_EQUAL_INT(200, response.status);
    TEST_ASSERT_EQUAL_STRING("A", fakes.wifi.connectCalls.back().ssid.c_str());
}

void test_portal_puts_a_tested_network_on_top()
{
    FakePlatform fakes;
    Portal d(fakes, {"B", "A"});  // A,B
    d.portal(HttpMethod::Post, "/api/connect", {{"ssid", "C"}, {"password", password("C")}}, {});
    fakes.wifi.completeConnect();
    d.esp.process();
    TEST_ASSERT_EQUAL_STRING("C,A,B", d.order().c_str());
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_five_networks_fit_by_default_and_a_sixth_is_refused);
    RUN_TEST(test_the_developer_sets_the_limit);
    RUN_TEST(test_a_lower_limit_keeps_a_longer_stored_list);
    RUN_TEST(test_saving_a_known_ssid_updates_it_in_its_place);
    RUN_TEST(test_priority_given_to_save_is_ignored);
    RUN_TEST(test_remove_forgets_one_network);
    RUN_TEST(test_move_puts_a_network_in_another_place);
    RUN_TEST(test_changes_are_stored_for_the_next_start);
    RUN_TEST(test_failed_write_changes_nothing);
    RUN_TEST(test_connects_to_the_first_network_in_range);
    RUN_TEST(test_moves_to_another_network_when_the_first_goes_away);
    RUN_TEST(test_after_a_move_the_new_first_is_tried_first);
    RUN_TEST(test_removing_the_connected_network_keeps_the_connection_until_it_drops);
    RUN_TEST(test_management_lists_the_networks_in_order_without_passwords);
    RUN_TEST(test_management_adds_without_testing);
    RUN_TEST(test_management_refuses_an_invalid_network_or_a_full_list);
    RUN_TEST(test_management_removes_and_moves);
    RUN_TEST(test_management_refuses_bad_moves_and_unknown_networks);
    RUN_TEST(test_management_keeps_the_last_network);
    RUN_TEST(test_management_changes_need_the_password_and_the_page_header);
    RUN_TEST(test_portal_lists_removes_and_moves);
    RUN_TEST(test_portal_may_remove_every_network_but_has_no_add);
    RUN_TEST(test_portal_changes_need_the_page_header);
    RUN_TEST(test_portal_refuses_a_new_network_when_full_before_testing_it);
    RUN_TEST(test_portal_puts_a_tested_network_on_top);
    return UNITY_END();
}
