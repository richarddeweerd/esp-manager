#pragma once

#include "core/Clock.h"
#include "core/JsonWriter.h"
#include "core/Logger.h"
#include "core/WifiDriver.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace espmgr
{

// One entry of the Connection History (F10.3).
struct ConnectionEvent
{
    enum class Kind : uint8_t
    {
        // begin() was called: the device has just started.
        Started,
        // Joined `ssid`, with `rssi` and `channel`.
        Connected,
        // An attempt to join `ssid` failed for `reason`.
        ConnectFailed,
        // The connection to `ssid` ended, for `reason` (None when the device left on purpose).
        Disconnected,
    };

    Kind kind = Kind::Started;
    // Milliseconds since start-up (US10.3.2).
    uint64_t uptimeMs = 0;
    std::string ssid;
    FailureReason reason = FailureReason::None;
    int8_t rssi = 0;
    uint8_t channel = 0;
};

// "started", "connected", "connect-failed" or "disconnected".
const char* toString(ConnectionEvent::Kind kind);

// A short code for a failure in JSON: "none", "network-not-found", "wrong-password", "timeout",
// "connection-lost" or "other".
const char* failureCode(FailureReason reason);

// The most recent connection events, oldest dropped first, kept in RAM: a restart clears them,
// Network Reset and Settings Reset don't. EspManager records the events it raises, so this holds
// exactly what onConnected, onConnectFailed and onDisconnected report, plus Started.
class ConnectionHistory
{
public:
    static constexpr size_t kDefaultCapacity = 20;
    static constexpr size_t kMaxCapacity = 50;

    ConnectionHistory(const Clock& clock, Logger& log) : clock_(clock), log_(log)
    {
    }

    ConnectionHistory(const ConnectionHistory&) = delete;
    ConnectionHistory& operator=(const ConnectionHistory&) = delete;

    // How many entries are kept, 0 to 50; 0 keeps none. Only before begin(): later, or above 50,
    // it changes nothing, logs at Error and returns false.
    bool setCapacity(size_t capacity);
    size_t capacity() const
    {
        return capacity_;
    }

    // The entries, oldest first.
    std::vector<ConnectionEvent> entries() const;
    size_t size() const
    {
        return entries_.size();
    }

    // Used by EspManager.
    void begin();
    void recordConnected(const std::string& ssid, int8_t rssi, uint8_t channel);
    void recordConnectFailed(const std::string& ssid, FailureReason reason);
    // For the network the last recordConnected() named.
    void recordDisconnected(FailureReason reason);

private:
    void record(ConnectionEvent event);

    const Clock& clock_;
    Logger& log_;
    size_t capacity_ = kDefaultCapacity;
    bool begun_ = false;
    // A ring: once full, `next_` is the oldest entry and the next one to be replaced.
    std::vector<ConnectionEvent> entries_;
    size_t next_ = 0;
    std::string connectedSsid_;
};

// Writes the history for GET /api/history: {"uptimeMs":…,"capacity":…,"events":[…]}, oldest
// first, each event with its kind, uptimeMs and the fields that apply to it. `nowMs` lets the page
// say how long ago each one was.
void writeJson(JsonWriter& json, const ConnectionHistory& history, uint64_t nowMs);

}  // namespace espmgr
