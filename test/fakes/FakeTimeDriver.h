#pragma once

#include "core/TimeDriver.h"

#include <cstdio>

namespace espmgr
{

// A TimeDriver that records what the core asks for and syncs only when the test calls
// answer(). localTime() writes the epoch in UTC, followed by the time zone it was given.
class FakeTimeDriver : public TimeDriver
{
public:
    void start(const std::vector<std::string>& serverNames, uint32_t interval, bool dhcp) override
    {
        running = true;
        servers = serverNames;
        intervalMs = interval;
        useDhcp = dhcp;
        ++starts;
    }

    void restart() override
    {
        ++restarts;
    }

    void stop() override
    {
        running = false;
        ++stops;
    }

    void setTimezone(const std::string& posix) override
    {
        timezone = posix;
        ++timezoneSets;
    }

    int64_t nowEpoch() const override
    {
        return epoch;
    }

    std::string localTime(int64_t value) const override
    {
        char text[64];
        std::snprintf(text, sizeof(text), "%lld %s", static_cast<long long>(value),
                      timezone.c_str());
        return text;
    }

    bool takeSynced() override
    {
        const bool was = synced;
        synced = false;
        return was;
    }

    std::string answeringServer() const override
    {
        return server;
    }

    // Test helper: a server answers, setting the clock to `newEpoch`.
    void answer(int64_t newEpoch, const std::string& from = "pool.ntp.org")
    {
        epoch = newEpoch;
        server = from;
        synced = true;
    }

    bool running = false;
    std::vector<std::string> servers;
    uint32_t intervalMs = 0;
    bool useDhcp = false;
    int starts = 0;
    int restarts = 0;
    int stops = 0;
    std::string timezone;
    int timezoneSets = 0;
    int64_t epoch = 0;
    bool synced = false;
    std::string server;
};

}  // namespace espmgr
