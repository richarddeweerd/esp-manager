#pragma once

#include "core/Clock.h"
#include "core/Events.h"
#include "core/Logger.h"
#include "core/SavedNetworks.h"
#include "core/WifiDriver.h"

#include <cstdint>
#include <string>

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
// Waiting retries after kRetryDelayMs, or goes back to Idle once there's no Saved Network.
class WifiConnection
{
public:
    // How long one attempt may take before it counts as failed.
    static constexpr uint64_t kAttemptTimeoutMs = 20000;
    // How long Waiting lasts before the next attempt. #8 replaces it with backoff.
    static constexpr uint64_t kRetryDelayMs = 1000;

    WifiConnection(WifiDriver& wifi, const Clock& clock, const SavedNetworks& networks,
                   Events& events, Logger& log)
        : wifi_(wifi), clock_(clock), networks_(networks), events_(events), log_(log)
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

    ConnectionState state() const
    {
        return state_;
    }

private:
    void startAttempt();
    void attemptFailed(FailureReason reason);
    void setState(ConnectionState next);

    WifiDriver& wifi_;
    const Clock& clock_;
    const SavedNetworks& networks_;
    Events& events_;
    Logger& log_;

    bool started_ = false;
    ConnectionState state_ = ConnectionState::Idle;
    std::string ssid_;  // of the current or last attempt
    uint64_t attemptStartedMs_ = 0;
    uint64_t retryAtMs_ = 0;
};

}  // namespace espmgr
