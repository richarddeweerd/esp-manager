#pragma once

#include "core/Clock.h"
#include "core/Logger.h"
#include "core/WifiDriver.h"

#include <cstdint>
#include <vector>

namespace espmgr
{

// Runs the Wi-Fi scans for every part that needs one: the connection attempts and the
// Provisioning Portal. The radio does one scan at a time, so a part that asks while a scan is
// running shares that scan instead of starting another.
class NetworkScanner
{
public:
    // A scan that hasn't finished by then counts as finished without results.
    static constexpr uint64_t kScanTimeoutMs = 15000;

    NetworkScanner(WifiDriver& wifi, Clock& clock, Logger& log)
        : wifi_(wifi), clock_(clock), log_(log)
    {
    }

    NetworkScanner(const NetworkScanner&) = delete;
    NetworkScanner& operator=(const NetworkScanner&) = delete;

    // Starts a scan, or joins the one that's running. Returns false if no scan is running
    // afterwards, because the radio couldn't start one.
    bool start();

    // Polls the running scan. Called from process(), before the parts that use the results.
    void process();

    bool busy() const
    {
        return busy_;
    }

    // What the last finished scan found, as the radio reported it: an SSID seen from several
    // access points appears once for each.
    const std::vector<ScanResult>& results() const
    {
        return results_;
    }

    // When the last scan finished; 0 before the first.
    uint64_t finishedAtMs() const
    {
        return finishedAtMs_;
    }

private:
    WifiDriver& wifi_;
    Clock& clock_;
    Logger& log_;

    bool busy_ = false;
    uint64_t startedAtMs_ = 0;
    uint64_t finishedAtMs_ = 0;
    std::vector<ScanResult> results_;
};

}  // namespace espmgr
