#pragma once

#include "core/UpdateDriver.h"

namespace espmgr
{

// An UpdateDriver that records what the core asks for. Jobs stay Running until the test calls
// finishFetch(), receive(), finishDownload() or fail().
class FakeUpdateDriver : public UpdateDriver
{
public:
    bool fetch(const UpdateRequest& request, size_t maxBytes) override
    {
        if (job.state == JobState::Running)
        {
            return false;
        }
        requests.push_back(request);
        fetchMax = maxBytes;
        job = Status{JobState::Running, UpdateErrorCode::None, 0, 0};
        downloading = false;
        ++fetches;
        return true;
    }

    bool download(const UpdateRequest& request, size_t size) override
    {
        if (job.state == JobState::Running)
        {
            return false;
        }
        requests.push_back(request);
        downloadSize = size;
        job = Status{JobState::Running, UpdateErrorCode::None, 0, 0};
        downloading = true;
        ++downloads;
        return true;
    }

    Status status() const override
    {
        return job;
    }

    std::string body() const override
    {
        return received;
    }

    Sha256 sha256() const override
    {
        return hash;
    }

    void abort() override
    {
        if (job.state == JobState::Running)
        {
            job.state = JobState::Idle;
        }
        ++aborts;
    }

    bool verifySignature(const std::string& publicKey, const Sha256& digest,
                         const std::vector<uint8_t>& signature) override
    {
        ++verifications;
        verifiedKey = publicKey;
        verifiedSignature = signature;
        return signatureValid && digest == hash;
    }

    bool activate() override
    {
        ++activations;
        return activateSucceeds;
    }

    size_t slotSize() const override
    {
        return slot;
    }

    bool pendingConfirmation() const override
    {
        return pending;
    }

    void confirm() override
    {
        pending = false;
        ++confirms;
    }

    // Test helpers.
    void finishFetch(const std::string& text)
    {
        received = text;
        job.state = JobState::Done;
        job.httpStatus = 200;
        job.bytes = text.size();
    }

    void receive(size_t bytes)
    {
        job.bytes = bytes;
    }

    void finishDownload(size_t bytes, const Sha256& digest)
    {
        job.state = JobState::Done;
        job.httpStatus = 200;
        job.bytes = bytes;
        hash = digest;
    }

    void fail(UpdateErrorCode error, int httpStatus = 0)
    {
        job.state = JobState::Failed;
        job.error = error;
        job.httpStatus = httpStatus;
    }

    Status job;
    bool downloading = false;
    std::vector<UpdateRequest> requests;
    size_t fetchMax = 0;
    size_t downloadSize = 0;
    std::string received;
    Sha256 hash{};
    int fetches = 0;
    int downloads = 0;
    int aborts = 0;
    int verifications = 0;
    std::string verifiedKey;
    std::vector<uint8_t> verifiedSignature;
    bool signatureValid = true;
    int activations = 0;
    bool activateSucceeds = true;
    size_t slot = 1310720;
    bool pending = false;
    int confirms = 0;
};

}  // namespace espmgr
