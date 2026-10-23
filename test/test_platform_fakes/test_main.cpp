// The fakes the core is tested against (US16.1.3). Core code only sees the interfaces, so these
// tests go through WifiDriver&, Clock&, Storage& and HttpServer& wherever the core would.

#include <FakePlatform.h>
#include <unity.h>

using namespace espmgr;

namespace
{

FakePlatform* fakes = nullptr;

// Polls like the core will: one step per process(), giving up after `timeoutMs`.
StationStatus pollUntilSettled(WifiDriver& wifi, Clock& clock, uint64_t timeoutMs, uint64_t stepMs,
                               void (*onStep)(int step))
{
    const uint64_t start = clock.nowMs();
    for (int step = 0; wifi.status() == StationStatus::Connecting; ++step)
    {
        if (clock.nowMs() - start >= timeoutMs)
        {
            break;
        }
        onStep(step);
        fakes->clock.advance(stepMs);
    }
    return wifi.status();
}

}  // namespace

void setUp()
{
    fakes = new FakePlatform();
}

void tearDown()
{
    delete fakes;
    fakes = nullptr;
}

void test_connect_stays_connecting_until_the_test_completes_it()
{
    WifiDriver& wifi = fakes->platform.wifi;
    wifi.setHostname("esp-abcdef");
    wifi.connect("Home", "secret123");

    const StationStatus status = pollUntilSettled(wifi, fakes->platform.clock, 10000, 100,
                                                  [](int step)
                                                  {
                                                      if (step == 30)
                                                      {
                                                          fakes->wifi.completeConnect(11);
                                                      }
                                                  });

    TEST_ASSERT_EQUAL(StationStatus::Connected, status);
    TEST_ASSERT_EQUAL_UINT64(3100, fakes->clock.nowMs());
    TEST_ASSERT_EQUAL_UINT8(11, wifi.channel());
    TEST_ASSERT_EQUAL(FailureReason::None, wifi.lastFailure());
    TEST_ASSERT_TRUE(wifi.localIp() == Ipv4Address(192, 168, 1, 50));
    TEST_ASSERT_EQUAL_size_t(1, fakes->wifi.connectCalls.size());
    TEST_ASSERT_EQUAL_STRING("Home", fakes->wifi.connectCalls[0].ssid.c_str());
    TEST_ASSERT_EQUAL_STRING("secret123", fakes->wifi.connectCalls[0].password.c_str());
    TEST_ASSERT_EQUAL_STRING("esp-abcdef", fakes->wifi.connectCalls[0].hostname.c_str());
}

void test_connect_that_never_completes_times_out_on_the_fake_clock()
{
    WifiDriver& wifi = fakes->platform.wifi;
    wifi.connect("Home", "secret123");

    const StationStatus status =
        pollUntilSettled(wifi, fakes->platform.clock, 10000, 250, [](int) {});

    TEST_ASSERT_EQUAL(StationStatus::Connecting, status);
    TEST_ASSERT_EQUAL_UINT64(10000, fakes->clock.nowMs());
}

void test_failed_connect_reports_the_reason_until_the_next_attempt()
{
    WifiDriver& wifi = fakes->platform.wifi;
    wifi.connect("Home", "wrong");
    fakes->wifi.failConnect(FailureReason::WrongPassword);

    TEST_ASSERT_EQUAL(StationStatus::Disconnected, wifi.status());
    TEST_ASSERT_EQUAL(FailureReason::WrongPassword, wifi.lastFailure());
    TEST_ASSERT_TRUE(wifi.localIp().isUnset());

    wifi.connect("Home", "right");
    TEST_ASSERT_EQUAL(FailureReason::None, wifi.lastFailure());
}

void test_lost_connection_is_reported_as_connection_lost()
{
    WifiDriver& wifi = fakes->platform.wifi;
    wifi.connect("Home", "secret123");
    fakes->wifi.completeConnect();
    fakes->wifi.loseConnection();

    TEST_ASSERT_EQUAL(StationStatus::Disconnected, wifi.status());
    TEST_ASSERT_EQUAL(FailureReason::ConnectionLost, wifi.lastFailure());
}

void test_static_ip_is_used_by_the_next_connect()
{
    WifiDriver& wifi = fakes->platform.wifi;
    StaticIpConfig config;
    config.ip = Ipv4Address(192, 168, 1, 20);
    config.gateway = Ipv4Address(192, 168, 1, 1);
    config.subnet = Ipv4Address(255, 255, 255, 0);
    wifi.useStaticIp(config);
    wifi.connect("Home", "secret123");
    wifi.useDhcp();
    wifi.connect("Home", "secret123");

    TEST_ASSERT_TRUE(fakes->wifi.connectCalls[0].staticIp);
    TEST_ASSERT_TRUE(fakes->wifi.connectCalls[0].staticIpConfig.ip == config.ip);
    TEST_ASSERT_FALSE(fakes->wifi.connectCalls[1].staticIp);
}

void test_scan_runs_until_the_test_completes_it()
{
    WifiDriver& wifi = fakes->platform.wifi;
    TEST_ASSERT_TRUE(wifi.startScan());
    TEST_ASSERT_FALSE(wifi.scanComplete());

    fakes->wifi.completeScan({{"Home", -48, 6, true}, {"Cafe", -80, 1, false}});

    TEST_ASSERT_TRUE(wifi.scanComplete());
    const std::vector<ScanResult> results = wifi.scanResults();
    TEST_ASSERT_EQUAL_size_t(2, results.size());
    TEST_ASSERT_EQUAL_STRING("Home", results[0].ssid.c_str());
    TEST_ASSERT_EQUAL_INT8(-48, results[0].rssi);
    TEST_ASSERT_FALSE(results[1].secured);
}

void test_scan_that_cannot_start_reports_false()
{
    fakes->wifi.scanStartSucceeds = false;
    TEST_ASSERT_FALSE(fakes->platform.wifi.startScan());
    TEST_ASSERT_TRUE(fakes->platform.wifi.scanComplete());
}

void test_access_point_runs_alongside_the_station()
{
    WifiDriver& wifi = fakes->platform.wifi;
    wifi.connect("Home", "secret123");
    TEST_ASSERT_TRUE(wifi.startAccessPoint("esp-abcdef", "", 6));
    fakes->wifi.clientCount = 1;

    TEST_ASSERT_TRUE(fakes->wifi.accessPointActive);
    TEST_ASSERT_EQUAL_UINT8(6, wifi.channel());
    TEST_ASSERT_EQUAL_UINT8(1, wifi.accessPointClientCount());
    TEST_ASSERT_EQUAL(StationStatus::Connecting, wifi.status());

    wifi.stopAccessPoint();
    TEST_ASSERT_EQUAL_UINT8(0, wifi.accessPointClientCount());
}

void test_clock_random_uses_queued_values_below_the_limit()
{
    Clock& clock = fakes->platform.clock;
    fakes->clock.randomValues = {7, 25};

    TEST_ASSERT_EQUAL_UINT32(7, clock.random(10));
    TEST_ASSERT_EQUAL_UINT32(5, clock.random(10));
    TEST_ASSERT_EQUAL_UINT32(0, clock.random(10));
    TEST_ASSERT_EQUAL_UINT32(0, clock.random(0));
}

void test_storage_round_trips_each_type()
{
    Storage& storage = fakes->platform.storage;
    TEST_ASSERT_TRUE(storage.putString("wifi", "ssid0", "Home"));
    TEST_ASSERT_TRUE(storage.putU32("wifi", "count", 3));
    TEST_ASSERT_TRUE(storage.putBool("wifi", "dhcp0", true));
    TEST_ASSERT_TRUE(storage.putBytes("wifi", "ip0", {192, 168, 1, 20}));

    TEST_ASSERT_EQUAL_STRING("Home", storage.getString("wifi", "ssid0").value().c_str());
    TEST_ASSERT_EQUAL_UINT32(3, storage.getU32("wifi", "count").value());
    TEST_ASSERT_TRUE(storage.getBool("wifi", "dhcp0").value());
    TEST_ASSERT_EQUAL_size_t(4, storage.getBytes("wifi", "ip0").value().size());
}

void test_storage_returns_nothing_for_missing_keys_and_other_types()
{
    Storage& storage = fakes->platform.storage;
    storage.putString("wifi", "ssid0", "Home");

    TEST_ASSERT_FALSE(storage.getString("wifi", "ssid1").has_value());
    TEST_ASSERT_FALSE(storage.getString("other", "ssid0").has_value());
    TEST_ASSERT_FALSE(storage.getU32("wifi", "ssid0").has_value());
}

void test_storage_rejects_names_nvs_would_reject()
{
    Storage& storage = fakes->platform.storage;
    TEST_ASSERT_TRUE(storage.putString("wifi", "fifteen_chars_k", "ok"));
    TEST_ASSERT_FALSE(storage.putString("wifi", "sixteen_chars_ke", "too long"));
    TEST_ASSERT_FALSE(storage.putString("namespace_is_16c", "key", "too long"));
    TEST_ASSERT_FALSE(storage.putString("wifi", "", "empty"));
    TEST_ASSERT_FALSE(storage.clear("namespace_is_16c"));
}

void test_storage_clear_only_touches_its_own_namespace()
{
    Storage& storage = fakes->platform.storage;
    storage.putString("wifi", "ssid0", "Home");
    storage.putString("settings", "mqtt", "broker.local");

    TEST_ASSERT_TRUE(storage.clear("wifi"));
    TEST_ASSERT_TRUE(storage.remove("settings", "missing"));

    TEST_ASSERT_FALSE(storage.getString("wifi", "ssid0").has_value());
    TEST_ASSERT_EQUAL_STRING("broker.local", storage.getString("settings", "mqtt").value().c_str());
}

void test_storage_reports_failed_writes()
{
    fakes->storage.writable = false;
    TEST_ASSERT_FALSE(fakes->platform.storage.putU32("wifi", "count", 1));
}

void test_http_route_answers_with_the_handler_response()
{
    HttpServer& http = fakes->platform.http;
    http.on(HttpMethod::Get, "/status",
            [&http](const HttpRequest& request)
            {
                const std::string name = request.arg("name").value_or("nobody");
                http.send(200, "text/plain", "hello " + name);
            });
    http.begin(80);

    const FakeHttpServer::Response response =
        fakes->http.request(HttpMethod::Get, "/status", {{"name", "owner"}});

    TEST_ASSERT_TRUE(response.sent);
    TEST_ASSERT_EQUAL_INT(200, response.status);
    TEST_ASSERT_EQUAL_STRING("text/plain", response.contentType.c_str());
    TEST_ASSERT_EQUAL_STRING("hello owner", response.body.c_str());
    TEST_ASSERT_EQUAL_UINT16(80, fakes->http.port);
}

void test_http_unknown_path_and_wrong_method_go_to_not_found()
{
    HttpServer& http = fakes->platform.http;
    http.on(HttpMethod::Post, "/save", [&http](const HttpRequest&) { http.send(200, "", ""); });
    http.onNotFound(
        [&http](const HttpRequest& request)
        {
            const std::string host = request.header("host").value_or("");
            http.sendRedirect("http://192.168.4.1/?from=" + host);
        });
    http.begin(80);

    const FakeHttpServer::Response response = fakes->http.request(
        HttpMethod::Get, "/save", {}, {{"Host", "connectivitycheck.gstatic.com"}});

    TEST_ASSERT_EQUAL_INT(302, response.status);
    TEST_ASSERT_EQUAL_STRING("http://192.168.4.1/?from=connectivitycheck.gstatic.com",
                             response.location.c_str());
}

void test_http_digest_challenge_when_not_authorized()
{
    HttpServer& http = fakes->platform.http;
    http.on(HttpMethod::Get, "/manage",
            [&http](const HttpRequest&)
            {
                if (!http.authenticateDigest("esp-abcdef", "admin", "owner-password"))
                {
                    return;
                }
                http.send(200, "text/html", "ok");
            });
    http.begin(80);

    TEST_ASSERT_EQUAL_INT(401, fakes->http.request(HttpMethod::Get, "/manage").status);
    TEST_ASSERT_EQUAL_STRING("admin", fakes->http.lastAuthUser.c_str());

    fakes->http.authorized = true;
    TEST_ASSERT_EQUAL_INT(200, fakes->http.request(HttpMethod::Get, "/manage").status);
}

void test_http_gzipped_body_and_captive_dns()
{
    static const uint8_t page[] = {0x1f, 0x8b, 0x08};
    HttpServer& http = fakes->platform.http;
    http.on(HttpMethod::Any, "/",
            [&http](const HttpRequest&) { http.send(200, "text/html", page, sizeof(page), true); });
    http.begin(80);
    TEST_ASSERT_TRUE(http.startCaptiveDns(Ipv4Address(192, 168, 4, 1)));

    const FakeHttpServer::Response response = fakes->http.request(HttpMethod::Post, "/");

    TEST_ASSERT_TRUE(response.gzipped);
    TEST_ASSERT_EQUAL_size_t(3, response.body.size());
    TEST_ASSERT_TRUE(fakes->http.captiveDnsActive);
    TEST_ASSERT_TRUE(fakes->http.captiveDnsIp == Ipv4Address(192, 168, 4, 1));
}

void test_http_stopped_server_answers_nothing()
{
    HttpServer& http = fakes->platform.http;
    http.on(HttpMethod::Get, "/", [&http](const HttpRequest&) { http.send(200, "", ""); });

    TEST_ASSERT_FALSE(fakes->http.request(HttpMethod::Get, "/").sent);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_connect_stays_connecting_until_the_test_completes_it);
    RUN_TEST(test_connect_that_never_completes_times_out_on_the_fake_clock);
    RUN_TEST(test_failed_connect_reports_the_reason_until_the_next_attempt);
    RUN_TEST(test_lost_connection_is_reported_as_connection_lost);
    RUN_TEST(test_static_ip_is_used_by_the_next_connect);
    RUN_TEST(test_scan_runs_until_the_test_completes_it);
    RUN_TEST(test_scan_that_cannot_start_reports_false);
    RUN_TEST(test_access_point_runs_alongside_the_station);
    RUN_TEST(test_clock_random_uses_queued_values_below_the_limit);
    RUN_TEST(test_storage_round_trips_each_type);
    RUN_TEST(test_storage_returns_nothing_for_missing_keys_and_other_types);
    RUN_TEST(test_storage_rejects_names_nvs_would_reject);
    RUN_TEST(test_storage_clear_only_touches_its_own_namespace);
    RUN_TEST(test_storage_reports_failed_writes);
    RUN_TEST(test_http_route_answers_with_the_handler_response);
    RUN_TEST(test_http_unknown_path_and_wrong_method_go_to_not_found);
    RUN_TEST(test_http_digest_challenge_when_not_authorized);
    RUN_TEST(test_http_gzipped_body_and_captive_dns);
    RUN_TEST(test_http_stopped_server_answers_nothing);
    return UNITY_END();
}
