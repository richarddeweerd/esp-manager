// Firmware updates (EP17): checking, installing, policy, failures, events, confirming, rollback
// and the history record.

#include <EspManager.h>
#include <FakePlatform.h>
#include <unity.h>

#include <string>
#include <vector>

using espmgr::ConnectionState;
using espmgr::FakePlatform;
using espmgr::InstallPolicy;
using espmgr::LogLevel;
using espmgr::PortalState;
using espmgr::UpdateDriver;
using espmgr::UpdateError;
using espmgr::UpdateErrorCode;
using espmgr::UpdateInfo;
using espmgr::UpdateManager;
using espmgr::UpdateMethod;
using espmgr::UpdateOutcome;
using espmgr::UpdateProgress;
using espmgr::UpdateState;

namespace
{

constexpr const char* kServer = "https://updates.example.com/lamp/";
constexpr const char* kKey = "-----BEGIN PUBLIC KEY-----\nTEST\n-----END PUBLIC KEY-----\n";
constexpr size_t kSize = 1000;

UpdateDriver::Sha256 imageHash()
{
    UpdateDriver::Sha256 hash{};
    for (size_t i = 0; i < hash.size(); ++i)
    {
        hash[i] = static_cast<uint8_t>(i);
    }
    return hash;
}

const char* kHashHex = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";

std::string manifest(const std::string& version, const std::string& extra = "")
{
    return R"({"version":")" + version + R"(","url":"fw.bin","size":)" + std::to_string(kSize) +
           R"(,"sha256":")" + kHashHex + R"(","signature":"MEUCIQ==","notes":"Better")" + extra +
           "}";
}

// An EspManager on fakes with a Saved Network, running firmware 1.0.0 with updates set up.
struct Device
{
    FakePlatform fakes;
    EspManager esp{fakes.platform};
    std::vector<std::string> events;
    std::vector<UpdateError> failures;
    std::vector<uint8_t> percents;

    explicit Device(bool setUp = true)
    {
        fakes.wifi.scanStartSucceeds = false;
        esp.setLogLevel(LogLevel::Debug);
        esp.setFirmwareVersion("1.0.0");
        TEST_ASSERT_TRUE(esp.networks().save("Home", "correct horse"));
        esp.onUpdateAvailable([this](const UpdateInfo& i)
                              { events.push_back("available " + i.version); })
            .onUpdateStarted([this](const UpdateInfo& i)
                             { events.push_back("started " + i.version); })
            .onUpdateProgress([this](const UpdateProgress& p) { percents.push_back(p.percent); })
            .onUpdateCompleted([this](const UpdateInfo& i)
                               { events.push_back("completed " + i.version); })
            .onUpdateFailed(
                [this](const UpdateError& e)
                {
                    failures.push_back(e);
                    events.push_back(std::string("failed ") + espmgr::toString(e.code));
                });
        if (setUp)
        {
            TEST_ASSERT_TRUE(esp.updates().setServer(kServer));
            esp.updates().setPublicKey(kKey);
        }
    }

    UpdateManager& updates()
    {
        return esp.updates();
    }

    void connect()
    {
        esp.begin();
        esp.process();
        fakes.wifi.completeConnect(11);
        esp.process();
        TEST_ASSERT_EQUAL(ConnectionState::Connected, esp.state());
    }

    void advance(uint32_t ms)
    {
        fakes.clock.advance(ms);
        esp.process();
    }

    // Checks and answers with `json`.
    void checkWith(const std::string& json)
    {
        TEST_ASSERT_TRUE(updates().check());
        esp.process();
        fakes.updates.finishFetch(json);
        esp.process();
    }

    // Checks, finds 2.0.0 and installs it until the download is done and verified.
    void installNewVersion()
    {
        checkWith(manifest("2.0.0"));
        TEST_ASSERT_EQUAL(UpdateState::Available, updates().state());
        TEST_ASSERT_TRUE(updates().install());
        esp.process();
        fakes.updates.finishDownload(kSize, imageHash());
        esp.process();  // download done: verifying
        esp.process();  // verified and installed
    }
};

}  // namespace

void setUp()
{
}

void tearDown()
{
}

void test_updates_need_a_server_and_a_connection()
{
    Device d(false);
    TEST_ASSERT_FALSE(d.updates().configured());
    TEST_ASSERT_FALSE(d.updates().check());  // before begin()
    d.connect();
    TEST_ASSERT_FALSE(d.updates().check());  // no server
    TEST_ASSERT_TRUE(d.fakes.log.contains("no Update Server set"));
    TEST_ASSERT_TRUE(d.updates().setServer(kServer));
    TEST_ASSERT_TRUE(d.updates().configured());
    d.fakes.wifi.loseConnection();
    d.esp.process();
    TEST_ASSERT_FALSE(d.updates().check());
    TEST_ASSERT_EQUAL(0, d.fakes.updates.fetches);
}

void test_server_and_channel_are_checked()
{
    Device d(false);
    UpdateManager& u = d.updates();
    TEST_ASSERT_FALSE(u.setServer("http://updates.example.com/"));
    TEST_ASSERT_FALSE(u.setServer("https://"));
    TEST_ASSERT_FALSE(u.setServer("https://example.com/a b/"));
    TEST_ASSERT_FALSE(u.setServer("https://example.com/" + std::string(250, 'a')));
    TEST_ASSERT_EQUAL_STRING("", u.manifestUrl().c_str());
    TEST_ASSERT_TRUE(u.setServer("https://example.com/lamp"));
    TEST_ASSERT_EQUAL_STRING("https://example.com/lamp/stable.json", u.manifestUrl().c_str());
    TEST_ASSERT_TRUE(u.setChannel("beta_2-x"));
    TEST_ASSERT_EQUAL_STRING("https://example.com/lamp/beta_2-x.json", u.manifestUrl().c_str());
    TEST_ASSERT_FALSE(u.setChannel(""));
    TEST_ASSERT_FALSE(u.setChannel("be/ta"));
    TEST_ASSERT_FALSE(u.setChannel(std::string(33, 'a')));
    TEST_ASSERT_EQUAL_STRING("beta_2-x", u.channel().c_str());
}

void test_a_check_fetches_the_manifest_with_headers_and_root_ca()
{
    Device d;
    static const char* ca = "-----BEGIN CERTIFICATE-----\nCA\n-----END CERTIFICATE-----\n";
    d.updates().setRootCa(ca);
    TEST_ASSERT_TRUE(d.updates().addHeader("Authorization", "Bearer s3cret-token"));
    d.connect();
    TEST_ASSERT_TRUE(d.updates().check());
    TEST_ASSERT_EQUAL(UpdateState::Checking, d.updates().state());
    TEST_ASSERT_FALSE(d.updates().check());  // busy
    TEST_ASSERT_EQUAL(1, d.fakes.updates.fetches);
    const espmgr::UpdateRequest& request = d.fakes.updates.requests.back();
    TEST_ASSERT_EQUAL_STRING("https://updates.example.com/lamp/stable.json", request.url.c_str());
    TEST_ASSERT_EQUAL(1, request.headers.size());
    TEST_ASSERT_EQUAL_STRING("Authorization", request.headers[0].first.c_str());
    TEST_ASSERT_EQUAL_STRING("Bearer s3cret-token", request.headers[0].second.c_str());
    TEST_ASSERT_EQUAL_PTR(ca, request.rootCa);
    TEST_ASSERT_EQUAL(espmgr::Manifest::kMaxSize, d.fakes.updates.fetchMax);
    TEST_ASSERT_FALSE(d.fakes.log.contains("s3cret"));
}

void test_headers_are_limited_and_never_logged()
{
    Device d;
    for (int i = 0; i < 4; ++i)
    {
        TEST_ASSERT_TRUE(d.updates().addHeader("X-" + std::to_string(i), "v"));
    }
    TEST_ASSERT_FALSE(d.updates().addHeader("X-5", "hidden-value"));
    TEST_ASSERT_FALSE(d.fakes.log.contains("hidden-value"));
    Device e;
    TEST_ASSERT_FALSE(e.updates().addHeader("", "v"));
    TEST_ASSERT_FALSE(e.updates().addHeader("Bad Name", "v"));
    TEST_ASSERT_FALSE(e.updates().addHeader("X-A", "line\r\nX-B: injected"));
}

void test_a_newer_version_is_available_and_announced_once()
{
    Device d;
    d.connect();
    d.checkWith(manifest("2.0.0"));
    TEST_ASSERT_EQUAL(UpdateState::Available, d.updates().state());
    TEST_ASSERT_EQUAL_STRING("2.0.0", d.updates().available().version.c_str());
    TEST_ASSERT_EQUAL_STRING("Better", d.updates().available().notes.c_str());
    TEST_ASSERT_EQUAL(kSize, d.updates().available().size);
    d.checkWith(manifest("2.0.0"));
    TEST_ASSERT_EQUAL(1, d.events.size());
    TEST_ASSERT_EQUAL_STRING("available 2.0.0", d.events[0].c_str());
    d.checkWith(manifest("2.1.0"));
    TEST_ASSERT_EQUAL_STRING("available 2.1.0", d.events.back().c_str());
}

void test_the_same_or_an_older_version_is_no_update()
{
    Device d;
    d.connect();
    d.checkWith(manifest("1.0.0"));
    TEST_ASSERT_EQUAL(UpdateState::Idle, d.updates().state());
    d.checkWith(manifest("0.9.0"));
    d.checkWith(manifest("1.0.0-rc.1"));
    TEST_ASSERT_EQUAL(UpdateState::Idle, d.updates().state());
    TEST_ASSERT_TRUE(d.events.empty());
    TEST_ASSERT_TRUE(d.fakes.log.contains("up to date"));
}

void test_a_failed_check_raises_the_error_and_keeps_what_was_available()
{
    Device d;
    d.connect();
    TEST_ASSERT_TRUE(d.updates().check());
    d.esp.process();
    d.fakes.updates.fail(UpdateErrorCode::ServerUnreachable);
    d.esp.process();
    TEST_ASSERT_EQUAL(UpdateState::Idle, d.updates().state());
    TEST_ASSERT_EQUAL(1, d.failures.size());
    TEST_ASSERT_EQUAL(UpdateErrorCode::ServerUnreachable, d.failures[0].code);
    TEST_ASSERT_EQUAL(UpdateErrorCode::ServerUnreachable, d.updates().lastError().code);

    d.checkWith(manifest("2.0.0"));
    TEST_ASSERT_TRUE(d.updates().check());
    d.esp.process();
    d.fakes.updates.fail(UpdateErrorCode::HttpStatus, 404);
    d.esp.process();
    TEST_ASSERT_EQUAL(UpdateErrorCode::HttpStatus, d.failures.back().code);
    TEST_ASSERT_EQUAL(404, d.failures.back().httpStatus);
    TEST_ASSERT_EQUAL(UpdateState::Available, d.updates().state());
    TEST_ASSERT_EQUAL_STRING("2.0.0", d.updates().available().version.c_str());
}

void test_invalid_manifests_and_versions_are_errors()
{
    Device d;
    d.connect();
    d.checkWith("{not json");
    TEST_ASSERT_EQUAL(UpdateErrorCode::ManifestInvalid, d.failures.back().code);
    // With a Public Key, the signature is required.
    d.checkWith(R"({"version":"2.0.0","url":"fw.bin","size":10,"sha256":")" +
                std::string(kHashHex) + R"("})");
    TEST_ASSERT_EQUAL(UpdateErrorCode::ManifestInvalid, d.failures.back().code);
    d.checkWith(manifest("2.0"));
    TEST_ASSERT_EQUAL(UpdateErrorCode::VersionInvalid, d.failures.back().code);
    d.checkWith(manifest("2.0.0", R"(,"minimumVersion":"one")"));
    TEST_ASSERT_EQUAL(UpdateErrorCode::VersionInvalid, d.failures.back().code);
    // An image anywhere but https:// is refused.
    d.checkWith(R"({"version":"2.0.0","url":"http://evil/fw.bin","size":10,"sha256":")" +
                std::string(kHashHex) + R"(","signature":"MA=="})");
    TEST_ASSERT_EQUAL(UpdateErrorCode::ManifestInvalid, d.failures.back().code);
    TEST_ASSERT_EQUAL(UpdateState::Idle, d.updates().state());

    d.esp.setFirmwareVersion("v1");
    d.checkWith(manifest("2.0.0"));
    TEST_ASSERT_EQUAL(UpdateErrorCode::VersionInvalid, d.failures.back().code);
    TEST_ASSERT_TRUE(d.fakes.log.contains("isn't a SemVer version"));
}

void test_a_device_below_the_minimum_version_is_too_old()
{
    Device d;
    d.connect();
    d.checkWith(manifest("3.0.0", R"(,"minimumVersion":"1.2.0")"));
    TEST_ASSERT_EQUAL(UpdateErrorCode::TooOld, d.failures.back().code);
    TEST_ASSERT_EQUAL_STRING("3.0.0", d.failures.back().version.c_str());
    TEST_ASSERT_EQUAL(UpdateState::Idle, d.updates().state());
    d.checkWith(manifest("3.0.0", R"(,"minimumVersion":"1.0.0")"));
    TEST_ASSERT_EQUAL(UpdateState::Available, d.updates().state());
}

void test_an_install_downloads_verifies_activates_and_restarts()
{
    Device d;
    d.connect();
    d.checkWith(manifest("2.0.0"));
    TEST_ASSERT_TRUE(d.updates().install());
    TEST_ASSERT_EQUAL(UpdateState::Downloading, d.updates().state());
    TEST_ASSERT_EQUAL(1, d.fakes.updates.downloads);
    TEST_ASSERT_EQUAL_STRING("https://updates.example.com/lamp/fw.bin",
                             d.fakes.updates.requests.back().url.c_str());
    TEST_ASSERT_EQUAL(kSize, d.fakes.updates.downloadSize);
    TEST_ASSERT_FALSE(d.updates().install());
    TEST_ASSERT_FALSE(d.updates().check());

    // Progress per whole percent, at most every 250 ms.
    d.fakes.updates.receive(100);
    d.advance(300);
    d.fakes.updates.receive(200);
    d.advance(100);
    d.fakes.updates.receive(300);
    d.advance(200);
    TEST_ASSERT_EQUAL(30, d.updates().progress());
    d.fakes.updates.finishDownload(kSize, imageHash());
    d.esp.process();
    TEST_ASSERT_EQUAL(UpdateState::Verifying, d.updates().state());
    d.esp.process();
    TEST_ASSERT_EQUAL(2, d.percents.size() - 1);
    TEST_ASSERT_EQUAL_UINT8(10, d.percents[0]);
    TEST_ASSERT_EQUAL_UINT8(30, d.percents[1]);
    TEST_ASSERT_EQUAL_UINT8(100, d.percents[2]);

    TEST_ASSERT_EQUAL(1, d.fakes.updates.verifications);
    TEST_ASSERT_EQUAL_STRING(kKey, d.fakes.updates.verifiedKey.c_str());
    // "MEUCIQ==" decoded.
    TEST_ASSERT_EQUAL(4, d.fakes.updates.verifiedSignature.size());
    TEST_ASSERT_EQUAL(1, d.fakes.updates.activations);
    TEST_ASSERT_EQUAL(UpdateState::Restarting, d.updates().state());
    const std::vector<std::string> expected = {"available 2.0.0", "started 2.0.0",
                                               "completed 2.0.0"};
    TEST_ASSERT_EQUAL(expected.size(), d.events.size());
    for (size_t i = 0; i < expected.size(); ++i)
    {
        TEST_ASSERT_EQUAL_STRING(expected[i].c_str(), d.events[i].c_str());
    }
    const espmgr::UpdateRecord& record = d.updates().lastResult();
    TEST_ASSERT_EQUAL(UpdateOutcome::Pending, record.outcome);
    TEST_ASSERT_EQUAL_STRING("1.0.0", record.fromVersion.c_str());
    TEST_ASSERT_EQUAL_STRING("2.0.0", record.toVersion.c_str());
    TEST_ASSERT_EQUAL(UpdateMethod::Manual, record.method);

    // 2 s, then the usual restart delay.
    d.advance(1900);
    d.advance(200);
    TEST_ASSERT_EQUAL(0, d.fakes.system.restarts);
    d.advance(600);
    TEST_ASSERT_EQUAL(1, d.fakes.system.restarts);
}

void test_install_without_a_public_key_is_refused_unless_allowed()
{
    Device d(false);
    TEST_ASSERT_TRUE(d.updates().setServer(kServer));
    d.connect();
    d.checkWith(manifest("2.0.0"));
    TEST_ASSERT_FALSE(d.updates().install());
    d.esp.process();
    TEST_ASSERT_EQUAL(UpdateErrorCode::Unsigned, d.failures.back().code);
    TEST_ASSERT_EQUAL(UpdateOutcome::Failed, d.updates().lastResult().outcome);
    TEST_ASSERT_EQUAL(UpdateErrorCode::Unsigned, d.updates().lastResult().error);
    TEST_ASSERT_EQUAL(UpdateState::Available, d.updates().state());
    TEST_ASSERT_EQUAL(0, d.fakes.updates.downloads);

    d.updates().allowUnsigned();
    TEST_ASSERT_TRUE(d.updates().install());
    d.esp.process();
    d.fakes.updates.finishDownload(kSize, imageHash());
    d.esp.process();
    d.esp.process();
    TEST_ASSERT_EQUAL(0, d.fakes.updates.verifications);
    TEST_ASSERT_EQUAL(1, d.fakes.updates.activations);
    TEST_ASSERT_TRUE(d.fakes.log.contains("installing an unsigned image"));
}

void test_install_checks_space_memory_and_partitions_first()
{
    Device d;
    d.connect();
    d.checkWith(manifest("2.0.0"));
    d.fakes.updates.slot = 0;
    TEST_ASSERT_FALSE(d.updates().install());
    d.esp.process();
    TEST_ASSERT_EQUAL(UpdateErrorCode::NoOtaPartition, d.failures.back().code);
    d.fakes.updates.slot = kSize - 1;
    TEST_ASSERT_FALSE(d.updates().install());
    d.esp.process();
    TEST_ASSERT_EQUAL(UpdateErrorCode::NoSpace, d.failures.back().code);
    d.fakes.updates.slot = kSize;
    d.fakes.system.heap = 30000;
    TEST_ASSERT_FALSE(d.updates().install());
    d.esp.process();
    TEST_ASSERT_EQUAL(UpdateErrorCode::LowMemory, d.failures.back().code);
    TEST_ASSERT_EQUAL(0, d.fakes.updates.downloads);
    d.fakes.system.heap = 60000;
    TEST_ASSERT_TRUE(d.updates().install());
}

void test_a_wrong_size_hash_or_signature_is_rejected()
{
    struct Case
    {
        size_t bytes;
        bool hashMatches;
        bool signatureValid;
        UpdateErrorCode code;
    };
    const Case cases[] = {{kSize - 1, true, true, UpdateErrorCode::SizeMismatch},
                          {kSize, false, true, UpdateErrorCode::HashMismatch},
                          {kSize, true, false, UpdateErrorCode::SignatureInvalid}};
    for (const Case& c : cases)
    {
        Device d;
        d.connect();
        d.fakes.updates.signatureValid = c.signatureValid;
        d.checkWith(manifest("2.0.0"));
        TEST_ASSERT_TRUE(d.updates().install());
        d.esp.process();
        UpdateDriver::Sha256 hash = imageHash();
        if (!c.hashMatches)
        {
            hash[0] = 0xff;
        }
        d.fakes.updates.finishDownload(c.bytes, hash);
        d.esp.process();
        d.esp.process();
        TEST_ASSERT_EQUAL(c.code, d.failures.back().code);
        TEST_ASSERT_EQUAL(0, d.fakes.updates.activations);
        TEST_ASSERT_EQUAL(UpdateState::Available, d.updates().state());
        TEST_ASSERT_EQUAL(UpdateOutcome::Failed, d.updates().lastResult().outcome);
        TEST_ASSERT_EQUAL(c.code, d.updates().lastResult().error);
    }
}

void test_a_bad_signature_encoding_or_activation_failure_is_an_error()
{
    Device d;
    d.connect();
    d.checkWith(R"({"version":"2.0.0","url":"fw.bin","size":1000,"sha256":")" +
                std::string(kHashHex) + R"(","signature":"not*base64"})");
    TEST_ASSERT_TRUE(d.updates().install());
    d.esp.process();
    d.fakes.updates.finishDownload(kSize, imageHash());
    d.esp.process();
    d.esp.process();
    TEST_ASSERT_EQUAL(UpdateErrorCode::SignatureInvalid, d.failures.back().code);

    Device e;
    e.connect();
    e.fakes.updates.activateSucceeds = false;
    e.installNewVersion();
    TEST_ASSERT_EQUAL(UpdateErrorCode::WriteFailed, e.failures.back().code);
}

void test_a_failed_or_interrupted_download_keeps_the_firmware()
{
    Device d;
    d.connect();
    d.checkWith(manifest("2.0.0"));
    TEST_ASSERT_TRUE(d.updates().install());
    d.esp.process();
    d.fakes.updates.fail(UpdateErrorCode::DownloadFailed);
    d.esp.process();
    TEST_ASSERT_EQUAL(UpdateErrorCode::DownloadFailed, d.failures.back().code);
    TEST_ASSERT_EQUAL(UpdateState::Available, d.updates().state());

    TEST_ASSERT_TRUE(d.updates().install());
    d.esp.process();
    d.fakes.wifi.loseConnection();
    d.esp.process();
    TEST_ASSERT_EQUAL(1, d.fakes.updates.aborts);
    TEST_ASSERT_EQUAL(UpdateErrorCode::DownloadFailed, d.failures.back().code);
    TEST_ASSERT_EQUAL(0, d.fakes.updates.activations);
    TEST_ASSERT_TRUE(d.fakes.log.contains("connection was lost"));
}

void test_automatic_checks_follow_the_connection_and_interval()
{
    Device d;
    d.updates().setAutoCheck(true);
    d.fakes.clock.randomValues.push_back(5000);
    d.connect();
    d.advance(64000);
    TEST_ASSERT_EQUAL(0, d.fakes.updates.fetches);
    d.advance(1000);
    TEST_ASSERT_EQUAL(1, d.fakes.updates.fetches);
    d.fakes.updates.finishFetch(manifest("1.0.0"));
    d.esp.process();
    d.advance(UpdateManager::kDefaultCheckIntervalMs - 2000);
    TEST_ASSERT_EQUAL(1, d.fakes.updates.fetches);
    d.advance(3000);
    TEST_ASSERT_EQUAL(2, d.fakes.updates.fetches);
}

void test_automatic_checks_are_off_by_default_and_wait_for_the_portal()
{
    Device d;
    d.connect();
    d.advance(UpdateManager::kDefaultCheckIntervalMs + 100000);
    TEST_ASSERT_EQUAL(0, d.fakes.updates.fetches);

    Device e;
    e.updates().setAutoCheck(true);
    e.connect();
    e.esp.startPortal();
    e.esp.process();
    TEST_ASSERT_EQUAL(PortalState::Active, e.esp.portalState());
    e.advance(200000);
    TEST_ASSERT_EQUAL(0, e.fakes.updates.fetches);
    e.esp.stopPortal();
    e.esp.process();
    e.advance(10);
    TEST_ASSERT_EQUAL(1, e.fakes.updates.fetches);
}

void test_the_check_interval_has_a_minimum()
{
    Device d;
    TEST_ASSERT_FALSE(d.updates().setCheckInterval(60000));
    TEST_ASSERT_EQUAL(UpdateManager::kDefaultCheckIntervalMs, d.updates().checkInterval());
    TEST_ASSERT_TRUE(d.updates().setCheckInterval(UpdateManager::kMinCheckIntervalMs));
}

void test_the_install_policy_installs_when_the_condition_allows()
{
    Device d;
    bool allowed = false;
    d.updates()
        .setInstallPolicy(InstallPolicy::Install)
        .setInstallCondition([&] { return allowed; });
    d.connect();
    d.checkWith(manifest("2.0.0"));
    d.advance(1000);
    TEST_ASSERT_EQUAL(UpdateState::Available, d.updates().state());
    TEST_ASSERT_EQUAL(0, d.fakes.updates.downloads);
    allowed = true;
    d.esp.process();
    TEST_ASSERT_EQUAL(UpdateState::Downloading, d.updates().state());
    d.fakes.updates.fail(UpdateErrorCode::DownloadFailed);
    d.esp.process();
    TEST_ASSERT_EQUAL(UpdateMethod::Automatic, d.updates().lastResult().method);
    // Not tried again until the next check finds it.
    d.advance(1000);
    TEST_ASSERT_EQUAL(1, d.fakes.updates.downloads);
    d.checkWith(manifest("2.0.0"));
    d.esp.process();
    TEST_ASSERT_EQUAL(2, d.fakes.updates.downloads);
}

void test_without_restarting_the_installed_version_isnt_offered_again()
{
    Device d;
    d.updates().setRestartAfterInstall(false);
    d.connect();
    d.installNewVersion();
    TEST_ASSERT_EQUAL(UpdateState::Idle, d.updates().state());
    d.advance(5000);
    TEST_ASSERT_EQUAL(0, d.fakes.system.restarts);
    d.checkWith(manifest("2.0.0"));
    TEST_ASSERT_EQUAL(UpdateState::Idle, d.updates().state());
}

void test_the_portal_waits_for_a_download()
{
    Device d;
    d.connect();
    d.checkWith(manifest("2.0.0"));
    TEST_ASSERT_TRUE(d.updates().install());
    d.esp.startPortal();
    d.esp.process();
    TEST_ASSERT_EQUAL(PortalState::Off, d.esp.portalState());
    d.fakes.updates.fail(UpdateErrorCode::DownloadFailed);
    d.esp.process();
    d.esp.process();
    TEST_ASSERT_EQUAL(PortalState::Active, d.esp.portalState());
}

void test_new_firmware_is_confirmed_once_connected()
{
    Device d;
    d.fakes.storage.putU32(UpdateManager::kNamespace, "outcome", 1);  // Pending
    d.fakes.storage.putString(UpdateManager::kNamespace, "from", "1.0.0");
    d.fakes.storage.putString(UpdateManager::kNamespace, "to", "2.0.0");
    d.esp.setFirmwareVersion("2.0.0");
    d.fakes.updates.pending = true;
    d.esp.begin();
    d.esp.process();
    TEST_ASSERT_TRUE(d.updates().pending());
    TEST_ASSERT_EQUAL(0, d.fakes.updates.confirms);
    d.fakes.wifi.completeConnect(11);
    d.esp.process();
    TEST_ASSERT_EQUAL(1, d.fakes.updates.confirms);
    TEST_ASSERT_FALSE(d.updates().pending());
    TEST_ASSERT_EQUAL(UpdateOutcome::Succeeded, d.updates().lastResult().outcome);
    TEST_ASSERT_EQUAL(1, *d.fakes.storage.getU32(UpdateManager::kNamespace, "outcome") - 1);
    TEST_ASSERT_TRUE(d.failures.empty());
}

void test_new_firmware_is_confirmed_after_ten_minutes_without_a_connection()
{
    Device d;
    d.fakes.updates.pending = true;
    d.esp.begin();
    d.esp.process();
    d.advance(UpdateManager::kConfirmAfterMs - 1000);
    TEST_ASSERT_EQUAL(0, d.fakes.updates.confirms);
    d.advance(1000);
    TEST_ASSERT_EQUAL(1, d.fakes.updates.confirms);
}

void test_confirming_manually_waits_for_mark_valid()
{
    Device d;
    d.updates().confirmManually();
    d.fakes.updates.pending = true;
    d.connect();
    d.advance(UpdateManager::kConfirmAfterMs + 1000);
    TEST_ASSERT_EQUAL(0, d.fakes.updates.confirms);
    TEST_ASSERT_TRUE(d.updates().markValid());
    TEST_ASSERT_EQUAL(1, d.fakes.updates.confirms);
    TEST_ASSERT_FALSE(d.updates().markValid());
}

void test_a_rolled_back_update_is_reported()
{
    Device d;
    d.fakes.storage.putU32(UpdateManager::kNamespace, "outcome", 1);  // Pending
    d.fakes.storage.putString(UpdateManager::kNamespace, "from", "1.0.0");
    d.fakes.storage.putString(UpdateManager::kNamespace, "to", "2.0.0");
    d.esp.begin();
    d.esp.process();
    TEST_ASSERT_EQUAL(1, d.failures.size());
    TEST_ASSERT_EQUAL(UpdateErrorCode::RolledBack, d.failures[0].code);
    TEST_ASSERT_EQUAL_STRING("2.0.0", d.failures[0].version.c_str());
    TEST_ASSERT_EQUAL(UpdateOutcome::RolledBack, d.updates().lastResult().outcome);
    TEST_ASSERT_EQUAL(4, *d.fakes.storage.getU32(UpdateManager::kNamespace, "outcome"));
}

void test_a_late_updates_call_still_confirms()
{
    // updates() first called after begin().
    Device d(false);
    d.fakes.updates.pending = true;
    d.esp.begin();
    d.esp.process();
    d.esp.updates();
    TEST_ASSERT_TRUE(d.esp.updates().pending());
    d.advance(UpdateManager::kConfirmAfterMs);
    TEST_ASSERT_EQUAL(1, d.fakes.updates.confirms);
}

void test_the_record_survives_a_restart_and_status_shows_it()
{
    Device d;
    d.connect();
    d.installNewVersion();
    Device e;
    e.fakes.storage = d.fakes.storage;
    e.esp.setFirmwareVersion("2.0.0");
    e.connect();
    TEST_ASSERT_EQUAL(UpdateOutcome::Succeeded, e.updates().lastResult().outcome);
    const espmgr::DeviceStatus status = e.esp.status();
    TEST_ASSERT_TRUE(status.update.configured);
    TEST_ASSERT_EQUAL_STRING("stable", status.update.channel.c_str());
    TEST_ASSERT_EQUAL(UpdateOutcome::Succeeded, status.update.last.outcome);
    TEST_ASSERT_EQUAL_STRING("1.0.0", status.update.last.fromVersion.c_str());
    TEST_ASSERT_EQUAL_STRING("2.0.0", status.update.last.toVersion.c_str());
}

void test_error_codes_have_names()
{
    TEST_ASSERT_EQUAL_STRING("server-unreachable",
                             espmgr::toString(UpdateErrorCode::ServerUnreachable));
    TEST_ASSERT_EQUAL_STRING("signature-invalid",
                             espmgr::toString(UpdateErrorCode::SignatureInvalid));
    TEST_ASSERT_EQUAL_STRING("rolled-back", espmgr::toString(UpdateErrorCode::RolledBack));
    TEST_ASSERT_EQUAL_STRING("downloading", espmgr::toString(UpdateState::Downloading));
    TEST_ASSERT_EQUAL_STRING("rolled-back", espmgr::toString(UpdateOutcome::RolledBack));
    TEST_ASSERT_EQUAL_STRING("automatic", espmgr::toString(UpdateMethod::Automatic));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_updates_need_a_server_and_a_connection);
    RUN_TEST(test_server_and_channel_are_checked);
    RUN_TEST(test_a_check_fetches_the_manifest_with_headers_and_root_ca);
    RUN_TEST(test_headers_are_limited_and_never_logged);
    RUN_TEST(test_a_newer_version_is_available_and_announced_once);
    RUN_TEST(test_the_same_or_an_older_version_is_no_update);
    RUN_TEST(test_a_failed_check_raises_the_error_and_keeps_what_was_available);
    RUN_TEST(test_invalid_manifests_and_versions_are_errors);
    RUN_TEST(test_a_device_below_the_minimum_version_is_too_old);
    RUN_TEST(test_an_install_downloads_verifies_activates_and_restarts);
    RUN_TEST(test_install_without_a_public_key_is_refused_unless_allowed);
    RUN_TEST(test_install_checks_space_memory_and_partitions_first);
    RUN_TEST(test_a_wrong_size_hash_or_signature_is_rejected);
    RUN_TEST(test_a_bad_signature_encoding_or_activation_failure_is_an_error);
    RUN_TEST(test_a_failed_or_interrupted_download_keeps_the_firmware);
    RUN_TEST(test_automatic_checks_follow_the_connection_and_interval);
    RUN_TEST(test_automatic_checks_are_off_by_default_and_wait_for_the_portal);
    RUN_TEST(test_the_check_interval_has_a_minimum);
    RUN_TEST(test_the_install_policy_installs_when_the_condition_allows);
    RUN_TEST(test_without_restarting_the_installed_version_isnt_offered_again);
    RUN_TEST(test_the_portal_waits_for_a_download);
    RUN_TEST(test_new_firmware_is_confirmed_once_connected);
    RUN_TEST(test_new_firmware_is_confirmed_after_ten_minutes_without_a_connection);
    RUN_TEST(test_confirming_manually_waits_for_mark_valid);
    RUN_TEST(test_a_rolled_back_update_is_reported);
    RUN_TEST(test_a_late_updates_call_still_confirms);
    RUN_TEST(test_the_record_survives_a_restart_and_status_shows_it);
    RUN_TEST(test_error_codes_have_names);
    return UNITY_END();
}
