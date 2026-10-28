#include <EspManager.h>
#include <FakePlatform.h>
#include <core/Logger.h>
#include <unity.h>

#include <string>

using espmgr::FakeClock;
using espmgr::FakeLogSink;
using espmgr::Logger;
using espmgr::LogLevel;

namespace
{

FakeClock fakeClock;
FakeLogSink sink;

void logOneOfEach(Logger& log)
{
    log.error("t", "error");
    log.warn("t", "warn");
    log.info("t", "info");
    log.debug("t", "debug");
    log.verbose("t", "verbose");
}

}  // namespace

void setUp()
{
    fakeClock = FakeClock();
    sink.lines.clear();
}

void tearDown()
{
}

void test_is_off_by_default()
{
    Logger log(sink, fakeClock);
    TEST_ASSERT_EQUAL(LogLevel::Off, log.level());
    logOneOfEach(log);
    TEST_ASSERT_EQUAL(0, sink.lines.size());
}

void test_writes_the_chosen_level_and_the_ones_above_it()
{
    Logger log(sink, fakeClock);
    log.setLevel(LogLevel::Warn);
    logOneOfEach(log);
    TEST_ASSERT_EQUAL(2, sink.lines.size());
    TEST_ASSERT_EQUAL(LogLevel::Error, sink.lines[0].level);
    TEST_ASSERT_EQUAL(LogLevel::Warn, sink.lines[1].level);
}

void test_verbose_writes_everything()
{
    Logger log(sink, fakeClock);
    log.setLevel(LogLevel::Verbose);
    logOneOfEach(log);
    TEST_ASSERT_EQUAL(5, sink.lines.size());
    TEST_ASSERT_EQUAL(LogLevel::Verbose, sink.lines[4].level);
}

void test_setting_off_again_silences_it()
{
    Logger log(sink, fakeClock);
    log.setLevel(LogLevel::Debug);
    log.setLevel(LogLevel::Off);
    logOneOfEach(log);
    TEST_ASSERT_EQUAL(0, sink.lines.size());
}

void test_enabled_matches_what_is_written()
{
    Logger log(sink, fakeClock);
    log.setLevel(LogLevel::Info);
    TEST_ASSERT_TRUE(log.enabled(LogLevel::Error));
    TEST_ASSERT_TRUE(log.enabled(LogLevel::Info));
    TEST_ASSERT_FALSE(log.enabled(LogLevel::Debug));
    TEST_ASSERT_FALSE(log.enabled(LogLevel::Off));
}

void test_formats_time_level_tag_and_message()
{
    Logger log(sink, fakeClock);
    log.setLevel(LogLevel::Info);
    fakeClock.advance(12345);
    log.warn("wifi", "retry in %u ms", 2000u);
    fakeClock.advance(1000000 - 12345 + 7);
    log.info("portal", "%s", "started");
    TEST_ASSERT_EQUAL_STRING("[espmgr 12.345 W wifi] retry in 2000 ms", sink.lines[0].text.c_str());
    TEST_ASSERT_EQUAL_STRING("[espmgr 1000.007 I portal] started", sink.lines[1].text.c_str());
}

void test_cuts_off_long_lines()
{
    Logger log(sink, fakeClock);
    log.setLevel(LogLevel::Error);
    const std::string longText(400, 'x');
    log.error("t", "%s", longText.c_str());
    TEST_ASSERT_EQUAL(Logger::kMaxLineLength, sink.lines[0].text.size());
    TEST_ASSERT_EQUAL_STRING("[espmgr 0.000 E t] xxx", sink.lines[0].text.substr(0, 22).c_str());
}

void test_espmanager_is_silent_until_a_level_is_set()
{
    espmgr::FakePlatform fakes;
    EspManager quiet(fakes.platform);
    quiet.begin();
    TEST_ASSERT_EQUAL(0, fakes.log.lines.size());

    EspManager chatty(fakes.platform);
    chatty.setLogLevel(LogLevel::Info);
    chatty.begin();
    TEST_ASSERT_FALSE(fakes.log.lines.empty());
    const std::string expected = std::string("EspManager ") + ESPMGR_VERSION + " starting";
    TEST_ASSERT_NOT_EQUAL(std::string::npos, fakes.log.lines[0].text.find(expected));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_is_off_by_default);
    RUN_TEST(test_writes_the_chosen_level_and_the_ones_above_it);
    RUN_TEST(test_verbose_writes_everything);
    RUN_TEST(test_setting_off_again_silences_it);
    RUN_TEST(test_enabled_matches_what_is_written);
    RUN_TEST(test_formats_time_level_tag_and_message);
    RUN_TEST(test_cuts_off_long_lines);
    RUN_TEST(test_espmanager_is_silent_until_a_level_is_set);
    return UNITY_END();
}
