#pragma once

#include "EspManagerVersion.h"

#include <memory>

namespace espmgr
{

struct Platform;

// The library's single entry point: call begin() from setup() and process() from loop().
class EspManager
{
public:
    // Uses the ESP32's own Wi-Fi, NVS storage and web server.
    EspManager();
    // Uses the given hardware instead; meant for tests. `platform` must outlive this object.
    explicit EspManager(Platform& platform);

    EspManager(const EspManager&) = delete;
    EspManager& operator=(const EspManager&) = delete;

    // Returns immediately; all work happens in process().
    void begin();

    // Handles pending work without blocking. Call it on every loop().
    void process();

private:
    // Owns the ESP32 hardware objects when the default constructor made them. Type-erased, so
    // this header doesn't depend on the ESP32 code.
    std::shared_ptr<void> ownedPlatform_;
    // Not used yet; the connection state machine (#7) will drive it.
    [[maybe_unused]] Platform& platform_;
};

}  // namespace espmgr

using espmgr::EspManager;
