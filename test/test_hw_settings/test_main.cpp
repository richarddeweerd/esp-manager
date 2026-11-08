// On-device test of Settings on real NVS, across a real restart (#32). The first boot declares
// Settings, stores values and calls esp.restart(); the second reads them back, then does a
// Settings Reset, which leaves the board clean. It also prints what 32 Settings cost in heap and
// how long reading them takes, for the README.
//
// It needs no network; the handshake only makes sure the host is listening. The summary at the end
// counts the second boot's tests. A failure in the first boot ends the run there, so it can't be
// lost. Run it with scripts/run-hw-tests.py; see test/hw/HostLink.h.

#include <Arduino.h>
#include <EspManager.h>
#include <Preferences.h>
#include <esp_system.h>
#include <unity.h>

#include "../hw/HostLink.h"

#include <cstdio>
#include <string>

using espmgr::CheckboxSetting;
using espmgr::NumberSetting;
using espmgr::PasswordSetting;
using espmgr::SelectSetting;
using espmgr::Settings;
using espmgr::TextSetting;

namespace
{

EspManager esp;

// Both boots declare these the same way.
TextSetting* text;
TextSetting* longText;
NumberSetting* number;
CheckboxSetting* checkbox;
SelectSetting* selection;
PasswordSetting* password;
TextSetting* untouched;
TextSetting* wrongType;
TextSetting* backToDefault;

// The longest key NVS takes, and the longest value a Setting takes, in two-byte characters.
const char* const kLongestKey = "long_text_15chr";

std::string longValue()
{
    std::string value;
    for (int i = 0; i < 256; ++i)
    {
        value += "\xC3\xA9";
    }
    return value;
}

void declareSettings()
{
    auto& settings = esp.settings();
    text = &settings.addText("host", "Host", "default.local");
    longText = &settings.addText(kLongestKey, "Long text").maxLength(512);
    number = &settings.addNumber("port", "Port", 1883);
    checkbox = &settings.addCheckbox("flag", "Flag");
    selection = &settings.addSelect("mode", "Mode", "a").option("a", "A").option("b", "B");
    password = &settings.addPassword("secret", "Secret");
    untouched = &settings.addText("untouched", "Untouched", "as declared");
    wrongType = &settings.addText("wrong_type", "Stored as a number", "fallback");
    backToDefault = &settings.addText("back", "Back to default", "original");
}

template <typename Use> void withStored(Use use)
{
    Preferences preferences;
    TEST_ASSERT_TRUE(preferences.begin(Settings::kNamespace, false));
    use(preferences);
    preferences.end();
}

bool isStored(const char* key)
{
    bool stored = false;
    withStored([&](Preferences& preferences) { stored = preferences.isKey(key); });
    return stored;
}

}  // namespace

void setUp()
{
}

void tearDown()
{
}

// --- First boot ---

// A mix as a sketch might have, with labels too long for std::string to keep inside the object:
// 20 texts holding a stored value of 20 characters, 6 numbers, 4 checkboxes and 2 selects.
void test_measures_32_settings()
{
    char key[8];
    char label[32];
    withStored(
        [&](Preferences& preferences)
        {
            for (int i = 0; i < 20; ++i)
            {
                snprintf(key, sizeof(key), "m%02d", i);
                preferences.putString(key, "a stored value of 20");
            }
        });

    const uint32_t before = ESP.getFreeHeap();
    for (int i = 0; i < 32; ++i)
    {
        snprintf(key, sizeof(key), "m%02d", i);
        snprintf(label, sizeof(label), "The label of Setting %02d", i);
        if (i < 20)
        {
            esp.settings().addText(key, label);
        }
        else if (i < 26)
        {
            esp.settings().addNumber(key, label, 1000).range(0, 65535);
        }
        else if (i < 30)
        {
            esp.settings().addCheckbox(key, label);
        }
        else
        {
            esp.settings().addSelect(key, label, "one").option("one", "One").option("two", "Two");
        }
    }
    const uint32_t declared = ESP.getFreeHeap();

    const uint32_t start = micros();
    esp.settings().load();
    const uint32_t readUs = micros() - start;
    const uint32_t loaded = ESP.getFreeHeap();

    Serial.printf("32 Settings: %u bytes of heap declared, %u bytes with their values, read in "
                  "%u ms\n",
                  static_cast<unsigned>(before - declared), static_cast<unsigned>(before - loaded),
                  static_cast<unsigned>(readUs / 1000));
    TEST_ASSERT_EQUAL_size_t(32, esp.settings().count());
    TEST_ASSERT_EQUAL_STRING("a stored value of 20", esp.settings().text("m00").c_str());
    // The README gives these figures; a big change should be noticed.
    TEST_ASSERT_LESS_THAN_UINT32(12000, before - loaded);
    // begin() returns at once, so reading the Settings must stay short.
    TEST_ASSERT_LESS_THAN_UINT32(100000, readUs);
}

void test_values_are_stored()
{
    TEST_ASSERT_TRUE(text->set("broker.example"));
    TEST_ASSERT_TRUE(longText->set(longValue()));
    TEST_ASSERT_TRUE(number->set(-5));
    TEST_ASSERT_TRUE(checkbox->set(true));
    TEST_ASSERT_TRUE(selection->set("b"));
    TEST_ASSERT_TRUE(password->set("rig-setting-secret"));
    for (const char* key : {"host", kLongestKey, "port", "flag", "mode", "secret"})
    {
        TEST_ASSERT_TRUE_MESSAGE(isStored(key), key);
    }
    TEST_ASSERT_FALSE(isStored("untouched"));
}

// Real NVS keeps types apart, and won't overwrite a key with a value of another type.
void test_a_value_of_another_type_is_replaced()
{
    TEST_ASSERT_EQUAL_STRING("fallback", wrongType->value().c_str());
    TEST_ASSERT_TRUE(wrongType->set("replaced"));
    TEST_ASSERT_EQUAL_STRING("replaced", wrongType->value().c_str());
}

void test_saving_the_default_removes_the_stored_value()
{
    TEST_ASSERT_EQUAL_STRING("no longer the default", backToDefault->value().c_str());
    TEST_ASSERT_TRUE(backToDefault->set("original"));
    TEST_ASSERT_FALSE(isStored("back"));
}

// Doesn't return when it works; a test that fails here is reported in place of the second boot.
void test_restart_restarts_the_board()
{
    Serial.flush();
    esp.restart();
    const uint32_t start = millis();
    while (millis() - start < 5000)
    {
        esp.process();
        delay(5);
    }
    TEST_FAIL_MESSAGE("still running 5 s after esp.restart()");
}

void firstBoot()
{
    withStored(
        [](Preferences& preferences)
        {
            preferences.clear();
            preferences.putInt("wrong_type", 1234);
            preferences.putString("back", "no longer the default");
        });

    UNITY_BEGIN();
    RUN_TEST(test_measures_32_settings);
    declareSettings();
    esp.begin();
    RUN_TEST(test_values_are_stored);
    RUN_TEST(test_a_value_of_another_type_is_replaced);
    RUN_TEST(test_saving_the_default_removes_the_stored_value);
    if (Unity.TestFailures == 0)
    {
        RUN_TEST(test_restart_restarts_the_board);
    }
    UNITY_END();
}

// --- Second boot ---

void test_values_survive_the_restart()
{
    TEST_ASSERT_EQUAL_STRING("broker.example", text->value().c_str());
    TEST_ASSERT_TRUE(longText->value() == longValue());
    TEST_ASSERT_EQUAL_INT32(-5, number->value());
    TEST_ASSERT_TRUE(checkbox->value());
    TEST_ASSERT_EQUAL_STRING("b", selection->value().c_str());
    TEST_ASSERT_EQUAL_STRING("rig-setting-secret", password->value().c_str());
    TEST_ASSERT_EQUAL_STRING("replaced", wrongType->value().c_str());
}

void test_defaults_stay_defaults_after_the_restart()
{
    TEST_ASSERT_EQUAL_STRING("as declared", untouched->value().c_str());
    TEST_ASSERT_EQUAL_STRING("original", backToDefault->value().c_str());
}

void test_settings_reset_clears_the_stored_values()
{
    TEST_ASSERT_TRUE(esp.settingsReset());
    TEST_ASSERT_EQUAL_STRING("default.local", text->value().c_str());
    TEST_ASSERT_EQUAL_INT32(1883, number->value());
    TEST_ASSERT_FALSE(checkbox->value());
    TEST_ASSERT_TRUE(password->value().empty());
    // These aren't declared in this boot, and go too: the whole namespace is cleared.
    for (const char* key : {"host", kLongestKey, "secret", "m00"})
    {
        TEST_ASSERT_FALSE_MESSAGE(isStored(key), key);
    }
}

void secondBoot()
{
    hostlink::waitForHostAfterRestart(10000);
    declareSettings();
    esp.begin();

    UNITY_BEGIN();
    RUN_TEST(test_values_survive_the_restart);
    RUN_TEST(test_defaults_stay_defaults_after_the_restart);
    RUN_TEST(test_settings_reset_clears_the_stored_values);
    UNITY_END();
}

void setup()
{
    Serial.begin(115200);
    // Only esp.restart() gives this reason: flashing and the host's reset don't.
    if (esp_reset_reason() == ESP_RST_SW)
    {
        secondBoot();
    }
    else
    {
        hostlink::askForNetwork(10000);
        firstBoot();
    }
}

void loop()
{
}
