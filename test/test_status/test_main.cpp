#include <EspManager.h>
#include <FakePlatform.h>
#include <unity.h>

#include <string>

using espmgr::ConnectionState;
using espmgr::DeviceStatus;
using espmgr::FakePlatform;
using espmgr::HttpMethod;
using espmgr::PortalState;

namespace
{

// An EspManager on fakes with a Saved Network, connected after connect().
struct Device
{
    FakePlatform fakes;
    EspManager esp{fakes.platform};

    Device()
    {
        fakes.wifi.scanStartSucceeds = false;
        TEST_ASSERT_TRUE(esp.networks().save("Home", "correct horse"));
        esp.begin();
    }

    void connect()
    {
        esp.process();
        fakes.wifi.completeConnect(11);
        esp.process();
        TEST_ASSERT_EQUAL(ConnectionState::Connected, esp.state());
    }
};

}  // namespace

void setUp()
{
}

void tearDown()
{
}

void test_connected_status_has_the_connection_details()
{
    Device d;
    d.connect();
    d.fakes.clock.advance(12345);
    const DeviceStatus status = d.esp.status();
    TEST_ASSERT_EQUAL(ConnectionState::Connected, status.connection);
    TEST_ASSERT_EQUAL(PortalState::Off, status.portal);
    TEST_ASSERT_EQUAL_STRING("Home", status.ssid.c_str());
    TEST_ASSERT_EQUAL_STRING("192.168.1.50", status.ip.toString().c_str());
    TEST_ASSERT_EQUAL_STRING("192.168.1.1", status.gateway.toString().c_str());
    TEST_ASSERT_EQUAL_INT8(-55, status.rssi);
    TEST_ASSERT_EQUAL_UINT8(11, status.channel);
}

void test_status_has_the_device_details()
{
    Device d;
    d.fakes.clock.advance(12345);
    d.fakes.system.heap = 123456;
    d.esp.setFirmwareVersion("1.4.2");
    const DeviceStatus status = d.esp.status();
    TEST_ASSERT_EQUAL_STRING("esp-abcdef", status.hostname.c_str());
    TEST_ASSERT_EQUAL_STRING("24:6f:28:ab:cd:ef", status.mac.c_str());
    TEST_ASSERT_EQUAL_UINT64(12345, status.uptimeMs);
    TEST_ASSERT_EQUAL_UINT32(123456, status.freeHeap);
    TEST_ASSERT_EQUAL_STRING("ESP32-C3 rev 4", status.chip.c_str());
    TEST_ASSERT_EQUAL_STRING("1.4.2", status.firmwareVersion.c_str());
    TEST_ASSERT_EQUAL_STRING(ESPMGR_VERSION, status.libraryVersion.c_str());
}

void test_not_connected_status_leaves_the_connection_details_empty()
{
    Device d;
    d.esp.process();
    DeviceStatus status = d.esp.status();
    TEST_ASSERT_EQUAL(ConnectionState::Connecting, status.connection);
    TEST_ASSERT_EQUAL_STRING("Home", status.ssid.c_str());
    TEST_ASSERT_TRUE(status.ip.isUnset());
    TEST_ASSERT_EQUAL_INT8(0, status.rssi);
    TEST_ASSERT_EQUAL_UINT8(0, status.channel);

    TEST_ASSERT_TRUE(d.esp.networks().clear());
    d.fakes.wifi.failConnect(espmgr::FailureReason::WrongPassword);
    d.esp.process();
    d.esp.process();
    status = d.esp.status();
    TEST_ASSERT_EQUAL(ConnectionState::Idle, status.connection);
    TEST_ASSERT_EQUAL_STRING("", status.ssid.c_str());
}

void test_status_json_has_every_field()
{
    Device d;
    d.connect();
    d.fakes.clock.advance(5000);
    d.esp.setFirmwareVersion("1.4.2");
    espmgr::JsonWriter json;
    espmgr::writeJson(json, d.esp.status());
    const std::string expected =
        R"({"connection":"Connected","portal":"Off","ssid":"Home","ip":"192.168.1.50",)"
        R"("gateway":"192.168.1.1","rssi":-55,"channel":11,"hostname":"esp-abcdef",)"
        R"("mac":"24:6f:28:ab:cd:ef","uptimeMs":5000,"freeHeap":180000,)"
        R"("chip":"ESP32-C3 rev 4","firmware":"1.4.2","library":")" ESPMGR_VERSION R"("})";
    TEST_ASSERT_EQUAL_STRING(expected.c_str(), json.str().c_str());
}

void test_portal_serves_the_status_on_its_access_point()
{
    Device d;
    d.connect();
    d.esp.startPortal();
    d.esp.process();
    const auto response = d.fakes.http.request(HttpMethod::Get, "/api/status");
    TEST_ASSERT_EQUAL_INT(200, response.status);
    TEST_ASSERT_EQUAL_STRING("application/json", response.contentType.c_str());
    TEST_ASSERT_TRUE(response.body.find(R"("connection":"Connected","portal":"Active")") !=
                     std::string::npos);

    d.fakes.http.localIp = d.fakes.wifi.ip;
    TEST_ASSERT_EQUAL_INT(403, d.fakes.http.request(HttpMethod::Get, "/api/status").status);
}

void test_status_never_holds_the_password()
{
    Device d;
    d.connect();
    d.esp.startPortal();
    d.esp.process();
    const auto response = d.fakes.http.request(HttpMethod::Get, "/api/status");
    TEST_ASSERT_TRUE(response.body.find("correct horse") == std::string::npos);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_connected_status_has_the_connection_details);
    RUN_TEST(test_status_has_the_device_details);
    RUN_TEST(test_not_connected_status_leaves_the_connection_details_empty);
    RUN_TEST(test_status_json_has_every_field);
    RUN_TEST(test_portal_serves_the_status_on_its_access_point);
    RUN_TEST(test_status_never_holds_the_password);
    return UNITY_END();
}
