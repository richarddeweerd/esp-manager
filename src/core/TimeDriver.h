#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace espmgr
{

// The platform's SNTP client and its local time (EP18). The client runs on its own and never
// blocks; the core asks takeSynced() from process() to hear about each successful sync.
class TimeDriver
{
public:
    // The platform's SNTP client takes at most this many servers.
    static constexpr size_t kMaxServers = 3;

    virtual ~TimeDriver() = default;

    // Starts syncing from `servers` (1 to kMaxServers names) every `intervalMs`, the first sync
    // at once. With `useDhcp`, a server the DHCP server hands out takes the first place. Starting
    // again replaces the earlier settings.
    virtual void start(const std::vector<std::string>& servers, uint32_t intervalMs,
                       bool useDhcp) = 0;
    // Starts a sync now, while started; the interval then counts from here.
    virtual void restart() = 0;
    // Stops syncing. The clock keeps running.
    virtual void stop() = 0;

    // Sets the rules for local time, a POSIX TZ string such as "CET-1CEST,M3.5.0,M10.5.0/3".
    virtual void setTimezone(const std::string& posix) = 0;

    // Seconds since 1970-01-01 UTC, as the clock has it now.
    virtual int64_t nowEpoch() const = 0;
    // `epoch` as local time, "2026-11-12T13:53:47+01:00".
    virtual std::string localTime(int64_t epoch) const = 0;

    // True once for each successful sync since the last call.
    virtual bool takeSynced() = 0;
    // A server that has answered recently; empty if none has.
    virtual std::string answeringServer() const = 0;
};

}  // namespace espmgr
