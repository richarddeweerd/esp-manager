#include "core/ConnectionHistory.h"

#include <utility>

namespace espmgr
{

const char* toString(ConnectionEvent::Kind kind)
{
    switch (kind)
    {
    case ConnectionEvent::Kind::Started:
        return "started";
    case ConnectionEvent::Kind::Connected:
        return "connected";
    case ConnectionEvent::Kind::ConnectFailed:
        return "connect-failed";
    case ConnectionEvent::Kind::Disconnected:
        return "disconnected";
    }
    return "?";
}

const char* failureCode(FailureReason reason)
{
    switch (reason)
    {
    case FailureReason::None:
        return "none";
    case FailureReason::NetworkNotFound:
        return "network-not-found";
    case FailureReason::WrongPassword:
        return "wrong-password";
    case FailureReason::Timeout:
        return "timeout";
    case FailureReason::ConnectionLost:
        return "connection-lost";
    case FailureReason::Other:
        break;
    }
    return "other";
}

void writeJson(JsonWriter& json, const ConnectionHistory& history, uint64_t nowMs)
{
    json.beginObject()
        .key("uptimeMs")
        .number(static_cast<int64_t>(nowMs))
        .key("capacity")
        .number(static_cast<int64_t>(history.capacity()))
        .key("events")
        .beginArray();
    for (const ConnectionEvent& event : history.entries())
    {
        json.beginObject()
            .key("kind")
            .string(toString(event.kind))
            .key("uptimeMs")
            .number(static_cast<int64_t>(event.uptimeMs));
        if (event.kind != ConnectionEvent::Kind::Started)
        {
            json.key("ssid").string(event.ssid);
        }
        if (event.kind == ConnectionEvent::Kind::Connected)
        {
            json.key("rssi").number(event.rssi).key("channel").number(event.channel);
        }
        else if (event.kind != ConnectionEvent::Kind::Started)
        {
            json.key("reason").string(failureCode(event.reason));
        }
        json.endObject();
    }
    json.endArray().endObject();
}

bool ConnectionHistory::setCapacity(size_t capacity)
{
    if (begun_)
    {
        log_.error("history", "setCapacity() after begin() is ignored");
        return false;
    }
    if (capacity > kMaxCapacity)
    {
        log_.error("history", "a capacity of %u is over the limit of %u",
                   static_cast<unsigned>(capacity), static_cast<unsigned>(kMaxCapacity));
        return false;
    }
    capacity_ = capacity;
    return true;
}

std::vector<ConnectionEvent> ConnectionHistory::entries() const
{
    std::vector<ConnectionEvent> ordered;
    ordered.reserve(entries_.size());
    for (size_t i = 0; i < entries_.size(); ++i)
    {
        // Before the ring is full, next_ is its end and the oldest entry is at 0.
        const size_t start = entries_.size() < capacity_ ? 0 : next_;
        ordered.push_back(entries_[(start + i) % entries_.size()]);
    }
    return ordered;
}

void ConnectionHistory::begin()
{
    begun_ = true;
    entries_.reserve(capacity_);
    ConnectionEvent started;
    started.kind = ConnectionEvent::Kind::Started;
    record(std::move(started));
}

void ConnectionHistory::recordConnected(const std::string& ssid, int8_t rssi, uint8_t channel)
{
    connectedSsid_ = ssid;
    ConnectionEvent event;
    event.kind = ConnectionEvent::Kind::Connected;
    event.ssid = ssid;
    event.rssi = rssi;
    event.channel = channel;
    record(std::move(event));
}

void ConnectionHistory::recordConnectFailed(const std::string& ssid, FailureReason reason)
{
    ConnectionEvent event;
    event.kind = ConnectionEvent::Kind::ConnectFailed;
    event.ssid = ssid;
    event.reason = reason;
    record(std::move(event));
}

void ConnectionHistory::recordDisconnected(FailureReason reason)
{
    ConnectionEvent event;
    event.kind = ConnectionEvent::Kind::Disconnected;
    event.ssid = connectedSsid_;
    event.reason = reason;
    record(std::move(event));
}

void ConnectionHistory::record(ConnectionEvent event)
{
    if (capacity_ == 0)
    {
        return;
    }
    event.uptimeMs = clock_.nowMs();
    if (entries_.size() < capacity_)
    {
        entries_.push_back(std::move(event));
        next_ = entries_.size() % capacity_;
        return;
    }
    entries_[next_] = std::move(event);
    next_ = (next_ + 1) % capacity_;
}

}  // namespace espmgr
