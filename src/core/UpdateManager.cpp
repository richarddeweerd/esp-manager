#include "core/UpdateManager.h"

#include "core/Base64.h"
#include "core/SemVer.h"

#include <cstdio>

namespace espmgr
{

namespace
{

constexpr const char* kTag = "update";

bool printableWithoutSpaces(const std::string& text)
{
    for (const char c : text)
    {
        if (c <= ' ' || c == 0x7f)
        {
            return false;
        }
    }
    return true;
}

std::string toHex(const UpdateDriver::Sha256& hash)
{
    static const char digits[] = "0123456789abcdef";
    std::string text;
    text.reserve(64);
    for (const uint8_t byte : hash)
    {
        text += digits[byte >> 4];
        text += digits[byte & 0x0f];
    }
    return text;
}

}  // namespace

UpdateManager::UpdateManager(Clock& clock, System& system, Storage& storage,
                             const WifiConnection& connection, Events& events, Logger& log,
                             Hooks hooks)
    : clock_(clock), system_(system), storage_(storage), connection_(connection), events_(events),
      log_(log), hooks_(std::move(hooks))
{
}

bool UpdateManager::setServer(const std::string& url)
{
    const bool https = url.rfind("https://", 0) == 0 && url.size() > 8 && url[8] != '/';
    if (!https || url.size() > kMaxUrlLength || !printableWithoutSpaces(url))
    {
        log_.error(kTag,
                   "refusing Update Server \"%s\": it must be an https:// URL of at most "
                   "256 characters",
                   withoutQuery(url).c_str());
        return false;
    }
    server_ = url;
    if (server_.back() != '/')
    {
        server_ += '/';
    }
    return true;
}

bool UpdateManager::setChannel(const std::string& channel)
{
    bool ok = !channel.empty() && channel.size() <= kMaxChannelLength;
    for (const char c : channel)
    {
        ok = ok && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                    c == '-' || c == '_');
    }
    if (!ok)
    {
        log_.error(kTag, "refusing Update Channel \"%s\": use 1 to 32 letters, digits, - or _",
                   channel.c_str());
        return false;
    }
    channel_ = channel;
    return true;
}

std::string UpdateManager::manifestUrl() const
{
    return server_.empty() ? std::string() : server_ + channel_ + ".json";
}

UpdateManager& UpdateManager::setPublicKey(const char* pem)
{
    publicKey_ = (pem != nullptr && pem[0] != '\0') ? pem : nullptr;
    return *this;
}

UpdateManager& UpdateManager::allowUnsigned()
{
    allowUnsigned_ = true;
    return *this;
}

UpdateManager& UpdateManager::setRootCa(const char* pem)
{
    rootCa_ = (pem != nullptr && pem[0] != '\0') ? pem : nullptr;
    return *this;
}

bool UpdateManager::addHeader(const std::string& name, const std::string& value)
{
    bool ok = !name.empty() && printableWithoutSpaces(name) && name.find(':') == std::string::npos;
    for (const char c : value)
    {
        ok = ok && c != '\r' && c != '\n' && c != '\0';
    }
    if (!ok || headers_.size() >= kMaxHeaders)
    {
        // The value may be a secret, so only the name is logged.
        log_.error(kTag, "refusing header \"%s\"", name.c_str());
        return false;
    }
    headers_.emplace_back(name, value);
    return true;
}

UpdateManager& UpdateManager::setAutoCheck(bool on)
{
    autoCheck_ = on;
    if (!on)
    {
        nextCheckMs_ = 0;
    }
    else if (nextCheckMs_ == 0 && wasConnected_)
    {
        nextCheckMs_ = clock_.nowMs() + kFirstCheckDelayMs + clock_.random(kCheckJitterMs);
    }
    return *this;
}

bool UpdateManager::setCheckInterval(uint32_t ms)
{
    if (ms < kMinCheckIntervalMs)
    {
        log_.error(kTag, "refusing check interval of %lu ms: at least 15 minutes",
                   static_cast<unsigned long>(ms));
        return false;
    }
    intervalMs_ = ms;
    return true;
}

UpdateManager& UpdateManager::setInstallPolicy(InstallPolicy policy)
{
    policy_ = policy;
    return *this;
}

UpdateManager& UpdateManager::setInstallCondition(std::function<bool()> condition)
{
    installCondition_ = std::move(condition);
    return *this;
}

UpdateManager& UpdateManager::setRestartAfterInstall(bool restart)
{
    restartAfterInstall_ = restart;
    return *this;
}

UpdateManager& UpdateManager::confirmManually()
{
    confirmManually_ = true;
    return *this;
}

bool UpdateManager::busy() const
{
    return state_ == UpdateState::Downloading || state_ == UpdateState::Verifying ||
           state_ == UpdateState::Installing || state_ == UpdateState::Restarting;
}

bool UpdateManager::check()
{
    if (!begun_)
    {
        return false;
    }
    if (!configured())
    {
        log_.error(kTag, "can't check for updates: no Update Server set");
        return false;
    }
    if (connection_.state() != ConnectionState::Connected || state_ == UpdateState::Checking ||
        busy())
    {
        return false;
    }
    startCheck(UpdateMethod::Manual);
    return true;
}

bool UpdateManager::install()
{
    return begun_ && startInstall(UpdateMethod::Manual);
}

bool UpdateManager::markValid()
{
    if (!awaitingConfirm_)
    {
        return false;
    }
    confirmNow();
    return true;
}

void UpdateManager::attach(UpdateDriver& driver)
{
    driver_ = &driver;
}

void UpdateManager::begin()
{
    begun_ = true;
    begunAtMs_ = clock_.nowMs();
    loadRecord();
    if (driver_ != nullptr && allowUnsigned_ && publicKey_ == nullptr)
    {
        log_.warn(kTag, "unsigned firmware updates are allowed; set a Public Key for production");
    }
    // Every new image starts pending, also one installed some other way, such as ArduinoOTA.
    awaitingConfirm_ = system_.firmwarePending();
    reportOutcome();
    if (awaitingConfirm_ && driver_ == nullptr)
    {
        // Updates aren't used, so keep the platform's usual behaviour: accept it at once.
        confirmNow();
    }
}

void UpdateManager::reportOutcome()
{
    if (record_.outcome != UpdateOutcome::Pending)
    {
        return;
    }
    const std::string installed = hooks_.installedVersion ? hooks_.installedVersion() : "";
    if (installed == record_.toVersion)
    {
        if (!awaitingConfirm_)
        {
            record_.outcome = UpdateOutcome::Succeeded;
            saveRecord();
        }
        return;
    }
    // The bootloader went back to the previous firmware.
    log_.warn(kTag, "firmware %s was rolled back; running %s", record_.toVersion.c_str(),
              installed.c_str());
    record_.outcome = UpdateOutcome::RolledBack;
    record_.error = UpdateErrorCode::RolledBack;
    saveRecord();
    fail(UpdateErrorCode::RolledBack, record_.toVersion);
}

void UpdateManager::process()
{
    if (!begun_ || driver_ == nullptr)
    {
        return;
    }
    const uint64_t now = clock_.nowMs();
    const bool connected = connection_.state() == ConnectionState::Connected;
    if (connected && !wasConnected_ && autoCheck_ && nextCheckMs_ == 0)
    {
        nextCheckMs_ = now + kFirstCheckDelayMs + clock_.random(kCheckJitterMs);
    }
    wasConnected_ = connected;

    if (awaitingConfirm_ && !confirmManually_ && (connected || now - begunAtMs_ >= kConfirmAfterMs))
    {
        confirmNow();
    }

    switch (state_)
    {
    case UpdateState::Checking:
    {
        const UpdateDriver::Status status = driver_->status();
        if (status.state == UpdateDriver::JobState::Done ||
            status.state == UpdateDriver::JobState::Failed)
        {
            finishCheck(status);
        }
        break;
    }
    case UpdateState::Downloading:
        processDownload(driver_->status());
        break;
    case UpdateState::Verifying:
        finishInstall();
        break;
    case UpdateState::Restarting:
        if (restartAtMs_ != 0 && now >= restartAtMs_)
        {
            restartAtMs_ = 0;
            if (hooks_.restart)
            {
                hooks_.restart();
            }
        }
        break;
    case UpdateState::Available:
        if (policy_ == InstallPolicy::Install && !autoInstallTried_ && connected &&
            (!installCondition_ || installCondition_()))
        {
            autoInstallTried_ = true;
            startInstall(UpdateMethod::Automatic);
        }
        break;
    case UpdateState::Idle:
    case UpdateState::Installing:
        break;
    }

    const bool canCheck = state_ == UpdateState::Idle || state_ == UpdateState::Available;
    if (autoCheck_ && configured() && connected && canCheck && nextCheckMs_ != 0 &&
        now >= nextCheckMs_ && !(hooks_.portalActive && hooks_.portalActive()))
    {
        startCheck(UpdateMethod::Automatic);
    }
}

UpdateRequest UpdateManager::request(const std::string& url) const
{
    UpdateRequest request;
    request.url = url;
    request.headers = headers_;
    request.rootCa = rootCa_;
    return request;
}

void UpdateManager::startCheck(UpdateMethod method)
{
    if (!driver_->fetch(request(manifestUrl()), Manifest::kMaxSize))
    {
        return;
    }
    log_.info(kTag, "checking %s", withoutQuery(manifestUrl()).c_str());
    checkMethod_ = method;
    state_ = UpdateState::Checking;
    autoInstallTried_ = false;
    // Any check, also a manual one, moves the next automatic one.
    if (autoCheck_)
    {
        nextCheckMs_ = clock_.nowMs() + intervalMs_;
    }
}

void UpdateManager::finishCheck(const UpdateDriver::Status& status)
{
    // Unless the Manifest offers an update, what was available before is gone.
    const UpdateInfo previous = available_;
    available_ = UpdateInfo();
    state_ = UpdateState::Idle;
    if (status.state == UpdateDriver::JobState::Failed)
    {
        const UpdateErrorCode code = status.error == UpdateErrorCode::None
                                         ? UpdateErrorCode::ServerUnreachable
                                         : status.error;
        if (!previous.version.empty())
        {
            available_ = previous;
            state_ = UpdateState::Available;
        }
        fail(code, "", status.httpStatus);
        return;
    }
    std::optional<Manifest> manifest = Manifest::parse(driver_->body());
    if (!manifest || (publicKey_ != nullptr && manifest->signature.empty()))
    {
        fail(UpdateErrorCode::ManifestInvalid, "");
        return;
    }
    const std::string installedText = hooks_.installedVersion ? hooks_.installedVersion() : "";
    const std::optional<SemVer> installed = SemVer::parse(installedText);
    if (!installed)
    {
        log_.error(kTag,
                   "the firmware version \"%s\" isn't a SemVer version; set it with "
                   "esp.setFirmwareVersion()",
                   installedText.c_str());
        fail(UpdateErrorCode::VersionInvalid, manifest->version);
        return;
    }
    const std::optional<SemVer> offered = SemVer::parse(manifest->version);
    std::optional<SemVer> minimum;
    if (!manifest->minimumVersion.empty())
    {
        minimum = SemVer::parse(manifest->minimumVersion);
    }
    if (!offered || (!manifest->minimumVersion.empty() && !minimum))
    {
        log_.error(kTag, "the Manifest's version \"%s\" isn't a SemVer version",
                   manifest->version.c_str());
        fail(UpdateErrorCode::VersionInvalid, manifest->version);
        return;
    }
    if (SemVer::compare(*offered, *installed) <= 0 || manifest->version == installedPending_)
    {
        log_.info(kTag, "up to date (%s, offered %s)", installedText.c_str(),
                  manifest->version.c_str());
        return;
    }
    if (minimum && SemVer::compare(*installed, *minimum) < 0)
    {
        log_.warn(kTag, "%s can't update to %s, which needs at least %s", installedText.c_str(),
                  manifest->version.c_str(), manifest->minimumVersion.c_str());
        fail(UpdateErrorCode::TooOld, manifest->version);
        return;
    }
    const std::string imageUrl = resolveUrl(manifestUrl(), manifest->url);
    if (imageUrl.empty())
    {
        log_.error(kTag, "the Manifest's url isn't an https:// URL");
        fail(UpdateErrorCode::ManifestInvalid, manifest->version);
        return;
    }
    manifest_ = *manifest;
    imageUrl_ = imageUrl;
    available_.version = manifest_.version;
    available_.notes = manifest_.notes;
    available_.size = manifest_.size;
    state_ = UpdateState::Available;
    log_.info(kTag, "update available: %s -> %s", installedText.c_str(),
              available_.version.c_str());
    if (announced_ != available_.version)
    {
        announced_ = available_.version;
        events_.raiseUpdateAvailable(available_);
    }
}

bool UpdateManager::startInstall(UpdateMethod method)
{
    if (state_ != UpdateState::Available || connection_.state() != ConnectionState::Connected)
    {
        return false;
    }
    method_ = method;
    if (publicKey_ == nullptr && !allowUnsigned_)
    {
        log_.error(kTag, "refusing to install: no Public Key set (setPublicKey())");
        failInstall(UpdateErrorCode::Unsigned);
        return false;
    }
    const size_t slot = driver_->slotSize();
    if (slot == 0)
    {
        log_.error(kTag, "refusing to install: the partition table has no second OTA slot");
        failInstall(UpdateErrorCode::NoOtaPartition);
        return false;
    }
    if (available_.size > slot)
    {
        log_.error(kTag, "refusing to install: %lu bytes don't fit the %lu-byte OTA slot",
                   static_cast<unsigned long>(available_.size), static_cast<unsigned long>(slot));
        failInstall(UpdateErrorCode::NoSpace);
        return false;
    }
    if (system_.freeHeap() < kMinFreeHeap)
    {
        log_.error(kTag, "refusing to install: only %lu bytes of heap free",
                   static_cast<unsigned long>(system_.freeHeap()));
        failInstall(UpdateErrorCode::LowMemory);
        return false;
    }
    if (!driver_->download(request(imageUrl_), available_.size))
    {
        return false;
    }
    log_.info(kTag, "downloading %s from %s", available_.version.c_str(),
              withoutQuery(imageUrl_).c_str());
    state_ = UpdateState::Downloading;
    percent_ = 0;
    progressSent_ = false;
    lastProgressMs_ = clock_.nowMs();
    events_.raiseUpdateStarted(available_);
    return true;
}

void UpdateManager::processDownload(const UpdateDriver::Status& status)
{
    if (status.state == UpdateDriver::JobState::Running &&
        connection_.state() != ConnectionState::Connected)
    {
        driver_->abort();
        log_.warn(kTag, "download stopped: the connection was lost");
        failInstall(UpdateErrorCode::DownloadFailed);
        return;
    }
    if (status.state == UpdateDriver::JobState::Failed ||
        status.state == UpdateDriver::JobState::Idle)
    {
        failInstall(status.error == UpdateErrorCode::None ? UpdateErrorCode::DownloadFailed
                                                          : status.error);
        return;
    }
    const uint64_t now = clock_.nowMs();
    const bool done = status.state == UpdateDriver::JobState::Done;
    uint8_t percent = 100;
    if (!done && available_.size > 0)
    {
        const uint64_t scaled = static_cast<uint64_t>(status.bytes) * 100 / available_.size;
        percent = static_cast<uint8_t>(scaled > 99 ? 99 : scaled);
    }
    if (percent != percent_ || (done && !progressSent_))
    {
        percent_ = percent;
        if (done || now - lastProgressMs_ >= kProgressIntervalMs)
        {
            lastProgressMs_ = now;
            progressSent_ = done;
            events_.raiseUpdateProgress(UpdateProgress{percent_});
        }
    }
    if (done)
    {
        state_ = UpdateState::Verifying;
    }
}

void UpdateManager::finishInstall()
{
    const UpdateDriver::Status status = driver_->status();
    if (status.bytes != available_.size)
    {
        log_.error(kTag, "got %lu bytes, the Manifest says %lu",
                   static_cast<unsigned long>(status.bytes),
                   static_cast<unsigned long>(available_.size));
        failInstall(UpdateErrorCode::SizeMismatch);
        return;
    }
    const UpdateDriver::Sha256 hash = driver_->sha256();
    if (toHex(hash) != manifest_.sha256)
    {
        log_.error(kTag, "the image's SHA-256 doesn't match the Manifest");
        failInstall(UpdateErrorCode::HashMismatch);
        return;
    }
    if (publicKey_ != nullptr)
    {
        const std::optional<std::vector<uint8_t>> signature = decodeBase64(manifest_.signature);
        if (!signature || !driver_->verifySignature(publicKey_, hash, *signature))
        {
            log_.error(kTag, "the image's signature doesn't match the Public Key");
            failInstall(UpdateErrorCode::SignatureInvalid);
            return;
        }
    }
    else
    {
        log_.warn(kTag, "installing an unsigned image");
    }
    state_ = UpdateState::Installing;
    if (!driver_->activate())
    {
        log_.error(kTag, "couldn't make the new image the boot image");
        failInstall(UpdateErrorCode::WriteFailed);
        return;
    }
    record_ = UpdateRecord();
    record_.outcome = UpdateOutcome::Pending;
    record_.fromVersion = hooks_.installedVersion ? hooks_.installedVersion() : "";
    record_.toVersion = available_.version;
    record_.method = method_;
    record_.epoch = hooks_.epoch ? hooks_.epoch() : 0;
    saveRecord();
    log_.info(kTag, "installed %s", available_.version.c_str());
    events_.raiseUpdateCompleted(available_);
    installedPending_ = available_.version;
    if (restartAfterInstall_)
    {
        state_ = UpdateState::Restarting;
        restartAtMs_ = clock_.nowMs() + kRestartDelayMs;
    }
    else
    {
        state_ = UpdateState::Idle;
        available_ = UpdateInfo();
    }
}

void UpdateManager::fail(UpdateErrorCode code, const std::string& version, int httpStatus)
{
    lastError_.code = code;
    lastError_.httpStatus = httpStatus;
    lastError_.version = version;
    if (code == UpdateErrorCode::HttpStatus)
    {
        log_.warn(kTag, "update failed: HTTP %d", httpStatus);
    }
    else
    {
        log_.warn(kTag, "update failed: %s", toString(code));
    }
    events_.raiseUpdateFailed(lastError_);
}

void UpdateManager::failInstall(UpdateErrorCode code)
{
    record_ = UpdateRecord();
    record_.outcome = UpdateOutcome::Failed;
    record_.fromVersion = hooks_.installedVersion ? hooks_.installedVersion() : "";
    record_.toVersion = available_.version;
    record_.method = method_;
    record_.error = code;
    record_.epoch = hooks_.epoch ? hooks_.epoch() : 0;
    saveRecord();
    state_ = UpdateState::Available;
    percent_ = 0;
    fail(code, available_.version);
}

void UpdateManager::confirmNow()
{
    system_.confirmFirmware();
    awaitingConfirm_ = false;
    const std::string installed = hooks_.installedVersion ? hooks_.installedVersion() : "";
    log_.info(kTag, "firmware %s confirmed", installed.c_str());
    if (record_.outcome == UpdateOutcome::Pending && record_.toVersion == installed)
    {
        record_.outcome = UpdateOutcome::Succeeded;
        saveRecord();
    }
}

void UpdateManager::loadRecord()
{
    record_ = UpdateRecord();
    const std::optional<uint32_t> outcome = storage_.getU32(kNamespace, "outcome");
    if (!outcome || *outcome > static_cast<uint32_t>(UpdateOutcome::RolledBack))
    {
        return;
    }
    record_.outcome = static_cast<UpdateOutcome>(*outcome);
    record_.fromVersion = storage_.getString(kNamespace, "from").value_or("");
    record_.toVersion = storage_.getString(kNamespace, "to").value_or("");
    record_.method = storage_.getU32(kNamespace, "method").value_or(0) == 1
                         ? UpdateMethod::Automatic
                         : UpdateMethod::Manual;
    const uint32_t error = storage_.getU32(kNamespace, "error").value_or(0);
    record_.error = error <= static_cast<uint32_t>(UpdateErrorCode::RolledBack)
                        ? static_cast<UpdateErrorCode>(error)
                        : UpdateErrorCode::None;
    record_.epoch = storage_.getU32(kNamespace, "epoch").value_or(0);
}

void UpdateManager::saveRecord()
{
    const bool stored =
        storage_.putU32(kNamespace, "outcome", static_cast<uint32_t>(record_.outcome)) &&
        storage_.putString(kNamespace, "from", record_.fromVersion) &&
        storage_.putString(kNamespace, "to", record_.toVersion) &&
        storage_.putU32(kNamespace, "method", static_cast<uint32_t>(record_.method)) &&
        storage_.putU32(kNamespace, "error", static_cast<uint32_t>(record_.error)) &&
        storage_.putU32(kNamespace, "epoch", static_cast<uint32_t>(record_.epoch));
    if (!stored)
    {
        log_.error(kTag, "couldn't store the update record");
    }
}

}  // namespace espmgr
