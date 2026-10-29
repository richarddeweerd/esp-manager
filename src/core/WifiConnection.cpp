#include "core/WifiConnection.h"

#include <algorithm>

namespace espmgr
{

void WifiConnection::process()
{
    if (!started_)
    {
        return;
    }
    switch (state_)
    {
    case ConnectionState::Idle:
        if (networks_.count() > 0)
        {
            startAttempt();
        }
        break;

    case ConnectionState::Connecting:
    {
        const bool timedOut = clock_.nowMs() - attemptStartedMs_ >= kAttemptTimeoutMs;
        if (scanning_)
        {
            if (!scanner_.busy())
            {
                scanning_ = false;
                if (networks_.count() == 0)
                {
                    setState(ConnectionState::Idle);
                    break;
                }
                connectTo(choose(networks_.list(), scanner_.results()));
            }
            else if (timedOut)
            {
                scanning_ = false;
                attemptFailed(FailureReason::Timeout);
            }
            break;
        }
        const StationStatus status = wifi_.status();
        if (status == StationStatus::Connected)
        {
            const ConnectedInfo info{ssid_, wifi_.localIp(), wifi_.channel()};
            log_.info("wifi", "connected to %s as %s, channel %u", ssid_.c_str(),
                      info.ip.toString().c_str(), static_cast<unsigned>(info.channel));
            failuresInARow_ = 0;
            if (trial_ == Trial::Running)
            {
                trial_ = Trial::Connected;
            }
            setState(ConnectionState::Connected);
            events_.raiseConnected(info);
        }
        else if (status == StationStatus::Disconnected)
        {
            const FailureReason reason = wifi_.lastFailure();
            attemptFailed(reason == FailureReason::None ? FailureReason::Other : reason);
        }
        else if (timedOut)
        {
            wifi_.disconnect();
            attemptFailed(FailureReason::Timeout);
        }
        break;
    }

    case ConnectionState::Connected:
        if (wifi_.status() != StationStatus::Connected)
        {
            FailureReason reason = wifi_.lastFailure();
            if (reason == FailureReason::None)
            {
                reason = FailureReason::ConnectionLost;
            }
            lastFailure_ = reason;
            log_.warn("wifi", "lost the connection to %s (%s)", ssid_.c_str(), toString(reason));
            scheduleRetry();
            setState(ConnectionState::Waiting);
            events_.raiseDisconnected(reason);
        }
        break;

    case ConnectionState::Waiting:
        if (networks_.count() == 0)
        {
            setState(ConnectionState::Idle);
        }
        else if (clock_.nowMs() >= retryAtMs_)
        {
            startAttempt();
        }
        break;
    }
}

bool WifiConnection::setRetryDelays(uint32_t minMs, uint32_t maxMs)
{
    if (minMs == 0 || maxMs < minMs)
    {
        log_.warn("wifi", "ignoring retry delays %u..%u ms", static_cast<unsigned>(minMs),
                  static_cast<unsigned>(maxMs));
        return false;
    }
    minRetryDelayMs_ = minMs;
    maxRetryDelayMs_ = maxMs;
    return true;
}

uint64_t WifiConnection::baseRetryDelay(uint32_t failures) const
{
    uint64_t delay = minRetryDelayMs_;
    for (uint32_t i = 1; i < failures && delay < maxRetryDelayMs_; ++i)
    {
        delay *= 2;
    }
    return std::min<uint64_t>(delay, maxRetryDelayMs_);
}

SavedNetwork WifiConnection::choose(const std::vector<SavedNetwork>& networks,
                                    const std::vector<ScanResult>& scan)
{
    const SavedNetwork* best = nullptr;
    int bestRssi = 0;
    for (const SavedNetwork& network : networks)
    {
        // The same SSID can be seen from several access points; the strongest counts.
        bool seen = false;
        int rssi = -128;
        for (const ScanResult& result : scan)
        {
            if (result.ssid == network.ssid)
            {
                seen = true;
                rssi = std::max<int>(rssi, result.rssi);
            }
        }
        if (seen && (best == nullptr || network.priority < best->priority ||
                     (network.priority == best->priority && rssi > bestRssi)))
        {
            best = &network;
            bestRssi = rssi;
        }
    }
    if (best != nullptr)
    {
        return *best;
    }
    return *std::min_element(networks.begin(), networks.end(),
                             [](const SavedNetwork& a, const SavedNetwork& b)
                             { return a.priority < b.priority; });
}

void WifiConnection::tryNetwork(const SavedNetwork& network)
{
    if (state_ == ConnectionState::Connecting || state_ == ConnectionState::Connected)
    {
        wifi_.disconnect();
    }
    scanning_ = false;
    trial_ = Trial::Running;
    trialFailure_ = FailureReason::None;
    attemptStartedMs_ = clock_.nowMs();
    setState(ConnectionState::Connecting);
    connectTo(network);
}

void WifiConnection::startAttempt()
{
    attemptStartedMs_ = clock_.nowMs();
    setState(ConnectionState::Connecting);
    scanning_ = scanner_.start();
    if (!scanning_)
    {
        log_.debug("wifi", "scan didn't start; connecting without one");
        connectTo(choose(networks_.list(), {}));
    }
}

void WifiConnection::connectTo(const SavedNetwork& network)
{
    ssid_ = network.ssid;
    if (network.dhcp)
    {
        wifi_.useDhcp();
    }
    else
    {
        wifi_.useStaticIp(network.staticIp);
    }
    log_.info("wifi", "connecting to %s", ssid_.c_str());
    wifi_.connect(network.ssid, network.password);
}

void WifiConnection::attemptFailed(FailureReason reason)
{
    lastFailure_ = reason;
    if (trial_ == Trial::Running)
    {
        // Not a Saved Network, so it isn't a failure in a row: carry on with the Saved Networks
        // after the shortest wait.
        trial_ = Trial::Failed;
        trialFailure_ = reason;
        log_.warn("wifi", "could not connect to %s (%s)", ssid_.c_str(), toString(reason));
        retryAtMs_ = clock_.nowMs() + minRetryDelayMs_;
        setState(networks_.count() > 0 ? ConnectionState::Waiting : ConnectionState::Idle);
        return;
    }
    scheduleRetry();
    log_.warn("wifi", "could not connect to %s (%s), retrying in %u ms", ssid_.c_str(),
              toString(reason), static_cast<unsigned>(retryAtMs_ - clock_.nowMs()));
    setState(ConnectionState::Waiting);
    events_.raiseConnectFailed(ssid_, reason);
}

void WifiConnection::scheduleRetry()
{
    ++failuresInARow_;
    const uint64_t base = baseRetryDelay(failuresInARow_);
    // Some randomness, so devices that lost the same router don't all come back at once.
    const uint64_t spread = base / 4;
    uint64_t delay = base - clock_.random(static_cast<uint32_t>(spread) + 1);
    delay = std::max<uint64_t>(delay, minRetryDelayMs_);
    retryAtMs_ = clock_.nowMs() + delay;
}

void WifiConnection::setState(ConnectionState next)
{
    if (next == state_)
    {
        return;
    }
    const ConnectionState previous = state_;
    state_ = next;
    events_.raiseConnectionStateChanged(previous, next);
}

}  // namespace espmgr
