#include "core/Events.h"

#include <utility>

namespace espmgr
{

void Events::raiseConnected(ConnectedInfo info)
{
    pending_.push_back([this, info = std::move(info)] { connected.call(info); });
}

void Events::raiseDisconnected(FailureReason reason)
{
    pending_.push_back([this, reason] { disconnected.call(reason); });
}

void Events::raiseConnectFailed(std::string ssid, FailureReason reason)
{
    pending_.push_back([this, ssid = std::move(ssid), reason]
                       { connectFailed.call(ssid, reason); });
}

void Events::raiseConnectionStateChanged(ConnectionState from, ConnectionState to)
{
    pending_.push_back([this, from, to] { connectionStateChanged.call(from, to); });
}

void Events::raisePortalStarted(std::string accessPointSsid)
{
    pending_.push_back([this, ssid = std::move(accessPointSsid)] { portalStarted.call(ssid); });
}

void Events::raisePortalStopped()
{
    pending_.push_back([this] { portalStopped.call(); });
}

void Events::raiseProvisioningCompleted(std::string ssid)
{
    pending_.push_back([this, ssid = std::move(ssid)] { provisioningCompleted.call(ssid); });
}

void Events::deliver()
{
    if (delivering_)
    {
        return;
    }
    delivering_ = true;
    size_t delivered = 0;
    while (!pending_.empty() && delivered < kMaxPerDelivery)
    {
        const std::function<void()> event = std::move(pending_.front());
        pending_.pop_front();
        event();
        ++delivered;
    }
    if (!pending_.empty())
    {
        log_.warn("events", "%u events left for the next process(); do listeners raise each other?",
                  static_cast<unsigned>(pending_.size()));
    }
    delivering_ = false;
}

}  // namespace espmgr
