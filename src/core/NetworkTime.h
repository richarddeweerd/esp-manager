#pragma once

#include "core/Clock.h"
#include "core/Events.h"
#include "core/Logger.h"
#include "core/TimeDriver.h"
#include "core/WifiConnection.h"

#include <cstdint>
#include <string>
#include <vector>

namespace espmgr
{

// Network Time (EP18): the device's date and time from NTP servers. Off until enable(); it
// never blocks process() and never touches the Wi-Fi connection. The platform's SNTP client
// does the work in the background; this part starts it once the station is connected, watches
// for answers from process(), and raises onTimeSynced and onTimeSyncFailed.
class NetworkTime
{
public:
    // The default servers, in the order they're tried.
    static constexpr const char* kDefaultServers[] = {"pool.ntp.org", "time.google.com",
                                                      "time.cloudflare.com"};
    static constexpr const char* kDefaultTimezone = "UTC0";
    static constexpr size_t kMaxNameLength = 64;
    static constexpr uint32_t kDefaultSyncIntervalMs = 60UL * 60 * 1000;
    // The platform's SNTP client refuses anything shorter.
    static constexpr uint32_t kMinSyncIntervalMs = 15000;
    // An attempt without an answer for this long counts as failed.
    static constexpr uint32_t kAnswerTimeoutMs = 30000;

    NetworkTime(TimeDriver& driver, Clock& clock, const WifiConnection& connection, Events& events,
                Logger& log);

    NetworkTime(const NetworkTime&) = delete;
    NetworkTime& operator=(const NetworkTime&) = delete;

    // Turns syncing on: the first sync starts when the station connects, or at once if it's
    // connected already. May be called at any time. Returns *this, so calls can be chained.
    NetworkTime& enable();
    // Stops syncing; the clock keeps running and synchronised() stays as it is.
    NetworkTime& disable();
    bool enabled() const
    {
        return enabled_;
    }

    // Replaces the first server ("pool.ntp.org" by default). A name is 1 to 64 printable
    // characters without spaces; a refused one changes nothing, is logged at Error and returns
    // false.
    bool setServer(const std::string& name);
    // Adds a server to try when the ones before it don't answer. The first call replaces the
    // default fallbacks ("time.google.com", "time.cloudflare.com"). There are three servers at
    // most, which the platform allows; a fourth is refused like an invalid name.
    bool addFallbackServer(const std::string& name);
    const std::vector<std::string>& servers() const
    {
        return servers_;
    }

    // Lets a server the DHCP server hands out take the first place. Off by default.
    NetworkTime& useDhcpServer(bool use);

    // The rules for local time, a POSIX TZ string such as "CET-1CEST,M3.5.0,M10.5.0/3"
    // (Amsterdam). "UTC0" by default. Copied and applied at once, also while Network Time is
    // off. 1 to 64 printable characters; a refused one changes nothing, is logged at Error and
    // returns false. The string isn't checked further: one the C library can't read gives UTC.
    bool setTimezone(const std::string& posix);
    // The POSIX string in use.
    const std::string& timezone() const
    {
        return timezone_;
    }

    // How often to sync again: 1 hour by default, at least 15 s; returns false, changing
    // nothing, below that.
    bool setSyncInterval(uint32_t ms);
    uint32_t syncInterval() const
    {
        return intervalMs_;
    }

    // Starts a sync now, if enabled and connected; otherwise the next connection starts one.
    void sync();

    // True from the first successful sync since start-up: the clock has real time.
    bool synchronised() const
    {
        return synchronised_;
    }
    // Seconds since 1970-01-01 UTC of the last successful sync; 0 before the first.
    int64_t lastSync() const
    {
        return lastSync_;
    }
    // Seconds since 1970-01-01 UTC now; only meaningful once synchronised().
    int64_t now() const;
    // Local time now as "2026-11-12T13:53:47+01:00"; empty until synchronised().
    std::string localTime() const;
    // The server that answered last; empty before the first answer.
    std::string server() const;

    // Used by EspManager.
    void begin();
    void process();

private:
    bool validName(const std::string& name, const char* what) const;
    void startDriver();

    TimeDriver& driver_;
    Clock& clock_;
    const WifiConnection& connection_;
    Events& events_;
    Logger& log_;

    std::vector<std::string> servers_;
    bool defaultFallbacks_ = true;
    bool useDhcp_ = false;
    std::string timezone_ = kDefaultTimezone;
    bool timezoneSet_ = false;
    uint32_t intervalMs_ = kDefaultSyncIntervalMs;

    bool enabled_ = false;
    bool begun_ = false;
    bool started_ = false;
    bool wasConnected_ = false;
    bool synchronised_ = false;
    int64_t lastSync_ = 0;
    // While watching, an answer is due by this time (Clock ms); a failure is raised after it.
    bool watching_ = false;
    uint64_t answerDueMs_ = 0;
};

}  // namespace espmgr
