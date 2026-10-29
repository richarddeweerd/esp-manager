#pragma once

#include "FakeClock.h"
#include "FakeHttpServer.h"
#include "FakeLogSink.h"
#include "FakeStorage.h"
#include "FakeSystem.h"
#include "FakeWifiDriver.h"
#include "core/Platform.h"

namespace espmgr
{

// All the fakes plus the Platform that points at them, for `EspManager esp(fakes.platform);`.
struct FakePlatform
{
    FakeWifiDriver wifi;
    FakeStorage storage;
    FakeClock clock;
    FakeHttpServer http;
    FakeLogSink log;
    FakeSystem system;
    Platform platform{wifi, storage, clock, http, log, system};
};

}  // namespace espmgr
