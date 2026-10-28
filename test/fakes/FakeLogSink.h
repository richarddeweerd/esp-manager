#pragma once

#include "core/Logger.h"

#include <string>
#include <vector>

namespace espmgr
{

// Keeps every line written, so tests can check what was logged and what wasn't.
class FakeLogSink : public LogSink
{
public:
    struct Line
    {
        LogLevel level;
        std::string text;
    };

    void write(LogLevel level, const char* line) override
    {
        lines.push_back({level, line});
    }

    std::vector<Line> lines;
};

}  // namespace espmgr
