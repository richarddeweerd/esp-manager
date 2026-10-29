#pragma once

#include <cstdint>
#include <string>

namespace espmgr
{

// The chip itself: what the status page reports about it.
class System
{
public:
    virtual ~System() = default;

    // Bytes of heap that are free now.
    virtual uint32_t freeHeap() const = 0;
    // The chip and its revision, such as "ESP32-S3 rev 0".
    virtual std::string chipModel() const = 0;
};

}  // namespace espmgr
