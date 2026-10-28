#pragma once

#include "core/Logger.h"

namespace espmgr
{

// Prints log lines to Serial. The sketch calls Serial.begin() itself.
class SerialLogSink : public LogSink
{
public:
    void write(LogLevel level, const char* line) override;
};

}  // namespace espmgr
