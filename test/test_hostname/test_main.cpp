#include <EspManager.h>
#include <FakePlatform.h>
#include <core/DeviceName.h>
#include <unity.h>

#include <string>

using espmgr::FakePlatform;
using espmgr::MacAddress;

namespace
{

// The fake's MAC ends in ab:cd:ef.
struct Device
{
    FakePlatform fakes;
    EspManager esp{fakes.platform};
};

}  // namespace

void setUp()
{
}

void tearDown()
{
}

void test_default_name_is_prefix_and_last_six_hex_digits_of_the_mac()
{
    const MacAddress mac{0x24, 0x6F, 0x28, 0x0A, 0xBC, 0xD1};
    TEST_ASSERT_EQUAL_STRING("esp-0abcd1", espmgr::defaultDeviceName("esp", mac).c_str());
    Device d;
    TEST_ASSERT_EQUAL_STRING("esp-abcdef", d.esp.hostname().c_str());
}

void test_valid_hostnames()
{
    const std::string longest(31, 'h');
    const char* valid[] = {"a", "esp-abcdef", "Kitchen-Lamp-2", "0x1f", longest.c_str()};
    for (const char* name : valid)
    {
        TEST_ASSERT_TRUE_MESSAGE(espmgr::isValidHostname(name), name);
    }
}

void test_invalid_hostnames()
{
    const std::string tooLong(32, 'h');
    const char* invalid[] = {"",        "-lamp",      "lamp-",       "my lamp",
                             "my_lamp", "lamp.local", "caf\xc3\xa9", tooLong.c_str()};
    for (const char* name : invalid)
    {
        TEST_ASSERT_FALSE_MESSAGE(espmgr::isValidHostname(name), name);
    }
}

void test_prefix_changes_the_default_name()
{
    Device d;
    TEST_ASSERT_TRUE(d.esp.setNamePrefix("lamp"));
    TEST_ASSERT_EQUAL_STRING("lamp-abcdef", d.esp.hostname().c_str());
}

void test_invalid_prefixes_are_refused()
{
    Device d;
    TEST_ASSERT_FALSE(d.esp.setNamePrefix(""));
    TEST_ASSERT_FALSE(d.esp.setNamePrefix("-lamp"));
    TEST_ASSERT_FALSE(d.esp.setNamePrefix("my lamp"));
    TEST_ASSERT_FALSE(d.esp.setNamePrefix(std::string(25, 'p')));  // 25 + 7 > 31
    TEST_ASSERT_TRUE(d.esp.setNamePrefix(std::string(24, 'p')));
    TEST_ASSERT_EQUAL(31, d.esp.hostname().size());
}

void test_hostname_overrides_the_default_name()
{
    Device d;
    TEST_ASSERT_TRUE(d.esp.setHostname("kitchen-lamp"));
    TEST_ASSERT_TRUE(d.esp.setNamePrefix("lamp"));
    TEST_ASSERT_EQUAL_STRING("kitchen-lamp", d.esp.hostname().c_str());
}

void test_invalid_hostname_is_refused_and_changes_nothing()
{
    Device d;
    TEST_ASSERT_TRUE(d.esp.setHostname("kitchen-lamp"));
    TEST_ASSERT_FALSE(d.esp.setHostname("kitchen lamp"));
    TEST_ASSERT_FALSE(d.esp.setHostname(std::string(32, 'k')));
    TEST_ASSERT_EQUAL_STRING("kitchen-lamp", d.esp.hostname().c_str());
}

void test_begin_gives_the_driver_the_hostname_before_any_scan()
{
    Device d;
    TEST_ASSERT_TRUE(d.esp.networks().save("Home", "correct horse"));
    TEST_ASSERT_EQUAL_STRING("", d.fakes.wifi.hostname.c_str());
    d.esp.begin();
    TEST_ASSERT_EQUAL_STRING("esp-abcdef", d.fakes.wifi.hostname.c_str());
    TEST_ASSERT_EQUAL(0, d.fakes.wifi.scansStarted);

    d.esp.process();
    d.fakes.wifi.completeScan({{"Home", -50, 6, true}});
    d.esp.process();
    TEST_ASSERT_EQUAL_STRING("esp-abcdef", d.fakes.wifi.connectCalls.at(0).hostname.c_str());
}

void test_hostname_set_before_begin_is_used()
{
    Device d;
    TEST_ASSERT_TRUE(d.esp.setHostname("kitchen-lamp"));
    TEST_ASSERT_EQUAL_STRING("", d.fakes.wifi.hostname.c_str());
    d.esp.begin();
    TEST_ASSERT_EQUAL_STRING("kitchen-lamp", d.fakes.wifi.hostname.c_str());
}

void test_change_after_begin_reaches_the_driver_at_once()
{
    Device d;
    d.esp.begin();
    TEST_ASSERT_TRUE(d.esp.setHostname("kitchen-lamp"));
    TEST_ASSERT_EQUAL_STRING("kitchen-lamp", d.fakes.wifi.hostname.c_str());
    TEST_ASSERT_TRUE(d.esp.setNamePrefix("lamp"));
    TEST_ASSERT_EQUAL_STRING("kitchen-lamp", d.fakes.wifi.hostname.c_str());
}

void test_dhcp_is_the_default_addressing()
{
    Device d;
    d.fakes.wifi.scanStartSucceeds = false;
    TEST_ASSERT_TRUE(d.esp.networks().save("Home", "correct horse"));
    TEST_ASSERT_TRUE(d.esp.networks().list()[0].dhcp);
    d.esp.begin();
    d.esp.process();
    TEST_ASSERT_FALSE(d.fakes.wifi.connectCalls.at(0).staticIp);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_default_name_is_prefix_and_last_six_hex_digits_of_the_mac);
    RUN_TEST(test_valid_hostnames);
    RUN_TEST(test_invalid_hostnames);
    RUN_TEST(test_prefix_changes_the_default_name);
    RUN_TEST(test_invalid_prefixes_are_refused);
    RUN_TEST(test_hostname_overrides_the_default_name);
    RUN_TEST(test_invalid_hostname_is_refused_and_changes_nothing);
    RUN_TEST(test_begin_gives_the_driver_the_hostname_before_any_scan);
    RUN_TEST(test_hostname_set_before_begin_is_used);
    RUN_TEST(test_change_after_begin_reaches_the_driver_at_once);
    RUN_TEST(test_dhcp_is_the_default_addressing);
    return UNITY_END();
}
