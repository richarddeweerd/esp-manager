#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace espmgr
{

// Why a firmware update check or install failed (US17.11.2).
enum class UpdateErrorCode : uint8_t
{
    None,
    // The Update Server didn't answer, or TLS failed.
    ServerUnreachable,
    // The Update Server answered with an HTTP status other than 200.
    HttpStatus,
    // The Manifest isn't valid JSON, misses a required field or is too large.
    ManifestInvalid,
    // The Manifest's or the installed version isn't a SemVer version.
    VersionInvalid,
    // The installed version is older than the Manifest's minimumVersion.
    TooOld,
    // The image doesn't fit the other OTA slot.
    NoSpace,
    // Too little free heap for TLS.
    LowMemory,
    // The partition table has no second OTA slot.
    NoOtaPartition,
    // No Public Key was set and allowUnsigned() wasn't called.
    Unsigned,
    // The download broke off.
    DownloadFailed,
    // The image's size differs from the Manifest's.
    SizeMismatch,
    // The image's SHA-256 differs from the Manifest's.
    HashMismatch,
    // The image's signature doesn't match the Public Key.
    SignatureInvalid,
    // Writing the image or making it the boot image failed.
    WriteFailed,
    // The new firmware wasn't confirmed, so the bootloader went back to the previous one.
    RolledBack,
};

// A short code for JSON and logs, such as "hash-mismatch"; "none" for None.
const char* toString(UpdateErrorCode code);

// Where the updates part stands.
enum class UpdateState : uint8_t
{
    Idle,
    Checking,
    // A newer version was found and can be installed.
    Available,
    Downloading,
    Verifying,
    Installing,
    // Installed; the device restarts in a moment.
    Restarting,
};

// "idle", "checking", "available", "downloading", "verifying", "installing" or "restarting".
const char* toString(UpdateState state);

// How to act on an update a check finds.
enum class InstallPolicy : uint8_t
{
    // Only raise onUpdateAvailable; installing waits for install().
    Notify,
    // Install it, once the install condition (if any) allows.
    Install,
};

// The update a Manifest offers, for onUpdateAvailable and friends.
struct UpdateInfo
{
    std::string version;
    // Release notes as plain text; may be empty.
    std::string notes;
    // Bytes.
    size_t size = 0;
};

// Passed to onUpdateProgress listeners.
struct UpdateProgress
{
    // 0 to 100.
    uint8_t percent = 0;
};

// Passed to onUpdateFailed listeners.
struct UpdateError
{
    UpdateErrorCode code = UpdateErrorCode::None;
    // The HTTP status for HttpStatus, otherwise 0.
    int httpStatus = 0;
    // The version being checked for or installed; empty for a failed check.
    std::string version;
};

// How an install attempt ended. Pending: installed, waiting for the new firmware to confirm.
enum class UpdateOutcome : uint8_t
{
    None,
    Pending,
    Succeeded,
    Failed,
    RolledBack,
};

// "none", "pending", "succeeded", "failed" or "rolled-back".
const char* toString(UpdateOutcome outcome);

enum class UpdateMethod : uint8_t
{
    Manual,
    Automatic,
};

// "manual" or "automatic".
const char* toString(UpdateMethod method);

// The most recent install attempt, kept across restarts (F17.11).
struct UpdateRecord
{
    UpdateOutcome outcome = UpdateOutcome::None;
    std::string fromVersion;
    std::string toVersion;
    UpdateMethod method = UpdateMethod::Manual;
    UpdateErrorCode error = UpdateErrorCode::None;
    // Seconds since 1970-01-01 UTC when Network Time was synchronised, else 0.
    int64_t epoch = 0;
};

}  // namespace espmgr
