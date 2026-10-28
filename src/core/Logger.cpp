#include "core/Logger.h"

#include <cstdarg>
#include <cstdio>

namespace espmgr
{

namespace
{

char letter(LogLevel level)
{
    switch (level)
    {
    case LogLevel::Error:
        return 'E';
    case LogLevel::Warn:
        return 'W';
    case LogLevel::Info:
        return 'I';
    case LogLevel::Debug:
        return 'D';
    case LogLevel::Verbose:
        return 'V';
    case LogLevel::Off:
        break;
    }
    return '?';
}

}  // namespace

#define ESPMGR_LOG_METHOD(name, logLevel)                                                          \
    void Logger::name(const char* tag, const char* format, ...)                                    \
    {                                                                                              \
        if (!enabled(logLevel))                                                                    \
        {                                                                                          \
            return;                                                                                \
        }                                                                                          \
        va_list args;                                                                              \
        va_start(args, format);                                                                    \
        write(logLevel, tag, format, args);                                                        \
        va_end(args);                                                                              \
    }

ESPMGR_LOG_METHOD(error, LogLevel::Error)
ESPMGR_LOG_METHOD(warn, LogLevel::Warn)
ESPMGR_LOG_METHOD(info, LogLevel::Info)
ESPMGR_LOG_METHOD(debug, LogLevel::Debug)
ESPMGR_LOG_METHOD(verbose, LogLevel::Verbose)

#undef ESPMGR_LOG_METHOD

void Logger::write(LogLevel level, const char* tag, const char* format, va_list args)
{
    char line[kMaxLineLength + 1];
    const uint64_t now = clock_.nowMs();
    int used = std::snprintf(line, sizeof(line), "[espmgr %lu.%03u %c %s] ",
                             static_cast<unsigned long>(now / 1000),
                             static_cast<unsigned>(now % 1000), letter(level), tag);
    if (used < 0)
    {
        return;
    }
    if (static_cast<size_t>(used) < sizeof(line))
    {
        std::vsnprintf(line + used, sizeof(line) - used, format, args);
    }
    sink_.write(level, line);
}

}  // namespace espmgr
