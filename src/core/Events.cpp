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

void Events::raiseSettingChanged(const Setting& setting)
{
    pending_.push_back([this, &setting] { settingChanged.call(setting); });
}

void Events::raiseTimeSynced(TimeSyncInfo info)
{
    pending_.push_back([this, info] { timeSynced.call(info); });
}

void Events::raiseTimeSyncFailed()
{
    pending_.push_back([this] { timeSyncFailed.call(); });
}

void Events::raiseUpdateAvailable(UpdateInfo info)
{
    pending_.push_back([this, info = std::move(info)] { updateAvailable.call(info); });
}

void Events::raiseUpdateStarted(UpdateInfo info)
{
    pending_.push_back([this, info = std::move(info)] { updateStarted.call(info); });
}

void Events::raiseUpdateProgress(UpdateProgress progress)
{
    pending_.push_back([this, progress] { updateProgress.call(progress); });
}

void Events::raiseUpdateCompleted(UpdateInfo info)
{
    pending_.push_back([this, info = std::move(info)] { updateCompleted.call(info); });
}

void Events::raiseUpdateFailed(UpdateError error)
{
    pending_.push_back([this, error = std::move(error)] { updateFailed.call(error); });
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
