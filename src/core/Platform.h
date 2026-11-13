#pragma once

#include "core/Clock.h"
#include "core/HttpServer.h"
#include "core/Logger.h"
#include "core/Storage.h"
#include "core/System.h"
#include "core/TimeDriver.h"
#include "core/UpdateDriver.h"
#include "core/WifiDriver.h"

namespace espmgr
{

// The hardware the core uses. The objects must outlive the EspManager they are given to.
// Internal during 0.x: sketches use the default EspManager constructor, tests pass fakes.
struct Platform
{
    WifiDriver& wifi;
    Storage& storage;
    Clock& clock;
    HttpServer& http;
    LogSink& log;
    System& system;
    TimeDriver& time;
    // Firmware updates; nullptr when the platform provides its own only on request (the ESP32
    // one is made by esp.updates(), so sketches without updates don't link it).
    UpdateDriver* updates = nullptr;
};

}  // namespace espmgr
