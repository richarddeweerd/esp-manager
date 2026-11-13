#include "platform/esp32/Esp32UpdateDriver.h"

#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_ota_ops.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>

#include <cstring>
#include <memory>

namespace espmgr
{

namespace
{

constexpr uint32_t kTaskStack = 8192;
constexpr int kTimeoutMs = 15000;
constexpr int kMaxRedirects = 3;
constexpr size_t kChunk = 4096;

}  // namespace

bool Esp32UpdateDriver::fetch(const UpdateRequest& request, size_t maxBytes)
{
    return start(request, maxBytes, false);
}

bool Esp32UpdateDriver::download(const UpdateRequest& request, size_t size)
{
    return start(request, size, true);
}

bool Esp32UpdateDriver::start(const UpdateRequest& request, size_t limit, bool download)
{
    if (state_.load() == JobState::Running)
    {
        return false;
    }
    request_ = request;
    limit_ = limit;
    download_ = download;
    body_.clear();
    hash_ = Sha256{};
    if (download)
    {
        downloaded_ = false;
    }
    error_.store(UpdateErrorCode::None);
    httpStatus_.store(0);
    bytes_.store(0);
    abort_.store(false);
    state_.store(JobState::Running);
    if (xTaskCreate(runTask, "espmgr-ota", kTaskStack, this, tskIDLE_PRIORITY + 1, nullptr) !=
        pdPASS)
    {
        finish(JobState::Failed, UpdateErrorCode::LowMemory);
    }
    return true;
}

void Esp32UpdateDriver::runTask(void* self)
{
    static_cast<Esp32UpdateDriver*>(self)->run();
    vTaskDelete(nullptr);
}

void Esp32UpdateDriver::finish(JobState state, UpdateErrorCode error)
{
    error_.store(error);
    // Last, so the core sees the body, hash and error once it reads Done or Failed.
    state_.store(state);
}

void Esp32UpdateDriver::run()
{
    esp_http_client_config_t config = {};
    config.url = request_.url.c_str();
    config.timeout_ms = kTimeoutMs;
    config.buffer_size = 2048;
    config.disable_auto_redirect = true;
    if (request_.rootCa != nullptr)
    {
        config.cert_pem = request_.rootCa;
    }
    else
    {
        config.crt_bundle_attach = esp_crt_bundle_attach;
    }
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr)
    {
        finish(JobState::Failed, UpdateErrorCode::LowMemory);
        return;
    }
    for (const auto& header : request_.headers)
    {
        esp_http_client_set_header(client, header.first.c_str(), header.second.c_str());
    }

    int64_t length = -1;
    int status = 0;
    for (int redirects = 0;; ++redirects)
    {
        if (esp_http_client_open(client, 0) != ESP_OK)
        {
            esp_http_client_cleanup(client);
            finish(JobState::Failed, UpdateErrorCode::ServerUnreachable);
            return;
        }
        length = esp_http_client_fetch_headers(client);
        status = esp_http_client_get_status_code(client);
        const bool redirect =
            status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
        if (!redirect || redirects >= kMaxRedirects)
        {
            break;
        }
        esp_http_client_set_redirection(client);
        esp_http_client_close(client);
        char url[300];
        // Updates only ever come over HTTPS, redirects included.
        if (esp_http_client_get_url(client, url, sizeof(url)) != ESP_OK ||
            std::strncmp(url, "https://", 8) != 0)
        {
            esp_http_client_cleanup(client);
            finish(JobState::Failed, UpdateErrorCode::ServerUnreachable);
            return;
        }
    }
    httpStatus_.store(status);
    if (status != 200)
    {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        finish(JobState::Failed, UpdateErrorCode::HttpStatus);
        return;
    }
    if (length > 0 && static_cast<uint64_t>(length) > limit_)
    {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        finish(JobState::Failed,
               download_ ? UpdateErrorCode::SizeMismatch : UpdateErrorCode::ManifestInvalid);
        return;
    }

    esp_ota_handle_t ota = 0;
    mbedtls_sha256_context sha;
    if (download_)
    {
        partition_ = esp_ota_get_next_update_partition(nullptr);
        if (partition_ == nullptr)
        {
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            finish(JobState::Failed, UpdateErrorCode::NoOtaPartition);
            return;
        }
        if (esp_ota_begin(partition_, limit_, &ota) != ESP_OK)
        {
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            finish(JobState::Failed, UpdateErrorCode::WriteFailed);
            return;
        }
        mbedtls_sha256_init(&sha);
        mbedtls_sha256_starts(&sha, 0);
    }

    std::unique_ptr<char[]> buffer(new (std::nothrow) char[kChunk]);
    UpdateErrorCode error = buffer ? UpdateErrorCode::None : UpdateErrorCode::LowMemory;
    size_t total = 0;
    while (error == UpdateErrorCode::None)
    {
        if (abort_.load())
        {
            error = UpdateErrorCode::DownloadFailed;
            break;
        }
        const int read = esp_http_client_read(client, buffer.get(), kChunk);
        if (read < 0)
        {
            error = UpdateErrorCode::DownloadFailed;
            break;
        }
        if (read == 0)
        {
            if (!esp_http_client_is_complete_data_received(client))
            {
                error = UpdateErrorCode::DownloadFailed;
            }
            break;
        }
        total += static_cast<size_t>(read);
        if (total > limit_)
        {
            error = download_ ? UpdateErrorCode::SizeMismatch : UpdateErrorCode::ManifestInvalid;
            break;
        }
        if (download_)
        {
            mbedtls_sha256_update(&sha, reinterpret_cast<const unsigned char*>(buffer.get()),
                                  static_cast<size_t>(read));
            if (esp_ota_write(ota, buffer.get(), static_cast<size_t>(read)) != ESP_OK)
            {
                error = UpdateErrorCode::WriteFailed;
                break;
            }
        }
        else
        {
            body_.append(buffer.get(), static_cast<size_t>(read));
        }
        bytes_.store(total);
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (download_)
    {
        mbedtls_sha256_finish(&sha, hash_.data());
        mbedtls_sha256_free(&sha);
        if (error != UpdateErrorCode::None)
        {
            esp_ota_abort(ota);
        }
        else if (esp_ota_end(ota) != ESP_OK)
        {
            // Not a valid app image.
            error = UpdateErrorCode::WriteFailed;
        }
        else
        {
            downloaded_ = true;
        }
    }
    finish(error == UpdateErrorCode::None ? JobState::Done : JobState::Failed, error);
}

UpdateDriver::Status Esp32UpdateDriver::status() const
{
    Status status;
    status.state = state_.load();
    status.error = error_.load();
    status.httpStatus = httpStatus_.load();
    status.bytes = bytes_.load();
    return status;
}

std::string Esp32UpdateDriver::body() const
{
    return state_.load() == JobState::Done ? body_ : std::string();
}

UpdateDriver::Sha256 Esp32UpdateDriver::sha256() const
{
    return hash_;
}

void Esp32UpdateDriver::abort()
{
    abort_.store(true);
}

bool Esp32UpdateDriver::verifySignature(const std::string& publicKey, const Sha256& hash,
                                        const std::vector<uint8_t>& signature)
{
    mbedtls_pk_context key;
    mbedtls_pk_init(&key);
    // A PEM key's length includes its terminating NUL.
    bool valid =
        mbedtls_pk_parse_public_key(&key, reinterpret_cast<const unsigned char*>(publicKey.c_str()),
                                    publicKey.size() + 1) == 0 &&
        mbedtls_pk_can_do(&key, MBEDTLS_PK_ECKEY) &&
        mbedtls_pk_verify(&key, MBEDTLS_MD_SHA256, hash.data(), hash.size(), signature.data(),
                          signature.size()) == 0;
    mbedtls_pk_free(&key);
    return valid;
}

bool Esp32UpdateDriver::activate()
{
    return downloaded_ && partition_ != nullptr && esp_ota_set_boot_partition(partition_) == ESP_OK;
}

size_t Esp32UpdateDriver::slotSize() const
{
    const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
    return next == nullptr ? 0 : next->size;
}

UpdateDriver& esp32UpdateDriver()
{
    static Esp32UpdateDriver driver;
    return driver;
}

}  // namespace espmgr
