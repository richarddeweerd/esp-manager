#pragma once

#include "core/Clock.h"
#include "core/HttpServer.h"
#include "core/Storage.h"
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
};

}  // namespace espmgr
