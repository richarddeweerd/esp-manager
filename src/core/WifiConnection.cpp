#include "core/WifiConnection.h"

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
        const StationStatus status = wifi_.status();
        if (status == StationStatus::Connected)
        {
            const ConnectedInfo info{ssid_, wifi_.localIp(), wifi_.channel()};
            log_.info("wifi", "connected to %s as %s, channel %u", ssid_.c_str(),
                      info.ip.toString().c_str(), static_cast<unsigned>(info.channel));
            setState(ConnectionState::Connected);
            events_.raiseConnected(info);
        }
        else if (status == StationStatus::Disconnected)
        {
            const FailureReason reason = wifi_.lastFailure();
            attemptFailed(reason == FailureReason::None ? FailureReason::Other : reason);
        }
        else if (clock_.nowMs() - attemptStartedMs_ >= kAttemptTimeoutMs)
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
            log_.warn("wifi", "lost the connection to %s", ssid_.c_str());
            retryAtMs_ = clock_.nowMs() + kRetryDelayMs;
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

void WifiConnection::startAttempt()
{
    const SavedNetwork network = networks_.list().front();
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
    attemptStartedMs_ = clock_.nowMs();
    setState(ConnectionState::Connecting);
}

void WifiConnection::attemptFailed(FailureReason reason)
{
    log_.warn("wifi", "could not connect to %s (reason %u), retrying in %u ms", ssid_.c_str(),
              static_cast<unsigned>(reason), static_cast<unsigned>(kRetryDelayMs));
    retryAtMs_ = clock_.nowMs() + kRetryDelayMs;
    setState(ConnectionState::Waiting);
    events_.raiseConnectFailed(ssid_, reason);
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
