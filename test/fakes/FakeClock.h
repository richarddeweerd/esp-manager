#pragma once

#include "core/Clock.h"

#include <deque>
#include <functional>

namespace espmgr
{

// A Clock that only moves when the test calls advance() or the code under test calls
// delayMs(), and returns the random values the test queued (0 once the queue is empty).
class FakeClock : public Clock
{
public:
    uint64_t nowMs() const override
    {
        return now_;
    }

    uint32_t random(uint32_t below) override
    {
        if (below == 0)
        {
            return 0;
        }
        uint32_t value = 0;
        if (!randomValues.empty())
        {
            value = randomValues.front();
            randomValues.pop_front();
        }
        return value % below;
    }

    // Moves the time on and then calls onDelay, so a test can drive the other fakes while the
    // code under test waits.
    void delayMs(uint32_t ms) override
    {
        now_ += ms;
        ++delays;
        if (onDelay)
        {
            onDelay();
        }
    }

    void advance(uint64_t ms)
    {
        now_ += ms;
    }

    std::function<void()> onDelay;
    int delays = 0;

    std::deque<uint32_t> randomValues;

private:
    uint64_t now_ = 0;
};

}  // namespace espmgr
