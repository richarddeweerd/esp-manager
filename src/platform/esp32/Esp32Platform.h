#pragma once

#include "core/Platform.h"
#include "platform/esp32/Esp32Clock.h"
#include "platform/esp32/Esp32HttpServer.h"
#include "platform/esp32/Esp32WifiDriver.h"
#include "platform/esp32/NvsStorage.h"

namespace espmgr
{

// Owns the real ESP32 hardware objects for the default EspManager constructor.
class Esp32Platform
{
public:
    Esp32Platform() = default;

    Esp32Platform(const Esp32Platform&) = delete;
    Esp32Platform& operator=(const Esp32Platform&) = delete;

    Platform& platform()
    {
        return platform_;
    }

private:
    Esp32WifiDriver wifi_;
    NvsStorage storage_;
    Esp32Clock clock_;
    Esp32HttpServer http_;
    Platform platform_{wifi_, storage_, clock_, http_};
};

}  // namespace espmgr
