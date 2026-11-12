#include "platform/esp32/Esp32TimeDriver.h"

#include <esp_sntp.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <sys/time.h>

namespace espmgr
{

namespace
{

// Set from the lwIP task, read from process().
std::atomic<bool> syncedFlag{false};

void onSynced(struct timeval*)
{
    syncedFlag.store(true);
}

}  // namespace

void Esp32TimeDriver::start(const std::vector<std::string>& servers, uint32_t intervalMs,
                            bool useDhcp)
{
    if (esp_sntp_enabled())
    {
        esp_sntp_stop();
    }
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_servermode_dhcp(useDhcp);
    for (size_t i = 0; i < kMaxServers; ++i)
    {
        servers_[i] = i < servers.size() ? servers[i] : std::string();
        esp_sntp_setservername(static_cast<u8_t>(i),
                               servers_[i].empty() ? nullptr : servers_[i].c_str());
    }
    sntp_set_sync_interval(intervalMs);
    sntp_set_time_sync_notification_cb(onSynced);
    esp_sntp_init();
}

void Esp32TimeDriver::restart()
{
    if (esp_sntp_enabled())
    {
        sntp_restart();
    }
}

void Esp32TimeDriver::stop()
{
    if (esp_sntp_enabled())
    {
        esp_sntp_stop();
    }
}

void Esp32TimeDriver::setTimezone(const std::string& posix)
{
    setenv("TZ", posix.c_str(), 1);
    tzset();
}

int64_t Esp32TimeDriver::nowEpoch() const
{
    return static_cast<int64_t>(time(nullptr));
}

std::string Esp32TimeDriver::localTime(int64_t epoch) const
{
    const time_t value = static_cast<time_t>(epoch);
    struct tm local;
    localtime_r(&value, &local);
    char text[32];
    if (strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%S%z", &local) == 0)
    {
        return std::string();
    }
    // "+0100" to "+01:00".
    std::string result(text);
    if (result.size() >= 5)
    {
        result.insert(result.size() - 2, ":");
    }
    return result;
}

bool Esp32TimeDriver::takeSynced()
{
    return syncedFlag.exchange(false);
}

std::string Esp32TimeDriver::answeringServer() const
{
    for (size_t i = 0; i < kMaxServers; ++i)
    {
        if (esp_sntp_getreachability(static_cast<uint8_t>(i)) != 0)
        {
            const char* name = esp_sntp_getservername(static_cast<uint8_t>(i));
            if (name != nullptr)
            {
                return name;
            }
        }
    }
    return std::string();
}

}  // namespace espmgr
