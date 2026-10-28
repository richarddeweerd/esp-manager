#include "platform/esp32/SerialLogSink.h"

#include <Arduino.h>

namespace espmgr
{

void SerialLogSink::write(LogLevel, const char* line)
{
    Serial.println(line);
}

}  // namespace espmgr
