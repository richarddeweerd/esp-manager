#pragma once

#include "core/TimeDriver.h"

#include <string>

namespace espmgr
{

// The ESP-IDF SNTP client (esp_sntp) and the C library's local time.
class Esp32TimeDriver : public TimeDriver
{
public:
    void start(const std::vector<std::string>& servers, uint32_t intervalMs, bool useDhcp) override;
    void restart() override;
    void stop() override;
    void setTimezone(const std::string& posix) override;
    int64_t nowEpoch() const override;
    std::string localTime(int64_t epoch) const override;
    bool takeSynced() override;
    std::string answeringServer() const override;

private:
    // esp_sntp keeps the pointers it's given, so the names live here.
    std::string servers_[kMaxServers];
};

}  // namespace espmgr
