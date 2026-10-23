#include <core/Ipv4Address.h>
#include <unity.h>

using espmgr::Ipv4Address;

void setUp()
{
}

void tearDown()
{
}

void test_parses_dotted_decimal()
{
    const auto ip = Ipv4Address::parse("192.168.1.20");
    TEST_ASSERT_TRUE(ip.has_value());
    TEST_ASSERT_TRUE(*ip == Ipv4Address(192, 168, 1, 20));
    TEST_ASSERT_TRUE(Ipv4Address::parse("0.0.0.0").value().isUnset());
    TEST_ASSERT_TRUE(Ipv4Address::parse("255.255.255.255").value() ==
                     Ipv4Address(255, 255, 255, 255));
}

void test_rejects_malformed_text()
{
    const char* invalid[] = {
        "",           "192.168.1", "192.168.1.20.1", "192.168.1.256", "192.168.01.1",
        "192.168.1.", ".168.1.20", " 192.168.1.20",  "192.168.1.20 ", "a.b.c.d",
        "1234.1.1.1", "192..1.20"};
    for (const char* text : invalid)
    {
        TEST_ASSERT_FALSE_MESSAGE(Ipv4Address::parse(text).has_value(), text);
    }
}

void test_formats_as_dotted_decimal()
{
    TEST_ASSERT_EQUAL_STRING("10.0.0.254", Ipv4Address(10, 0, 0, 254).toString().c_str());
    TEST_ASSERT_EQUAL_STRING("0.0.0.0", Ipv4Address().toString().c_str());
}

void test_round_trips()
{
    const Ipv4Address ip(172, 16, 5, 9);
    TEST_ASSERT_TRUE(Ipv4Address::parse(ip.toString()).value() == ip);
    TEST_ASSERT_TRUE(ip != Ipv4Address());
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_parses_dotted_decimal);
    RUN_TEST(test_rejects_malformed_text);
    RUN_TEST(test_formats_as_dotted_decimal);
    RUN_TEST(test_round_trips);
    return UNITY_END();
}
