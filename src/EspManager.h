#pragma once

#include "EspManagerVersion.h"

namespace espmgr
{

// The library's single entry point: call begin() from setup() and process() from loop().
class EspManager
{
public:
    // Returns immediately; all work happens in process().
    void begin();

    // Handles pending work without blocking. Call it on every loop().
    void process();
};

}  // namespace espmgr

using espmgr::EspManager;
