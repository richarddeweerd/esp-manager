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

    // True if a line contains `text`.
    bool contains(const std::string& text) const
    {
        for (const Line& line : lines)
        {
            if (line.text.find(text) != std::string::npos)
            {
                return true;
            }
        }
        return false;
    }

    std::vector<Line> lines;
};

}  // namespace espmgr
