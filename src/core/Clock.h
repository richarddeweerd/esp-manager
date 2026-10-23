#pragma once

#include <cstdint>

namespace espmgr
{

// Time and randomness for timeouts and backoff.
class Clock
{
public:
    virtual ~Clock() = default;

    // Milliseconds since start-up. Never goes backwards and doesn't wrap in practice.
    virtual uint64_t nowMs() const = 0;

    // A random number from 0 up to, but not including, `below`. Returns 0 if `below` is 0.
    virtual uint32_t random(uint32_t below) = 0;
};

}  // namespace espmgr
