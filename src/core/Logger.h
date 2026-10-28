#pragma once

#include "core/Clock.h"

#include <cstdarg>
#include <cstddef>
#include <cstdint>

// Lets the compiler check log format strings against their arguments. Member functions count
// `this` as the first argument.
#if defined(__GNUC__)
#define ESPMGR_PRINTF_FORMAT(formatIndex, firstArg)                                                \
    __attribute__((format(printf, formatIndex, firstArg)))
#else
#define ESPMGR_PRINTF_FORMAT(formatIndex, firstArg)
#endif

namespace espmgr
{

// How much the library logs. Each level includes the ones above it; Off logs nothing.
enum class LogLevel : uint8_t
{
    Off,
    Error,
    Warn,
    Info,
    Debug,
    Verbose
};

// Where finished log lines go. The ESP32 version prints them to Serial.
class LogSink
{
public:
    virtual ~LogSink() = default;

    // `line` has no line ending and is only valid during the call.
    virtual void write(LogLevel level, const char* line) = 0;
};

// The library's internal logger (US10.1.1). Silent until a level is set. Lines look like
// "[espmgr 12.345 W wifi] lost connection", with the time in seconds since start-up.
//
// Never pass a password, or the value of a password-type Setting, to it (US08.2.2). They
// aren't masked afterwards; they must not reach the logger at all.
class Logger
{
public:
    // Longest line written, including the prefix; longer messages are cut off.
    static constexpr size_t kMaxLineLength = 160;

    Logger(LogSink& sink, const Clock& clock) : sink_(sink), clock_(clock)
    {
    }

    void setLevel(LogLevel level)
    {
        level_ = level;
    }

    LogLevel level() const
    {
        return level_;
    }

    // True if a message at `level` would be written. Lets callers skip expensive arguments.
    bool enabled(LogLevel level) const
    {
        return level != LogLevel::Off && level <= level_;
    }

    void error(const char* tag, const char* format, ...) ESPMGR_PRINTF_FORMAT(3, 4);
    void warn(const char* tag, const char* format, ...) ESPMGR_PRINTF_FORMAT(3, 4);
    void info(const char* tag, const char* format, ...) ESPMGR_PRINTF_FORMAT(3, 4);
    void debug(const char* tag, const char* format, ...) ESPMGR_PRINTF_FORMAT(3, 4);
    void verbose(const char* tag, const char* format, ...) ESPMGR_PRINTF_FORMAT(3, 4);

private:
    void write(LogLevel level, const char* tag, const char* format, va_list args);

    LogSink& sink_;
    const Clock& clock_;
    LogLevel level_ = LogLevel::Off;
};

}  // namespace espmgr
