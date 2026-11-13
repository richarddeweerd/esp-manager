#pragma once

#include "core/UpdateTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace espmgr
{

// An HTTPS request to the Update Server.
struct UpdateRequest
{
    std::string url;
    // Extra headers, such as an Authorization token. Secrets: never log them.
    std::vector<std::pair<std::string, std::string>> headers;
    // PEM root certificate to trust instead of the built-in bundle; nullptr for the bundle.
    const char* rootCa = nullptr;
};

// The firmware update hardware (EP17): HTTPS, the OTA slots and the bootloader's rollback.
// fetch() and download() return at once and do the work in the background; the core polls
// status() from process(). Only one job runs at a time.
class UpdateDriver
{
public:
    using Sha256 = std::array<uint8_t, 32>;

    enum class JobState : uint8_t
    {
        Idle,
        Running,
        Done,
        Failed,
    };

    struct Status
    {
        JobState state = JobState::Idle;
        // When Failed: ServerUnreachable, HttpStatus, DownloadFailed, WriteFailed or
        // NoOtaPartition.
        UpdateErrorCode error = UpdateErrorCode::None;
        int httpStatus = 0;
        // Bytes received so far.
        size_t bytes = 0;
    };

    virtual ~UpdateDriver() = default;

    // Starts fetching `request` as text of at most `maxBytes`; more fails the job
    // (DownloadFailed). False if a job is running.
    virtual bool fetch(const UpdateRequest& request, size_t maxBytes) = 0;
    // Starts streaming `request` into the other OTA slot, computing its SHA-256. `size` is what
    // the Manifest promises; more bytes fail the job. False if a job is running.
    virtual bool download(const UpdateRequest& request, size_t size) = 0;
    virtual Status status() const = 0;
    // After a fetch is Done: what was received.
    virtual std::string body() const = 0;
    // After a download is Done: the SHA-256 of what was written.
    virtual Sha256 sha256() const = 0;
    // Stops the running job; a download leaves the boot image as it was.
    virtual void abort() = 0;

    // Whether `signature` (DER-encoded ECDSA P-256) signs `hash` for the PEM `publicKey`.
    virtual bool verifySignature(const std::string& publicKey, const Sha256& hash,
                                 const std::vector<uint8_t>& signature) = 0;
    // Makes the downloaded image the one the bootloader starts next. False if it can't.
    virtual bool activate() = 0;

    // Bytes the other OTA slot holds; 0 if there's no second slot.
    virtual size_t slotSize() const = 0;
    // Whether the running firmware is pending: new, and not confirmed yet.
    virtual bool pendingConfirmation() const = 0;
    // Confirms the running firmware, so the bootloader no longer rolls it back.
    virtual void confirm() = 0;
};

}  // namespace espmgr
