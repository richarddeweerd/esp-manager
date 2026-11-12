// Network Time: the device's clock set from NTP servers once it's connected, with the time zone
// the Owner picks in the web pages, printing the local time every minute.
//
// Without ownerCanEdit() the sketch's own setTimezone() is used: a POSIX TZ string, such as
// "EST5EDT,M3.2.0,M11.1.0" for New York. Once synchronised, the C library's time(), localtime_r()
// and strftime() give the real local time, like they do here.

#include <Arduino.h>
#include <EspManager.h>

#include <ctime>

namespace
{

EspManager esp;
uint32_t lastPrintMs = 0;

void printLocalTime()
{
    const time_t now = time(nullptr);
    struct tm local;
    localtime_r(&now, &local);
    char text[32];
    strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S %Z", &local);
    Serial.printf("Local time: %s\n", text);
}

}  // namespace

void setup()
{
    Serial.begin(115200);

    esp.ntp()
        .enable()
        .ownerCanEdit()           // a Time Section with the zone picker, before begin()
        .editableInManagement();  // and in the Management Interface too
    esp.ntp().setTimezone("CET-1CEST,M3.5.0,M10.5.0/3");  // until the Owner picks a zone
    esp.management().enable();

    esp.onTimeSynced(
        [](const espmgr::TimeSyncInfo& info)
        {
            if (info.first)
            {
                Serial.printf("The clock is set, from %s.\n", esp.ntp().server().c_str());
                printLocalTime();
            }
        });
    esp.onTimeSyncFailed([] { Serial.println("No answer from the time servers yet."); });

    esp.begin();
}

void loop()
{
    esp.process();

    if (esp.ntp().synchronised() && millis() - lastPrintMs >= 60000)
    {
        lastPrintMs = millis();
        printLocalTime();
    }
}
