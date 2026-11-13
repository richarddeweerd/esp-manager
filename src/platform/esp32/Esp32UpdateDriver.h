#pragma once

#include "core/UpdateDriver.h"

#include <esp_partition.h>

#include <atomic>
#include <string>

namespace espmgr
{

// Firmware updates on the ESP32: esp_http_client over TLS (the certificate bundle or one root
// CA), esp_ota_* for the other OTA slot, mbedtls for SHA-256 and ECDSA. Each job runs in its own
// FreeRTOS task; process() reads its progress through atomics.
class Esp32UpdateDriver : public UpdateDriver
{
public:
    bool fetch(const UpdateRequest& request, size_t maxBytes) override;
    bool download(const UpdateRequest& request, size_t size) override;
    Status status() const override;
    std::string body() const override;
    Sha256 sha256() const override;
    void abort() override;
    bool verifySignature(const std::string& publicKey, const Sha256& hash,
                         const std::vector<uint8_t>& signature) override;
    bool activate() override;
    size_t slotSize() const override;

private:
    bool start(const UpdateRequest& request, size_t limit, bool download);
    static void runTask(void* self);
    void run();
    void finish(JobState state, UpdateErrorCode error);

    // Written by the caller before the task starts; read by the task.
    UpdateRequest request_;
    size_t limit_ = 0;
    bool download_ = false;
    // Written by the task before it publishes Done; read by the core after.
    std::string body_;
    Sha256 hash_{};
    const esp_partition_t* partition_ = nullptr;
    bool downloaded_ = false;

    std::atomic<JobState> state_{JobState::Idle};
    std::atomic<UpdateErrorCode> error_{UpdateErrorCode::None};
    std::atomic<int> httpStatus_{0};
    std::atomic<size_t> bytes_{0};
    std::atomic<bool> abort_{false};
};

// The driver esp.updates() attaches; created on first use, so sketches that never call
// esp.updates() don't link it.
UpdateDriver& esp32UpdateDriver();

}  // namespace espmgr
