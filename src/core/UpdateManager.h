#pragma once

#include "core/Clock.h"
#include "core/Events.h"
#include "core/Logger.h"
#include "core/Manifest.h"
#include "core/Storage.h"
#include "core/System.h"
#include "core/UpdateDriver.h"
#include "core/UpdateTypes.h"
#include "core/WifiConnection.h"

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace espmgr
{

// Firmware updates (EP17): checks the Update Server's Manifest for the device's Update Channel,
// downloads a newer image into the other OTA slot, checks its size, SHA-256 and signature,
// makes it the boot image and restarts. New firmware is pending until it confirms itself, so a
// firmware that can't start properly is rolled back by the bootloader. Nothing here blocks
// process(): the platform's UpdateDriver does the HTTPS work in the background.
class UpdateManager
{
public:
    static constexpr const char* kDefaultChannel = "stable";
    static constexpr size_t kMaxChannelLength = 32;
    static constexpr size_t kMaxUrlLength = 256;
    static constexpr size_t kMaxHeaders = 4;
    static constexpr uint32_t kDefaultCheckIntervalMs = 24UL * 60 * 60 * 1000;
    static constexpr uint32_t kMinCheckIntervalMs = 15UL * 60 * 1000;
    // The first automatic check after connecting waits this long plus up to kCheckJitterMs.
    static constexpr uint32_t kFirstCheckDelayMs = 60000;
    static constexpr uint32_t kCheckJitterMs = 30000;
    // Free heap needed before a download starts, for TLS.
    static constexpr uint32_t kMinFreeHeap = 40 * 1024;
    // Progress events come at most this often.
    static constexpr uint32_t kProgressIntervalMs = 250;
    // The restart after installing waits this long, so the page and listeners can react.
    static constexpr uint32_t kRestartDelayMs = 2000;
    // Pending firmware is confirmed after running this long, even without a connection.
    static constexpr uint32_t kConfirmAfterMs = 10UL * 60 * 1000;
    // Where the history record lives.
    static constexpr const char* kNamespace = "espmgr-ota";

    struct Hooks
    {
        // The installed version, as given to esp.setFirmwareVersion().
        std::function<std::string()> installedVersion;
        // Whether the Provisioning Portal is open; no automatic check runs meanwhile.
        std::function<bool()> portalActive;
        // Seconds since 1970 when Network Time is synchronised, else 0.
        std::function<int64_t()> epoch;
        // Restarts the device.
        std::function<void()> restart;
    };

    UpdateManager(Clock& clock, System& system, Storage& storage, const WifiConnection& connection,
                  Events& events, Logger& log, Hooks hooks);

    UpdateManager(const UpdateManager&) = delete;
    UpdateManager& operator=(const UpdateManager&) = delete;

    // --- Set-up, before begin() ---

    // The folder on the Update Server that holds the Manifests, such as
    // "https://updates.example.com/thermostat/". The Manifest is "<server><channel>.json"; a
    // missing "/" at the end is added. HTTPS only, at most 256 characters; a refused URL changes
    // nothing, is logged at Error and returns false.
    bool setServer(const std::string& url);
    const std::string& server() const
    {
        return server_;
    }
    // The Update Channel, "stable" by default: 1 to 32 letters, digits, "-" or "_".
    bool setChannel(const std::string& channel);
    const std::string& channel() const
    {
        return channel_;
    }
    // The URL of the Manifest; empty while no server is set.
    std::string manifestUrl() const;

    // The Public Key (PEM) that images must be signed with. Not copied: pass a string that lives
    // as long as the EspManager, such as a literal. Without one every install is refused, unless
    // allowUnsigned().
    UpdateManager& setPublicKey(const char* pem);
    // Installs images without a signature check, for development; the SHA-256 is still checked.
    // Logged as a warning at begin().
    UpdateManager& allowUnsigned();
    // Trusts only this PEM root certificate for the Update Server, instead of the platform's
    // bundle of public ones. Not copied, like setPublicKey().
    UpdateManager& setRootCa(const char* pem);
    // Sends this header with every request, such as an Authorization token. At most 4; copied.
    // The value is never logged or shown. Returns false for a fifth or an empty name.
    bool addHeader(const std::string& name, const std::string& value);

    // Checks automatically: about a minute after connecting, then every interval. Off by
    // default.
    UpdateManager& setAutoCheck(bool on);
    bool autoCheck() const
    {
        return autoCheck_;
    }
    // 24 hours by default, at least 15 minutes; returns false, changing nothing, below that.
    bool setCheckInterval(uint32_t ms);
    uint32_t checkInterval() const
    {
        return intervalMs_;
    }
    // What to do when a check finds an update: Notify (the default) or Install.
    UpdateManager& setInstallPolicy(InstallPolicy policy);
    InstallPolicy installPolicy() const
    {
        return policy_;
    }
    // With the Install policy, installs only once `condition` returns true, for example not
    // while a motor runs. Asked on every process() while an update waits.
    UpdateManager& setInstallCondition(std::function<bool()> condition);
    // Restarts 2 s after installing (the default). With false, the sketch restarts when it
    // suits; the new firmware starts then.
    UpdateManager& setRestartAfterInstall(bool restart);
    // Leaves confirming new firmware to the sketch's markValid(), instead of confirming once it
    // connects or has run for 10 minutes.
    UpdateManager& confirmManually();

    // --- Actions ---

    // Fetches the Manifest now. False if it can't start: no server, not connected, or busy.
    bool check();
    // Installs the update check() found. False if there's none, or it can't start; a refusal
    // such as a missing Public Key or too little space also raises onUpdateFailed.
    bool install();
    // Confirms pending firmware. False if the running firmware isn't pending.
    bool markValid();

    // --- State ---

    UpdateState state() const
    {
        return state_;
    }
    // The update found by the last check; empty version if none.
    const UpdateInfo& available() const
    {
        return available_;
    }
    // 0 to 100 while downloading.
    uint8_t progress() const
    {
        return percent_;
    }
    // The most recent install attempt, kept across restarts.
    const UpdateRecord& lastResult() const
    {
        return record_;
    }
    // The last failure of a check or an install since start-up; None if there was none.
    const UpdateError& lastError() const
    {
        return lastError_;
    }
    // Whether the running firmware is pending confirmation.
    bool pending() const
    {
        return awaitingConfirm_;
    }
    // Whether a server is set and a driver is there, so updates can work at all.
    bool configured() const
    {
        return driver_ != nullptr && !server_.empty();
    }
    // Whether a download or install is running; the Provisioning Portal waits for it.
    bool busy() const;

    // Used by EspManager.
    // The platform's driver; may come after begin(), when esp.updates() is first called late.
    void attach(UpdateDriver& driver);
    bool attached() const
    {
        return driver_ != nullptr;
    }
    void begin();
    void process();

private:
    UpdateRequest request(const std::string& url) const;
    void startCheck(UpdateMethod method);
    void finishCheck(const UpdateDriver::Status& status);
    bool startInstall(UpdateMethod method);
    void processDownload(const UpdateDriver::Status& status);
    void finishInstall();
    void fail(UpdateErrorCode code, const std::string& version, int httpStatus = 0);
    void failInstall(UpdateErrorCode code);
    // What begin() does once a driver is there: confirming and rollback reporting.
    void startUp();
    void confirmNow();
    void loadRecord();
    void saveRecord();

    Clock& clock_;
    System& system_;
    Storage& storage_;
    const WifiConnection& connection_;
    Events& events_;
    Logger& log_;
    Hooks hooks_;
    UpdateDriver* driver_ = nullptr;

    std::string server_;
    std::string channel_ = kDefaultChannel;
    const char* publicKey_ = nullptr;
    bool allowUnsigned_ = false;
    const char* rootCa_ = nullptr;
    std::vector<std::pair<std::string, std::string>> headers_;
    bool autoCheck_ = false;
    uint32_t intervalMs_ = kDefaultCheckIntervalMs;
    InstallPolicy policy_ = InstallPolicy::Notify;
    std::function<bool()> installCondition_;
    bool restartAfterInstall_ = true;
    bool confirmManually_ = false;

    bool begun_ = false;
    uint64_t begunAtMs_ = 0;
    UpdateState state_ = UpdateState::Idle;
    UpdateMethod method_ = UpdateMethod::Manual;
    UpdateMethod checkMethod_ = UpdateMethod::Manual;
    UpdateInfo available_;
    // The full Manifest behind available_, and where its image is.
    Manifest manifest_;
    std::string imageUrl_;
    // The last version onUpdateAvailable was raised for, so it's raised once per version.
    std::string announced_;
    // An automatic install was tried for this check's result; the next check clears it.
    bool autoInstallTried_ = false;
    uint8_t percent_ = 0;
    uint64_t lastProgressMs_ = 0;
    bool progressSent_ = false;
    uint64_t restartAtMs_ = 0;
    bool wasConnected_ = false;
    // 0: no automatic check scheduled yet.
    uint64_t nextCheckMs_ = 0;
    bool awaitingConfirm_ = false;
    // Installed without restarting yet; a check doesn't offer it again.
    std::string installedPending_;
    UpdateRecord record_;
    UpdateError lastError_;
};

}  // namespace espmgr
