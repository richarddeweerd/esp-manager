#pragma once

#include "core/Clock.h"
#include "core/Events.h"
#include "core/Logger.h"
#include "core/Settings.h"
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

    // The keys of the Time Section's Settings; a sketch can't declare Settings with these.
    static constexpr const char* kZoneKey = "time-zone";
    static constexpr const char* kDstKey = "time-dst";
    static constexpr const char* kPosixKey = "time-posix";
    static constexpr const char* kServerKey = "time-server";
    static constexpr const char* kFallbackKey = "time-fallback";
    // The Section's title.
    static constexpr const char* kSectionTitle = "Time";

    NetworkTime(TimeDriver& driver, Clock& clock, const WifiConnection& connection,
                Settings& settings, Events& events, Logger& log);

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
    // The POSIX string in use: the Owner's, or else the Developer's.
    std::string timezone() const;

    // Lets the Owner pick the time zone in the web pages: a "Time" Section of Settings with a list
    // of named zones (the phone's own preselected) and a "Daylight saving time" checkbox for zones
    // that have it. Call it before begin(), like any Setting. The Owner's zone overrides
    // setTimezone(); a Settings Reset goes back to it. The Section shows in the Provisioning
    // Portal, and in the Management Interface after editableInManagement().
    NetworkTime& ownerCanEdit();
    // Also lets the Owner change the first two time servers; an empty field keeps the
    // Developer's. Implies ownerCanEdit().
    NetworkTime& ownerCanEditServers();
    // Makes the Time Section editable in the Management Interface too.
    NetworkTime& editableInManagement();

    // The Owner's named zone, such as "Europe/Amsterdam", and whether it keeps daylight saving
    // time; empty and true while the Owner hasn't picked one.
    std::string zone() const;
    bool dst() const;

    // How often to sync again: 1 hour by default, at least 15 s; returns false, changing
    // nothing, below that.
    bool setSyncInterval(uint32_t ms);
    uint32_t syncInterval() const
    {
        return intervalMs_;
    }

    // Starts a sync now, if enabled and connected, and returns true; otherwise the next
    // connection starts one, and it returns false.
    bool sync();

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
    // Applies a change to one of the Time Section's Settings.
    void settingChanged(const Setting& setting);

private:
    bool validName(const std::string& name, const char* what) const;
    void startDriver();
    // The servers in use: the Developer's, with the Owner's in the first two places.
    std::vector<std::string> activeServers() const;
    bool ownerZone() const;
    void applyTimezone();

    TimeDriver& driver_;
    Clock& clock_;
    const WifiConnection& connection_;
    Settings& settings_;
    Events& events_;
    Logger& log_;

    std::vector<std::string> servers_;
    bool defaultFallbacks_ = true;
    bool useDhcp_ = false;
    std::string timezone_ = kDefaultTimezone;
    uint32_t intervalMs_ = kDefaultSyncIntervalMs;

    // The Time Section's Settings, once declared.
    TextSetting* zoneSetting_ = nullptr;
    CheckboxSetting* dstSetting_ = nullptr;
    TextSetting* posixSetting_ = nullptr;
    TextSetting* serverSetting_ = nullptr;
    TextSetting* fallbackSetting_ = nullptr;
    bool editableInManagement_ = false;

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
