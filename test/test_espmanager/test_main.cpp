#include <EspManager.h>
#include <unity.h>

#include <string>

void setUp()
{
}

void tearDown()
{
}

void test_version_string_matches_version_numbers()
{
    const std::string expected = std::to_string(ESPMGR_VERSION_MAJOR) + "." +
                                 std::to_string(ESPMGR_VERSION_MINOR) + "." +
                                 std::to_string(ESPMGR_VERSION_PATCH);
    TEST_ASSERT_EQUAL_STRING(expected.c_str(), ESPMGR_VERSION);
}

void test_begin_and_process_can_be_called_from_global_namespace()
{
    EspManager esp;
    esp.begin();
    esp.process();
    esp.process();
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_version_string_matches_version_numbers);
    RUN_TEST(test_begin_and_process_can_be_called_from_global_namespace);
    return UNITY_END();
}
