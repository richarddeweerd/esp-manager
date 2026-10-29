#pragma once

#include "core/Clock.h"
#include "core/Events.h"
#include "core/Logger.h"
#include "core/NetworkScanner.h"
#include "core/SavedNetworks.h"
#include "core/WifiDriver.h"

#include <cstdint>
#include <string>
#include <vector>

namespace espmgr
{

// Connects the station to a Saved Network and keeps it connected (EP01, EP07). Driven from
// process(), it polls the WifiDriver and never waits:
//
//   Idle ──Saved Network──▶ Connecting ──connected──▶ Connected
//                            ▲      │ failed or timed out   │ connection lost
//                            │      ▼                        │
//                            └── Waiting ◀───────────────────┘
//
// Each attempt scans first and joins the best reachable Saved Network. Waiting lasts longer
// after each failure in a row (exponential backoff with random variation), and goes back to
// Idle once there's no Saved Network.
//
// A trial (tryNetwork()) is one attempt at a network that isn't saved, for the Provisioning
// Portal to test what the Owner entered. It goes through the same states; afterwards the
// connection carries on with the Saved Networks.
class WifiConnection
{
public:
    enum class Trial
    {
        None,
        Running,
        Connected,
        Failed,
    };

    // How long one attempt, scan included, may take before it counts as failed.
    static constexpr uint64_t kAttemptTimeoutMs = 20000;
    static constexpr uint32_t kDefaultMinRetryDelayMs = 1000;
    static constexpr uint32_t kDefaultMaxRetryDelayMs = 5 * 60 * 1000;

    WifiConnection(WifiDriver& wifi, Clock& clock, NetworkScanner& scanner,
                   const SavedNetworks& networks, Events& events, Logger& log)
        : wifi_(wifi), clock_(clock), scanner_(scanner), networks_(networks), events_(events),
          log_(log)
    {
    }

    WifiConnection(const WifiConnection&) = delete;
    WifiConnection& operator=(const WifiConnection&) = delete;

    // Allows process() to start connecting. Returns immediately.
    void begin()
    {
        started_ = true;
    }

    void process();

    bool started() const
    {
        return started_;
    }

    ConnectionState state() const
    {
        return state_;
    }

    // The network being tried or connected to; empty while Idle.
    std::string ssid() const
    {
        return state_ == ConnectionState::Idle ? std::string() : ssid_;
    }

    // Why the most recent attempt failed or the connection was lost; None until that happens.
    // Kept after a later successful connection.
    FailureReason lastFailure() const
    {
        return lastFailure_;
    }

    // Stops what the connection is doing and makes one attempt at `network`, without a scan and
    // without saving it. trial() tells how it went.
    void tryNetwork(const SavedNetwork& network);

    Trial trial() const
    {
        return trial_;
    }

    // Why the trial failed.
    FailureReason trialFailure() const
    {
        return trialFailure_;
    }

    // Forgets a finished trial's outcome.
    void endTrial()
    {
        if (trial_ != Trial::Running)
        {
            trial_ = Trial::None;
        }
    }

    // The wait after the first failure, doubling after each further one up to `maxMs`. Returns
    // false, changing nothing, unless 0 < minMs <= maxMs.
    bool setRetryDelays(uint32_t minMs, uint32_t maxMs);

    // The base delay before the next retry after `failures` failures in a row; the actual wait
    // is between 75 % and 100 % of it, but never below the minimum.
    uint64_t baseRetryDelay(uint32_t failures) const;

    // The best reachable Saved Network: the lowest priority number among those in the scan
    // results, the strongest signal breaking ties. If none was found, the first Saved Network by
    // priority, since hidden networks don't show up in a scan.
    static SavedNetwork choose(const std::vector<SavedNetwork>& networks,
                               const std::vector<ScanResult>& scan);

private:
    void startAttempt();
    void connectTo(const SavedNetwork& network);
    void attemptFailed(FailureReason reason);
    void scheduleRetry();
    void setState(ConnectionState next);

    WifiDriver& wifi_;
    Clock& clock_;
    NetworkScanner& scanner_;
    const SavedNetworks& networks_;
    Events& events_;
    Logger& log_;

    bool started_ = false;
    ConnectionState state_ = ConnectionState::Idle;
    FailureReason lastFailure_ = FailureReason::None;
    std::string ssid_;  // of the current or last attempt
    bool scanning_ = false;
    uint64_t attemptStartedMs_ = 0;
    uint64_t retryAtMs_ = 0;
    uint32_t failuresInARow_ = 0;
    uint32_t minRetryDelayMs_ = kDefaultMinRetryDelayMs;
    uint32_t maxRetryDelayMs_ = kDefaultMaxRetryDelayMs;
    Trial trial_ = Trial::None;
    FailureReason trialFailure_ = FailureReason::None;
};

}  // namespace espmgr
