// On-device test of firmware updates (#47): a real download over HTTPS into the other OTA slot,
// a real restart into it, confirming, and a real rollback by the bootloader. It runs over four
// boots of the same image; scripts/run-hw-tests.py serves the Manifests and the image (this test's
// own app image, signed with the test-only key in test/keys/) and tells each boot which version
// it is:
//   1. 1.0.0 refuses an image with a bad signature, then installs 2.0.0 and restarts.
//   2. 2.0.0 starts pending, confirms once connected, then installs 3.0.0 and restarts.
//   3. 3.0.0 starts pending with confirmManually() and restarts without confirming, as a firmware
//      that crashes would.
//   4. The bootloader has gone back to 2.0.0, which reports the rollback.
//
// Each boot asks the host:  "ESPMGR-HW OTA <running slot>"
//                     →     "OTA <boot> <version> <server> <expected slot, or ->"
// The summary at the end counts the last boot's tests; run-hw-tests.py also counts failures
// reported by the earlier boots. A failure ends the run in that boot. Needs the min_spiffs.csv
// partition table (the -ota environments), since the HTTPS client doesn't fit the default slots.
#include <Arduino.h>
#include <EspManager.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <unity.h>

#include "../hw/HostLink.h"
#include "TestKeys.h"

#include <cstdio>
#include <string>
#include <vector>

using espmgr::UpdateErrorCode;
using espmgr::UpdateOutcome;
using espmgr::UpdateState;

namespace
{

EspManager esp;

int boot = 0;
std::string version;
std::string server;
std::string expectedSlot;

std::vector<espmgr::UpdateError> failures;
std::vector<std::string> available;
int progressEvents = 0;

std::string runningSlot()
{
    const esp_partition_t* running = esp_ota_get_running_partition();
    return running == nullptr ? "?" : running->label;
}

void idle()
{
    esp.process();
    delay(5);
}

// Asks the host which boot this is. False when the host doesn't serve updates (a plain
// `pio test`), or answers something else.
bool askHost()
{
    const std::string answer =
        hostlink::ask("ESPMGR-HW OTA " + runningSlot(), "OTA ", 10000, [] { delay(5); });
    char versionText[32];
    char serverText[128];
    char slotText[16];
    if (std::sscanf(answer.c_str(), "%d %31s %127s %15s", &boot, versionText, serverText,
                    slotText) != 4)
    {
        return false;
    }
    version = versionText;
    server = serverText;
    expectedSlot = slotText;
    return true;
}

void setUpUpdates(bool manualConfirm)
{
    esp.setFirmwareVersion(version);
    TEST_ASSERT_TRUE(esp.updates().setServer(server));
    esp.updates().setPublicKey(testkeys::kPublicKey).setRootCa(testkeys::kRootCa);
    if (manualConfirm)
    {
        esp.updates().confirmManually();
    }
    esp.onUpdateFailed([](const espmgr::UpdateError& error) { failures.push_back(error); });
    esp.onUpdateAvailable([](const espmgr::UpdateInfo& info)
                          { available.push_back(info.version); });
    esp.onUpdateProgress([](const espmgr::UpdateProgress&) { ++progressEvents; });
}

// Runs process() until `done` or `timeoutMs` pass.
template <typename Done> bool processUntil(Done done, uint32_t timeoutMs)
{
    const uint32_t start = millis();
    while (millis() - start < timeoutMs)
    {
        idle();
        if (done())
        {
            return true;
        }
    }
    return false;
}

// Checks `channel` and waits for the answer: the version found, or "" with the failure counted.
std::string checkChannel(const char* channel)
{
    TEST_ASSERT_TRUE(esp.updates().setChannel(channel));
    const size_t failed = failures.size();
    TEST_ASSERT_TRUE_MESSAGE(esp.updates().check(), "check() didn't start");
    TEST_ASSERT_TRUE_MESSAGE(
        processUntil([] { return esp.updates().state() != UpdateState::Checking; }, 30000),
        "no answer to the check within 30 s");
    // Events are delivered at the end of process(), after the state changed.
    idle();
    if (failures.size() > failed)
    {
        Serial.printf("check failed: %s (HTTP %d)\n", espmgr::toString(failures.back().code),
                      failures.back().httpStatus);
        return "";
    }
    return esp.updates().available().version;
}

// Installs what the check found; doesn't return when the board restarts into it.
void installAndRestart()
{
    Serial.flush();
    TEST_ASSERT_TRUE_MESSAGE(esp.updates().install(), "install() didn't start");
    const size_t failed = failures.size();
    processUntil([&] { return failures.size() > failed; }, 120000);
    if (failures.size() > failed)
    {
        Serial.printf("install failed: %s\n", espmgr::toString(failures.back().code));
        TEST_FAIL_MESSAGE("the install failed");
    }
    TEST_FAIL_MESSAGE("still running 2 minutes after install()");
}

void connect()
{
    // The Saved Network stored in the first boot.
    TEST_ASSERT_TRUE_MESSAGE(esp.waitForConnection(30000), "not connected within 30 s");
}

}  // namespace

void setUp()
{
    hostlink::printWifiEvents();
}

void tearDown()
{
}

// --- Boot 1: 1.0.0, freshly flashed ---

void test_runs_from_the_first_slot()
{
    TEST_ASSERT_FALSE(esp.updates().pending());
    TEST_ASSERT_EQUAL(UpdateOutcome::None, esp.updates().lastResult().outcome);
}

void test_an_image_with_a_bad_signature_is_refused()
{
    connect();
    TEST_ASSERT_EQUAL_STRING("9.0.0", checkChannel("tampered").c_str());
    const size_t failed = failures.size();
    TEST_ASSERT_TRUE(esp.updates().install());
    TEST_ASSERT_TRUE_MESSAGE(processUntil([&] { return failures.size() > failed; }, 120000),
                             "the install didn't end within 2 minutes");
    TEST_ASSERT_EQUAL_STRING("signature-invalid", espmgr::toString(failures.back().code));
    TEST_ASSERT_GREATER_THAN(0, progressEvents);
    TEST_ASSERT_EQUAL(UpdateOutcome::Failed, esp.updates().lastResult().outcome);
    // Still the same firmware next time.
    TEST_ASSERT_EQUAL_STRING(runningSlot().c_str(), esp_ota_get_boot_partition()->label);
}

void test_installs_2_0_0()
{
    TEST_ASSERT_EQUAL_STRING("2.0.0", checkChannel("stable").c_str());
    installAndRestart();
}

void firstBoot()
{
    {
        // A record from an earlier run would confuse the outcome checks.
        Preferences preferences;
        preferences.begin(espmgr::UpdateManager::kNamespace, false);
        preferences.clear();
        preferences.end();
    }
    setUpUpdates(false);
    // Kept in NVS for the later boots.
    esp.networks().save(hostlink::testSsid, hostlink::testPassword);
    esp.begin();
    RUN_TEST(test_runs_from_the_first_slot);
    RUN_TEST(test_an_image_with_a_bad_signature_is_refused);
    if (Unity.TestFailures == 0)
    {
        RUN_TEST(test_installs_2_0_0);
    }
}

// --- Boot 2: 2.0.0, pending ---

void test_new_firmware_starts_pending_in_the_other_slot()
{
    TEST_ASSERT_EQUAL_STRING(expectedSlot.c_str(), runningSlot().c_str());
    TEST_ASSERT_TRUE(esp.updates().pending());
    TEST_ASSERT_EQUAL(UpdateOutcome::Pending, esp.updates().lastResult().outcome);
    TEST_ASSERT_EQUAL_STRING("2.0.0", esp.updates().lastResult().toVersion.c_str());
}

void test_connecting_confirms_it()
{
    connect();
    TEST_ASSERT_TRUE(processUntil([] { return !esp.updates().pending(); }, 5000));
    const espmgr::UpdateRecord& record = esp.updates().lastResult();
    TEST_ASSERT_EQUAL(UpdateOutcome::Succeeded, record.outcome);
    TEST_ASSERT_EQUAL_STRING("1.0.0", record.fromVersion.c_str());
    TEST_ASSERT_EQUAL_STRING("2.0.0", record.toVersion.c_str());
    TEST_ASSERT_EQUAL(espmgr::UpdateMethod::Manual, record.method);
    esp_ota_img_states_t state;
    TEST_ASSERT_EQUAL(ESP_OK, esp_ota_get_state_partition(esp_ota_get_running_partition(), &state));
    TEST_ASSERT_EQUAL(ESP_OTA_IMG_VALID, state);
}

void test_installs_3_0_0()
{
    TEST_ASSERT_EQUAL_STRING("3.0.0", checkChannel("stable").c_str());
    installAndRestart();
}

void secondBoot()
{
    setUpUpdates(false);
    esp.begin();
    RUN_TEST(test_new_firmware_starts_pending_in_the_other_slot);
    RUN_TEST(test_connecting_confirms_it);
    if (Unity.TestFailures == 0)
    {
        RUN_TEST(test_installs_3_0_0);
    }
}

// --- Boot 3: 3.0.0, which never confirms ---

void test_manual_confirm_waits_for_the_sketch()
{
    TEST_ASSERT_EQUAL_STRING(expectedSlot.c_str(), runningSlot().c_str());
    TEST_ASSERT_TRUE(esp.updates().pending());
    connect();
    processUntil([] { return false; }, 1000);
    TEST_ASSERT_TRUE(esp.updates().pending());
    TEST_ASSERT_EQUAL(UpdateOutcome::Pending, esp.updates().lastResult().outcome);
}

// Restarts the way a crash would: no confirmation, no esp.restart().
void test_restarts_without_confirming()
{
    WiFi.disconnect();
    delay(200);
    Serial.flush();
    esp_restart();
}

void thirdBoot()
{
    setUpUpdates(true);
    esp.begin();
    RUN_TEST(test_manual_confirm_waits_for_the_sketch);
    if (Unity.TestFailures == 0)
    {
        RUN_TEST(test_restarts_without_confirming);
    }
}

// --- Boot 4: back on 2.0.0 ---

void test_the_bootloader_rolled_back()
{
    TEST_ASSERT_EQUAL_STRING(expectedSlot.c_str(), runningSlot().c_str());
    TEST_ASSERT_FALSE(esp.updates().pending());
    const espmgr::UpdateRecord& record = esp.updates().lastResult();
    TEST_ASSERT_EQUAL(UpdateOutcome::RolledBack, record.outcome);
    TEST_ASSERT_EQUAL_STRING("3.0.0", record.toVersion.c_str());
    // Raised from the first process().
    idle();
    TEST_ASSERT_EQUAL(1, failures.size());
    TEST_ASSERT_EQUAL(UpdateErrorCode::RolledBack, failures[0].code);
    TEST_ASSERT_EQUAL_STRING("3.0.0", failures[0].version.c_str());
}

void test_nothing_newer_is_offered_after_the_rollback()
{
    connect();
    // The host serves 2.0.0 again, which is what's running.
    TEST_ASSERT_EQUAL_STRING("", checkChannel("stable").c_str());
    TEST_ASSERT_TRUE(available.empty());
}

void fourthBoot()
{
    setUpUpdates(false);
    esp.begin();
    RUN_TEST(test_the_bootloader_rolled_back);
    RUN_TEST(test_nothing_newer_is_offered_after_the_rollback);
}

void test_needs_the_host()
{
    TEST_IGNORE_MESSAGE("no test network or update server (run through scripts/run-hw-tests.py)");
}

void setup()
{
    Serial.begin(115200);
    hostlink::startWifiEventLog();
    // Every boot after the first starts through a restart: esp.restart(), esp_restart() or the
    // bootloader's rollback after one.
    if (esp_reset_reason() == ESP_RST_SW)
    {
        hostlink::waitForHostAfterRestart(10000);
    }
    else
    {
        hostlink::askForNetwork(10000);
    }
    UNITY_BEGIN();
    const bool host = askHost();
    if (!host || (boot == 1 && !hostlink::haveNetwork()))
    {
        RUN_TEST(test_needs_the_host);
    }
    else if (boot == 1)
    {
        firstBoot();
    }
    else if (boot == 2)
    {
        secondBoot();
    }
    else if (boot == 3)
    {
        thirdBoot();
    }
    else
    {
        fourthBoot();
    }
    // Leave the board as the other tests expect it: no Saved Network, not associated.
    esp.networkReset();
    WiFi.disconnect();
    delay(200);
    UNITY_END();
    hostlink::printWifiEvents();
}

void loop()
{
}
