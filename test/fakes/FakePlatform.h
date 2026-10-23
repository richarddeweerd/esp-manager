#pragma once

#include "FakeClock.h"
#include "FakeHttpServer.h"
#include "FakeStorage.h"
#include "FakeWifiDriver.h"
#include "core/Platform.h"

namespace espmgr
{

// All four fakes plus the Platform that points at them, for `EspManager esp(fakes.platform);`.
struct FakePlatform
{
    FakeWifiDriver wifi;
    FakeStorage storage;
    FakeClock clock;
    FakeHttpServer http;
    Platform platform{wifi, storage, clock, http};
};

}  // namespace espmgr
