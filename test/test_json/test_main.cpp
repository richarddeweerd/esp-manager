#include "core/JsonWriter.h"

#include <unity.h>

using espmgr::JsonWriter;

void setUp()
{
}

void tearDown()
{
}

void test_object_with_each_kind_of_value()
{
    JsonWriter json;
    json.beginObject()
        .key("ssid")
        .string("Home")
        .key("rssi")
        .number(-60)
        .key("secured")
        .boolean(true)
        .key("open")
        .boolean(false)
        .endObject();
    TEST_ASSERT_EQUAL_STRING(R"({"ssid":"Home","rssi":-60,"secured":true,"open":false})",
                             json.str().c_str());
}

void test_nested_arrays_and_objects()
{
    JsonWriter json;
    json.beginObject().key("networks").beginArray();
    json.beginObject().key("a").number(1).endObject();
    json.beginObject().key("b").number(2).endObject();
    json.endArray().key("empty").beginArray().endArray().key("scanning").boolean(false);
    json.endObject();
    TEST_ASSERT_EQUAL_STRING(R"({"networks":[{"a":1},{"b":2}],"empty":[],"scanning":false})",
                             json.str().c_str());
}

void test_array_of_plain_values()
{
    JsonWriter json;
    json.beginArray().number(1).string("two").boolean(true).endArray();
    TEST_ASSERT_EQUAL_STRING(R"([1,"two",true])", json.str().c_str());
}

void test_strings_are_escaped()
{
    JsonWriter json;
    json.string(std::string("quote\" back\\ nl\n tab\t cr\r bell\x07 nul") + '\0' + "!");
    TEST_ASSERT_EQUAL_STRING(R"("quote\" back\\ nl\n tab\t cr\r bell\u0007 nul\u0000!")",
                             json.str().c_str());
}

void test_utf8_is_copied_as_it_is()
{
    JsonWriter json;
    json.string("Café ☕");
    TEST_ASSERT_EQUAL_STRING("\"Café ☕\"", json.str().c_str());
}

void test_keys_are_escaped_too()
{
    JsonWriter json;
    json.beginObject().key("a\"b").number(0).endObject();
    TEST_ASSERT_EQUAL_STRING(R"({"a\"b":0})", json.str().c_str());
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_object_with_each_kind_of_value);
    RUN_TEST(test_nested_arrays_and_objects);
    RUN_TEST(test_array_of_plain_values);
    RUN_TEST(test_strings_are_escaped);
    RUN_TEST(test_utf8_is_copied_as_it_is);
    RUN_TEST(test_keys_are_escaped_too);
    return UNITY_END();
}
