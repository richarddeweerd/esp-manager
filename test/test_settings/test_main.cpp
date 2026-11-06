// Settings (US06.1.1-US06.1.4, US06.2.1): declaring them, validation, what's stored, the change
// event and the Settings Reset.

#include <EspManager.h>
#include <FakePlatform.h>
#include <unity.h>

#include <string>
#include <vector>

using espmgr::CheckboxSetting;
using espmgr::FakePlatform;
using espmgr::LogLevel;
using espmgr::NumberSetting;
using espmgr::PasswordSetting;
using espmgr::SelectSetting;
using espmgr::Setting;
using espmgr::Settings;
using espmgr::SettingsSection;
using espmgr::TextSetting;
using Problem = espmgr::Setting::Problem;

namespace
{

const char* const kNamespace = "espmgr-set";

struct Device
{
    explicit Device(FakePlatform& platform) : fakes(platform)
    {
        esp.setLogLevel(LogLevel::Verbose);
        esp.onSettingChanged([this](const Setting& setting) { changes.push_back(setting.key()); });
    }

    FakePlatform& fakes;
    EspManager esp{fakes.platform};
    std::vector<std::string> changes;

    Settings& settings()
    {
        return esp.settings();
    }

    bool stored(const std::string& key) const
    {
        const auto found = fakes.storage.data.find(kNamespace);
        return found != fakes.storage.data.end() && found->second.count(key) > 0;
    }

    std::string changed()
    {
        esp.process();
        std::string text;
        for (const std::string& key : changes)
        {
            text += (text.empty() ? "" : ",") + key;
        }
        changes.clear();
        return text;
    }
};

}  // namespace

void setUp()
{
}

void tearDown()
{
}

// --- Declaring ---

void test_each_type_starts_at_its_default()
{
    FakePlatform fakes;
    Device d(fakes);
    TextSetting& text = d.settings().addText("host", "Server", "broker.local");
    PasswordSetting& password = d.settings().addPassword("secret", "Password");
    NumberSetting& number = d.settings().addNumber("port", "Port", 1883);
    CheckboxSetting& checkbox = d.settings().addCheckbox("ha", "Discovery", true);
    SelectSetting& select =
        d.settings().addSelect("zone", "Zone", "eu").option("eu", "Europe").option("us", "US");
    d.esp.begin();

    TEST_ASSERT_EQUAL_STRING("broker.local", text.value().c_str());
    TEST_ASSERT_TRUE(password.value().empty());
    TEST_ASSERT_EQUAL_INT32(1883, number.value());
    TEST_ASSERT_TRUE(checkbox.value());
    TEST_ASSERT_EQUAL_STRING("eu", select.value().c_str());
    TEST_ASSERT_EQUAL_UINT(5, d.settings().count());
    TEST_ASSERT_TRUE(text.valid());
}

void test_values_can_be_looked_up_by_key()
{
    FakePlatform fakes;
    Device d(fakes);
    d.settings().addText("host", "Server", "broker.local");
    d.settings().addNumber("port", "Port", 1883);
    d.settings().addCheckbox("ha", "Discovery", true);
    d.esp.begin();

    TEST_ASSERT_EQUAL_STRING("broker.local", d.settings().text("host").c_str());
    TEST_ASSERT_EQUAL_INT32(1883, d.settings().number("port"));
    TEST_ASSERT_TRUE(d.settings().checkbox("ha"));
    TEST_ASSERT_NOT_NULL(d.settings().find("port"));
    TEST_ASSERT_NULL(d.settings().find("nope"));
}

void test_looking_up_a_wrong_key_or_type_gives_an_empty_value_and_a_warning()
{
    FakePlatform fakes;
    Device d(fakes);
    d.settings().addNumber("port", "Port", 1883);
    d.esp.begin();

    TEST_ASSERT_TRUE(d.settings().text("port").empty());
    TEST_ASSERT_EQUAL_INT32(0, d.settings().number("nope"));
    TEST_ASSERT_FALSE(d.settings().checkbox("port"));
    TEST_ASSERT_TRUE(fakes.log.contains("no text Setting port"));
    TEST_ASSERT_TRUE(fakes.log.contains("no number Setting nope"));
}

void test_sections_keep_the_declared_order()
{
    FakePlatform fakes;
    Device d(fakes);
    SettingsSection& mqtt = d.settings().section("MQTT");
    mqtt.addText("host", "Server");
    d.settings().addText("location", "Location");
    mqtt.addNumber("port", "Port");
    d.settings().section("API").addText("api", "API server");

    const auto& sections = d.settings().sections();
    TEST_ASSERT_EQUAL_UINT(3, sections.size());
    TEST_ASSERT_EQUAL_STRING("MQTT", sections[0].title().c_str());
    TEST_ASSERT_TRUE(sections[1].title().empty());
    TEST_ASSERT_EQUAL_STRING("API", sections[2].title().c_str());
    TEST_ASSERT_EQUAL_UINT(2, sections[0].settings().size());
    TEST_ASSERT_EQUAL_STRING("port", sections[0].settings()[1]->key().c_str());
    TEST_ASSERT_EQUAL_STRING("MQTT", d.settings().find("port")->section().c_str());
    TEST_ASSERT_EQUAL_PTR(&mqtt, &d.settings().section("MQTT"));
}

void test_a_duplicate_key_is_refused_and_keeps_its_default()
{
    FakePlatform fakes;
    Device d(fakes);
    TextSetting& first = d.settings().addText("host", "Server", "one");
    TextSetting& second = d.settings().addText("host", "Server again", "two");
    d.esp.begin();

    TEST_ASSERT_TRUE(first.valid());
    TEST_ASSERT_FALSE(second.valid());
    TEST_ASSERT_EQUAL_STRING("two", second.value().c_str());
    TEST_ASSERT_FALSE(second.set("three"));
    TEST_ASSERT_EQUAL_STRING("two", second.value().c_str());
    TEST_ASSERT_EQUAL_UINT(1, d.settings().count());
    TEST_ASSERT_EQUAL_UINT(1, d.settings().sections()[0].settings().size());
    TEST_ASSERT_EQUAL_STRING("one", d.settings().text("host").c_str());
    TEST_ASSERT_TRUE(fakes.log.contains("refused host"));
}

void test_a_bad_key_is_refused()
{
    FakePlatform fakes;
    Device d(fakes);
    TEST_ASSERT_FALSE(d.settings().addText("a_key_of_16_char", "Long").valid());
    TEST_ASSERT_TRUE(d.settings().addText("a_key_of_15_cha", "Fits").valid());
    TEST_ASSERT_FALSE(d.settings().addText("", "Empty").valid());
    TEST_ASSERT_FALSE(d.settings().addText("has space", "Space").valid());
    TEST_ASSERT_FALSE(d.settings().addText("setting.x", "Dot").valid());
    TEST_ASSERT_TRUE(d.settings().addText("Mqtt-Port_2", "Fine").valid());
    TEST_ASSERT_EQUAL_UINT(2, d.settings().count());
}

void test_a_declaration_after_begin_is_refused()
{
    FakePlatform fakes;
    Device d(fakes);
    d.esp.begin();
    NumberSetting& late = d.settings().addNumber("late", "Late", 7);

    TEST_ASSERT_FALSE(late.valid());
    TEST_ASSERT_EQUAL_INT32(7, late.value());
    TEST_ASSERT_EQUAL_UINT(0, d.settings().count());
    TEST_ASSERT_TRUE(fakes.log.contains("declared after begin()"));
}

// --- Validation ---

void test_text_rules()
{
    FakePlatform fakes;
    Device d(fakes);
    TextSetting& text = d.settings().addText("host", "Server");
    TEST_ASSERT_TRUE(text.check(std::string(64, 'x')).ok());
    TEST_ASSERT_EQUAL(Problem::TooLong, text.check(std::string(65, 'x')).problem);
    text.maxLength(4);
    TEST_ASSERT_EQUAL(Problem::TooLong, text.check("12345").problem);
    TEST_ASSERT_TRUE(text.check("1234").ok());
    text.maxLength(512);
    TEST_ASSERT_TRUE(text.check(std::string(512, 'x')).ok());
    text.maxLength(513);
    TEST_ASSERT_EQUAL_UINT(512, text.maxLengthValue());
    text.maxLength(0);
    TEST_ASSERT_EQUAL_UINT(512, text.maxLengthValue());

    TEST_ASSERT_TRUE(text.check("").ok());
    TEST_ASSERT_TRUE(text.check("caf\xc3\xa9 \xe2\x82\xac \xf0\x9f\x8f\xa0").ok());
    for (const std::string& bad :
         {std::string("a\nb"), std::string("a\tb"), std::string("a\x7f"), std::string("a\0b", 3),
          std::string("\xc3"), std::string("\xc3("), std::string("\xc0\x80"),
          std::string("\xed\xa0\x80"), std::string("\xf4\x90\x80\x80"), std::string("\xff")})
    {
        TEST_ASSERT_EQUAL(Problem::InvalidCharacter, text.check(bad).problem);
    }
}

void test_required_refuses_empty_values()
{
    FakePlatform fakes;
    Device d(fakes);
    TextSetting& text = d.settings().addText("host", "Server").required();
    PasswordSetting& password = d.settings().addPassword("secret", "Password").required();
    SelectSetting& select =
        d.settings().addSelect("zone", "Zone").option("eu", "Europe").required();
    d.esp.begin();

    TEST_ASSERT_TRUE(text.isRequired());
    TEST_ASSERT_EQUAL(Problem::Required, text.check("").problem);
    TEST_ASSERT_EQUAL(Problem::Required, password.check("").problem);
    TEST_ASSERT_EQUAL(Problem::Required, select.check("").problem);
    TEST_ASSERT_FALSE(text.set(""));
    TEST_ASSERT_TRUE(text.set("broker"));
}

void test_number_rules()
{
    FakePlatform fakes;
    Device d(fakes);
    NumberSetting& number = d.settings().addNumber("port", "Port", 1883).range(1, 65535);
    TEST_ASSERT_TRUE(number.check(1).ok());
    TEST_ASSERT_TRUE(number.check(65535).ok());
    TEST_ASSERT_EQUAL(Problem::OutOfRange, number.check(0).problem);
    TEST_ASSERT_EQUAL(Problem::OutOfRange, number.check(65536).problem);

    number.range(10, 5);  // ignored
    TEST_ASSERT_EQUAL_INT32(1, number.min());
    TEST_ASSERT_EQUAL_INT32(65535, number.max());

    TEST_ASSERT_TRUE(number.checkText("80").ok());
    for (const char* bad : {"", " 80", "80 ", "+80", "8x", "x", "0x50", "1.5", "-"})
    {
        TEST_ASSERT_EQUAL(Problem::NotANumber, number.checkText(bad).problem);
    }
    TEST_ASSERT_EQUAL(Problem::OutOfRange, number.checkText("99999").problem);
    TEST_ASSERT_EQUAL(Problem::OutOfRange, number.checkText("99999999999999999999999").problem);

    NumberSetting& wide = d.settings().addNumber("wide", "Wide");
    TEST_ASSERT_TRUE(wide.checkText("-2147483648").ok());
    TEST_ASSERT_TRUE(wide.checkText("2147483647").ok());
    TEST_ASSERT_EQUAL(Problem::OutOfRange, wide.checkText("2147483648").problem);
}

void test_checkbox_rules()
{
    FakePlatform fakes;
    Device d(fakes);
    CheckboxSetting& checkbox = d.settings().addCheckbox("ha", "Discovery");
    d.esp.begin();
    TEST_ASSERT_TRUE(checkbox.setText("1"));
    TEST_ASSERT_TRUE(checkbox.value());
    TEST_ASSERT_EQUAL_STRING("1", checkbox.text().c_str());
    TEST_ASSERT_TRUE(checkbox.setText("0"));
    TEST_ASSERT_FALSE(checkbox.value());
    TEST_ASSERT_EQUAL(Problem::NotACheckbox, checkbox.checkText("on").problem);
    TEST_ASSERT_FALSE(checkbox.setText(""));
}

void test_select_rules()
{
    FakePlatform fakes;
    Device d(fakes);
    SelectSetting& select =
        d.settings().addSelect("zone", "Zone").option("eu", "Europe").option("us", "US");
    select.option("eu", "Again").option("", "Empty");  // ignored
    TEST_ASSERT_EQUAL_UINT(2, select.options().size());
    TEST_ASSERT_EQUAL_STRING("Europe", select.options()[0].label.c_str());
    TEST_ASSERT_TRUE(select.check("us").ok());
    TEST_ASSERT_TRUE(select.check("").ok());
    TEST_ASSERT_EQUAL(Problem::UnknownOption, select.check("mars").problem);
}

void test_the_developers_check_gives_its_message()
{
    FakePlatform fakes;
    Device d(fakes);
    int calls = 0;
    TextSetting& text =
        d.settings()
            .addText("host", "Server")
            .validate(
                [&calls](const std::string& value) -> std::string
                {
                    ++calls;
                    return value.find('.') == std::string::npos ? "Enter a full name" : "";
                });
    NumberSetting& number = d.settings()
                                .addNumber("even", "Even")
                                .validate([](const int32_t& value) -> std::string
                                          { return value % 2 ? "Must be even" : ""; });
    d.esp.begin();

    const Setting::Check refused = text.check("broker");
    TEST_ASSERT_EQUAL(Problem::Rejected, refused.problem);
    TEST_ASSERT_EQUAL_STRING("Enter a full name", refused.message.c_str());
    TEST_ASSERT_TRUE(text.check("broker.local").ok());
    TEST_ASSERT_FALSE(text.set("broker"));
    TEST_ASSERT_EQUAL_STRING("Must be even", number.checkText("3").message.c_str());

    // Built-in rules go first, and an empty value doesn't reach it.
    calls = 0;
    TEST_ASSERT_EQUAL(Problem::TooLong, text.check(std::string(65, '.')).problem);
    TEST_ASSERT_TRUE(text.check("").ok());
    TEST_ASSERT_EQUAL_INT(0, calls);
}

// --- Storing ---

void test_only_values_that_differ_from_the_default_are_stored()
{
    FakePlatform fakes;
    Device d(fakes);
    NumberSetting& number = d.settings().addNumber("port", "Port", 1883);
    d.esp.begin();

    TEST_ASSERT_FALSE(d.stored("port"));
    TEST_ASSERT_TRUE(number.set(8883));
    TEST_ASSERT_TRUE(d.stored("port"));
    TEST_ASSERT_TRUE(number.set(1883));
    TEST_ASSERT_FALSE(d.stored("port"));
    TEST_ASSERT_EQUAL_INT32(1883, number.value());
}

void test_values_of_each_type_survive_a_restart()
{
    FakePlatform fakes;
    {
        Device d(fakes);
        TextSetting& text = d.settings().addText("host", "Server", "default");
        PasswordSetting& password = d.settings().addPassword("secret", "Password");
        NumberSetting& number = d.settings().addNumber("temp", "Temperature", 5);
        CheckboxSetting& checkbox = d.settings().addCheckbox("ha", "Discovery", true);
        SelectSetting& select =
            d.settings().addSelect("zone", "Zone", "eu").option("eu", "Europe").option("us", "US");
        d.esp.begin();
        TEST_ASSERT_TRUE(text.set(""));
        TEST_ASSERT_TRUE(password.set("hunter22"));
        TEST_ASSERT_TRUE(number.set(-40));
        TEST_ASSERT_TRUE(checkbox.set(false));
        TEST_ASSERT_TRUE(select.set("us"));
    }
    Device d(fakes);
    TextSetting& text = d.settings().addText("host", "Server", "default");
    PasswordSetting& password = d.settings().addPassword("secret", "Password");
    NumberSetting& number = d.settings().addNumber("temp", "Temperature", 5);
    CheckboxSetting& checkbox = d.settings().addCheckbox("ha", "Discovery", true);
    SelectSetting& select =
        d.settings().addSelect("zone", "Zone", "eu").option("eu", "Europe").option("us", "US");
    d.esp.begin();

    TEST_ASSERT_TRUE(text.value().empty());
    TEST_ASSERT_EQUAL_STRING("hunter22", password.value().c_str());
    TEST_ASSERT_EQUAL_INT32(-40, number.value());
    TEST_ASSERT_FALSE(checkbox.value());
    TEST_ASSERT_EQUAL_STRING("us", select.value().c_str());
    TEST_ASSERT_EQUAL_STRING("", d.changed().c_str());  // loading isn't a change
}

void test_a_new_default_reaches_an_unchanged_setting()
{
    FakePlatform fakes;
    {
        Device d(fakes);
        d.settings().addNumber("port", "Port", 1883);
        d.settings().addNumber("keep", "Keep", 1);
        d.esp.begin();
        TEST_ASSERT_TRUE(d.settings().find("keep")->setText("2"));
    }
    Device d(fakes);
    NumberSetting& port = d.settings().addNumber("port", "Port", 8883);
    NumberSetting& keep = d.settings().addNumber("keep", "Keep", 100);
    d.esp.begin();
    TEST_ASSERT_EQUAL_INT32(8883, port.value());
    TEST_ASSERT_EQUAL_INT32(2, keep.value());
}

void test_a_stored_value_that_no_longer_fits_reads_as_the_default_and_stays()
{
    FakePlatform fakes;
    fakes.storage.putU32(kNamespace, "port", 70000);
    fakes.storage.putString(kNamespace, "zone", "mars");
    Device d(fakes);
    NumberSetting& port = d.settings().addNumber("port", "Port", 1883).range(1, 65535);
    SelectSetting& zone = d.settings().addSelect("zone", "Zone", "eu").option("eu", "Europe");
    d.esp.begin();

    TEST_ASSERT_EQUAL_INT32(1883, port.value());
    TEST_ASSERT_EQUAL_STRING("eu", zone.value().c_str());
    TEST_ASSERT_TRUE(d.stored("port"));
    TEST_ASSERT_TRUE(d.stored("zone"));
    TEST_ASSERT_TRUE(fakes.log.contains("port: using the default"));
    TEST_ASSERT_FALSE(fakes.log.contains("mars"));
    TEST_ASSERT_EQUAL_STRING("", d.changed().c_str());

    // The next save puts it right, also a save of the default.
    TEST_ASSERT_TRUE(port.set(1883));
    TEST_ASSERT_FALSE(d.stored("port"));
    TEST_ASSERT_EQUAL_STRING("", d.changed().c_str());
}

void test_a_stored_value_of_another_type_reads_as_the_default_and_is_replaced()
{
    FakePlatform fakes;
    fakes.storage.putString(kNamespace, "port", "1883");
    Device d(fakes);
    NumberSetting& port = d.settings().addNumber("port", "Port", 5);
    d.esp.begin();

    TEST_ASSERT_EQUAL_INT32(5, port.value());
    TEST_ASSERT_TRUE(fakes.log.contains("stored as another type"));
    TEST_ASSERT_TRUE(port.set(6));
    TEST_ASSERT_EQUAL_UINT32(6, *fakes.storage.getU32(kNamespace, "port"));
}

void test_a_stored_value_the_developers_check_refuses_reads_as_the_default()
{
    FakePlatform fakes;
    fakes.storage.putString(kNamespace, "host", "old");
    Device d(fakes);
    TextSetting& host =
        d.settings()
            .addText("host", "Server", "new.local")
            .validate([](const std::string& value) -> std::string
                      { return value.find('.') == std::string::npos ? "Full name" : ""; });
    d.esp.begin();
    TEST_ASSERT_EQUAL_STRING("new.local", host.value().c_str());
}

void test_a_bad_default_logs_an_error()
{
    FakePlatform fakes;
    Device d(fakes);
    d.settings().addNumber("port", "Port", 0).range(1, 10);
    d.esp.begin();
    TEST_ASSERT_TRUE(fakes.log.contains("port: the default value fails"));
}

void test_a_failed_store_changes_nothing()
{
    FakePlatform fakes;
    Device d(fakes);
    NumberSetting& port = d.settings().addNumber("port", "Port", 1);
    d.esp.begin();
    fakes.storage.writable = false;

    TEST_ASSERT_FALSE(port.set(2));
    TEST_ASSERT_EQUAL_INT32(1, port.value());
    TEST_ASSERT_EQUAL_STRING("", d.changed().c_str());
}

void test_set_before_begin_is_refused()
{
    FakePlatform fakes;
    fakes.storage.putU32(kNamespace, "port", 2);
    Device d(fakes);
    NumberSetting& port = d.settings().addNumber("port", "Port", 1);

    TEST_ASSERT_FALSE(port.set(3));
    d.esp.begin();
    TEST_ASSERT_EQUAL_INT32(2, port.value());
}

// --- Saving several values ---

void test_apply_stores_all_of_them_and_reports_each_change()
{
    FakePlatform fakes;
    Device d(fakes);
    TextSetting& host = d.settings().addText("host", "Server");
    NumberSetting& port = d.settings().addNumber("port", "Port", 1883);
    CheckboxSetting& ha = d.settings().addCheckbox("ha", "Discovery");
    d.esp.begin();

    const auto problems = d.settings().apply({{"host", "broker"}, {"port", "1883"}, {"ha", "1"}});

    TEST_ASSERT_TRUE(problems.empty());
    TEST_ASSERT_EQUAL_STRING("broker", host.value().c_str());
    TEST_ASSERT_EQUAL_INT32(1883, port.value());
    TEST_ASSERT_TRUE(ha.value());
    TEST_ASSERT_EQUAL_STRING("host,ha", d.changed().c_str());  // the port didn't change
}

void test_one_bad_value_stores_nothing_and_reports_every_problem()
{
    FakePlatform fakes;
    Device d(fakes);
    TextSetting& host = d.settings().addText("host", "Server").required();
    NumberSetting& port = d.settings().addNumber("port", "Port", 1883).range(1, 65535);
    d.esp.begin();

    const auto problems =
        d.settings().apply({{"host", "broker"}, {"port", "0"}, {"ha", "1"}, {"zone", "eu"}});

    TEST_ASSERT_EQUAL_UINT(3, problems.size());
    TEST_ASSERT_EQUAL_STRING("port", problems[0].key.c_str());
    TEST_ASSERT_EQUAL(Problem::OutOfRange, problems[0].check.problem);
    TEST_ASSERT_EQUAL(Problem::UnknownSetting, problems[1].check.problem);
    TEST_ASSERT_TRUE(host.value().empty());
    TEST_ASSERT_EQUAL_INT32(1883, port.value());
    TEST_ASSERT_FALSE(d.stored("host"));
    TEST_ASSERT_EQUAL_STRING("", d.changed().c_str());
}

void test_check_changes_nothing()
{
    FakePlatform fakes;
    Device d(fakes);
    TextSetting& host = d.settings().addText("host", "Server");
    d.settings().addNumber("port", "Port").range(1, 9);
    d.esp.begin();

    TEST_ASSERT_TRUE(d.settings().check({{"host", "broker"}}).empty());
    TEST_ASSERT_EQUAL_UINT(1, d.settings().check({{"port", "10"}}).size());
    TEST_ASSERT_TRUE(host.value().empty());
    TEST_ASSERT_EQUAL_STRING("", d.changed().c_str());
}

void test_a_store_failing_halfway_puts_the_earlier_values_back()
{
    FakePlatform fakes;
    Device d(fakes);
    TextSetting& host = d.settings().addText("host", "Server");
    NumberSetting& port = d.settings().addNumber("port", "Port", 1);
    d.esp.begin();
    TEST_ASSERT_TRUE(host.set("old"));
    d.changed();
    fakes.storage.failingKey = "port";

    const auto problems = d.settings().apply({{"host", "new"}, {"port", "2"}});

    TEST_ASSERT_EQUAL_UINT(1, problems.size());
    TEST_ASSERT_EQUAL(Problem::NotStored, problems[0].check.problem);
    TEST_ASSERT_EQUAL_STRING("old", host.value().c_str());
    TEST_ASSERT_EQUAL_STRING("old", fakes.storage.getString(kNamespace, "host")->c_str());
    TEST_ASSERT_EQUAL_INT32(1, port.value());
    TEST_ASSERT_EQUAL_STRING("", d.changed().c_str());
}

void test_apply_before_begin_is_refused()
{
    FakePlatform fakes;
    Device d(fakes);
    TextSetting& host = d.settings().addText("host", "Server");

    const auto problems = d.settings().apply({{"host", "x"}});

    TEST_ASSERT_EQUAL_UINT(1, problems.size());
    TEST_ASSERT_EQUAL(Problem::NotStored, problems[0].check.problem);
    TEST_ASSERT_TRUE(host.value().empty());
}

// --- Complete ---

void test_complete_is_false_while_a_required_setting_is_empty()
{
    FakePlatform fakes;
    Device d(fakes);
    TEST_ASSERT_TRUE(d.settings().complete());
    d.settings().addText("note", "Note");
    TextSetting& host = d.settings().addText("host", "Server").required();
    d.settings().addNumber("port", "Port");
    d.esp.begin();

    TEST_ASSERT_FALSE(d.settings().complete());
    TEST_ASSERT_TRUE(host.set("broker"));
    TEST_ASSERT_TRUE(d.settings().complete());
}

void test_a_required_setting_with_a_default_is_complete()
{
    FakePlatform fakes;
    Device d(fakes);
    d.settings().addText("host", "Server", "broker").required();
    d.esp.begin();
    TEST_ASSERT_TRUE(d.settings().complete());
}

// --- The change event ---

void test_set_reports_from_process_and_an_unchanged_value_reports_nothing()
{
    FakePlatform fakes;
    Device d(fakes);
    NumberSetting& port = d.settings().addNumber("port", "Port", 1);
    d.esp.begin();

    TEST_ASSERT_TRUE(port.set(2));
    TEST_ASSERT_TRUE(d.changes.empty());  // not until process()
    TEST_ASSERT_EQUAL_STRING("port", d.changed().c_str());
    TEST_ASSERT_TRUE(port.set(2));
    TEST_ASSERT_EQUAL_STRING("", d.changed().c_str());
}

void test_a_listener_sees_the_new_value_and_may_set_another()
{
    FakePlatform fakes;
    Device d(fakes);
    NumberSetting& port = d.settings().addNumber("port", "Port", 1);
    TextSetting& note = d.settings().addText("note", "Note");
    d.esp.begin();
    std::string seen;
    d.esp.onSettingChanged(
        [&](const Setting& setting)
        {
            seen += setting.key() + "=" + setting.text() + ";";
            if (setting.key() == "port")
            {
                note.set("port changed");
            }
        });

    TEST_ASSERT_TRUE(port.set(2));
    d.esp.process();

    TEST_ASSERT_EQUAL_STRING("port=2;note=port changed;", seen.c_str());
}

// --- Passwords ---

void test_a_password_is_never_shown_or_logged()
{
    FakePlatform fakes;
    Device d(fakes);
    PasswordSetting& password = d.settings().addPassword("secret", "Password");
    d.esp.begin();
    TEST_ASSERT_TRUE(password.isEmpty());

    TEST_ASSERT_TRUE(password.set("hunter22"));
    d.esp.process();
    TEST_ASSERT_TRUE(d.settings().apply({{"secret", "hunter33"}}).empty());
    d.esp.process();

    TEST_ASSERT_FALSE(password.isEmpty());
    TEST_ASSERT_TRUE(password.text().empty());
    TEST_ASSERT_TRUE(d.settings().find("secret")->text().empty());
    TEST_ASSERT_EQUAL_STRING("hunter33", d.settings().text("secret").c_str());
    TEST_ASSERT_FALSE(fakes.log.contains("hunter"));
}

void test_a_refused_value_is_not_logged()
{
    FakePlatform fakes;
    Device d(fakes);
    d.settings().addPassword("secret", "Password").maxLength(4);
    d.esp.begin();
    TEST_ASSERT_FALSE(d.settings().apply({{"secret", "hunter22"}}).empty());
    TEST_ASSERT_FALSE(fakes.log.contains("hunter"));
}

// --- Management flag ---

void test_editable_in_management_is_off_by_default()
{
    FakePlatform fakes;
    Device d(fakes);
    TEST_ASSERT_FALSE(d.settings().addText("a", "A").isEditableInManagement());
    TEST_ASSERT_TRUE(
        d.settings().addText("b", "B").editableInManagement().isEditableInManagement());
}

// --- Resets ---

void test_settings_reset_returns_to_defaults_and_reports_what_changed()
{
    FakePlatform fakes;
    Device d(fakes);
    NumberSetting& port = d.settings().addNumber("port", "Port", 1);
    TextSetting& host = d.settings().addText("host", "Server", "default");
    d.settings().addCheckbox("ha", "Discovery");
    d.esp.begin();
    TEST_ASSERT_TRUE(d.esp.networks().save("Home", "wifi password"));
    TEST_ASSERT_TRUE(port.set(2));
    TEST_ASSERT_TRUE(host.set("custom"));
    d.changed();

    TEST_ASSERT_TRUE(d.esp.settingsReset());

    TEST_ASSERT_EQUAL_INT32(1, port.value());
    TEST_ASSERT_EQUAL_STRING("default", host.value().c_str());
    TEST_ASSERT_FALSE(d.stored("port"));
    TEST_ASSERT_FALSE(d.stored("host"));
    TEST_ASSERT_EQUAL_STRING("port,host", d.changed().c_str());
    TEST_ASSERT_EQUAL_UINT(1, d.esp.networks().count());
}

void test_a_failed_settings_reset_changes_nothing()
{
    FakePlatform fakes;
    Device d(fakes);
    NumberSetting& port = d.settings().addNumber("port", "Port", 1);
    d.esp.begin();
    TEST_ASSERT_TRUE(port.set(2));
    d.changed();
    fakes.storage.clearable = false;

    TEST_ASSERT_FALSE(d.esp.settingsReset());

    TEST_ASSERT_EQUAL_INT32(2, port.value());
    TEST_ASSERT_EQUAL_STRING("", d.changed().c_str());
}

void test_a_network_reset_keeps_the_settings()
{
    FakePlatform fakes;
    Device d(fakes);
    NumberSetting& port = d.settings().addNumber("port", "Port", 1);
    d.esp.begin();
    TEST_ASSERT_TRUE(port.set(2));

    TEST_ASSERT_TRUE(d.esp.networkReset());

    TEST_ASSERT_EQUAL_INT32(2, port.value());
    TEST_ASSERT_TRUE(d.stored("port"));
}

void test_a_factory_reset_erases_them_without_reporting()
{
    FakePlatform fakes;
    Device d(fakes);
    NumberSetting& port = d.settings().addNumber("port", "Port", 1);
    d.esp.begin();
    TEST_ASSERT_TRUE(port.set(2));
    d.changed();

    TEST_ASSERT_TRUE(d.esp.factoryReset());

    TEST_ASSERT_EQUAL_INT32(1, port.value());
    TEST_ASSERT_FALSE(d.stored("port"));
    TEST_ASSERT_TRUE(d.changes.empty());
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_each_type_starts_at_its_default);
    RUN_TEST(test_values_can_be_looked_up_by_key);
    RUN_TEST(test_looking_up_a_wrong_key_or_type_gives_an_empty_value_and_a_warning);
    RUN_TEST(test_sections_keep_the_declared_order);
    RUN_TEST(test_a_duplicate_key_is_refused_and_keeps_its_default);
    RUN_TEST(test_a_bad_key_is_refused);
    RUN_TEST(test_a_declaration_after_begin_is_refused);
    RUN_TEST(test_text_rules);
    RUN_TEST(test_required_refuses_empty_values);
    RUN_TEST(test_number_rules);
    RUN_TEST(test_checkbox_rules);
    RUN_TEST(test_select_rules);
    RUN_TEST(test_the_developers_check_gives_its_message);
    RUN_TEST(test_only_values_that_differ_from_the_default_are_stored);
    RUN_TEST(test_values_of_each_type_survive_a_restart);
    RUN_TEST(test_a_new_default_reaches_an_unchanged_setting);
    RUN_TEST(test_a_stored_value_that_no_longer_fits_reads_as_the_default_and_stays);
    RUN_TEST(test_a_stored_value_of_another_type_reads_as_the_default_and_is_replaced);
    RUN_TEST(test_a_stored_value_the_developers_check_refuses_reads_as_the_default);
    RUN_TEST(test_a_bad_default_logs_an_error);
    RUN_TEST(test_a_failed_store_changes_nothing);
    RUN_TEST(test_set_before_begin_is_refused);
    RUN_TEST(test_apply_stores_all_of_them_and_reports_each_change);
    RUN_TEST(test_one_bad_value_stores_nothing_and_reports_every_problem);
    RUN_TEST(test_check_changes_nothing);
    RUN_TEST(test_a_store_failing_halfway_puts_the_earlier_values_back);
    RUN_TEST(test_apply_before_begin_is_refused);
    RUN_TEST(test_complete_is_false_while_a_required_setting_is_empty);
    RUN_TEST(test_a_required_setting_with_a_default_is_complete);
    RUN_TEST(test_set_reports_from_process_and_an_unchanged_value_reports_nothing);
    RUN_TEST(test_a_listener_sees_the_new_value_and_may_set_another);
    RUN_TEST(test_a_password_is_never_shown_or_logged);
    RUN_TEST(test_a_refused_value_is_not_logged);
    RUN_TEST(test_editable_in_management_is_off_by_default);
    RUN_TEST(test_settings_reset_returns_to_defaults_and_reports_what_changed);
    RUN_TEST(test_a_failed_settings_reset_changes_nothing);
    RUN_TEST(test_a_network_reset_keeps_the_settings);
    RUN_TEST(test_a_factory_reset_erases_them_without_reporting);
    return UNITY_END();
}
