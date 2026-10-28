#pragma once

#include "core/Ipv4Address.h"
#include "core/Logger.h"
#include "core/WifiDriver.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>

namespace espmgr
{

// Where the Wi-Fi connection stands. Waiting means a retry is scheduled after a failure.
enum class ConnectionState : uint8_t
{
    Idle,
    Connecting,
    Connected,
    Waiting
};

// Whether the Provisioning Portal is running.
enum class PortalState : uint8_t
{
    Off,
    Active
};

// Passed to onConnected listeners.
struct ConnectedInfo
{
    std::string ssid;
    Ipv4Address ip;
    uint8_t channel = 0;
};

// Listeners for one event. Listeners added while the event is being delivered get the next
// one, not this one. They can't be removed; they live as long as the EspManager.
template <typename... Args> class Listeners
{
public:
    using Callback = std::function<void(Args...)>;

    void add(Callback callback)
    {
        if (callback)
        {
            callbacks_.push_back(std::move(callback));
        }
    }

    void call(Args... args) const
    {
        // A deque keeps existing elements in place when a listener adds another.
        const size_t count = callbacks_.size();
        for (size_t i = 0; i < count; ++i)
        {
            callbacks_[i](args...);
        }
    }

    size_t size() const
    {
        return callbacks_.size();
    }

private:
    std::deque<Callback> callbacks_;
};

// The library's events (EP12, US07.2.2) and their listeners, used by the Developer's onX()
// calls and by internal parts alike. raise…() only queues an event; deliver(), called at the
// end of process(), calls the listeners. So a listener always sees the state after the change
// that raised the event, and listeners are never called from another task.
class Events
{
public:
    // At most this many events are delivered per deliver() call; the rest wait for the next
    // process(). Guards loop() against listeners that keep raising events in a circle.
    static constexpr size_t kMaxPerDelivery = 64;

    explicit Events(Logger& log) : log_(log)
    {
    }

    Events(const Events&) = delete;
    Events& operator=(const Events&) = delete;

    Listeners<const ConnectedInfo&> connected;
    Listeners<FailureReason> disconnected;
    Listeners<const std::string&, FailureReason> connectFailed;
    Listeners<ConnectionState, ConnectionState> connectionStateChanged;
    Listeners<const std::string&> portalStarted;
    Listeners<> portalStopped;
    Listeners<const std::string&> provisioningCompleted;

    void raiseConnected(ConnectedInfo info);
    void raiseDisconnected(FailureReason reason);
    void raiseConnectFailed(std::string ssid, FailureReason reason);
    void raiseConnectionStateChanged(ConnectionState from, ConnectionState to);
    void raisePortalStarted(std::string accessPointSsid);
    void raisePortalStopped();
    void raiseProvisioningCompleted(std::string ssid);

    // Calls the listeners of every queued event in the order raised, including events the
    // listeners raise meanwhile. Does nothing when called from inside a listener.
    void deliver();

    size_t pending() const
    {
        return pending_.size();
    }

private:
    Logger& log_;
    std::deque<std::function<void()>> pending_;
    bool delivering_ = false;
};

}  // namespace espmgr
