// Your own settings on the web pages. The sketch declares them; the Owner fills them in during
// setup in the Provisioning Portal, and the ones marked .editableInManagement() later in the
// Management Interface. The library validates and stores the values, and says when one changes.
//
// Logging is on because a mistake in a declaration, such as a repeated key or one over 15
// characters, can't stop the build or the sketch: it logs an error, and that Setting isn't stored
// or shown. So set the log level first, and declare the Settings after it.

#include <EspManager.h>

namespace
{

EspManager esp;

// A handle for quick access in loop(). Keep one like this, or look a value up by key.
espmgr::NumberSetting* reportSeconds = nullptr;
uint32_t lastReportMs = 0;

std::string mustNotHaveSpaces(const std::string& value)
{
    return value.find(' ') == std::string::npos ? "" : "Use a name without spaces.";
}

void declareSettings()
{
    auto& mqtt = esp.settings().section("MQTT");
    mqtt.addText("mqtt_host", "Server")
        .required()
        .validate(mustNotHaveSpaces)
        .editableInManagement();
    mqtt.addNumber("mqtt_port", "Port", 1883).range(1, 65535).editableInManagement();
    mqtt.addText("mqtt_user", "User name");
    // Never shown on a page or logged: the page only offers to replace or remove it.
    mqtt.addPassword("mqtt_pass", "Password");

    auto& device = esp.settings().section("Device");
    device.addSelect("unit", "Temperature unit", "c")
        .option("c", "Celsius")
        .option("f", "Fahrenheit")
        .editableInManagement();
    device.addCheckbox("report", "Report on the serial port", true).editableInManagement();
    reportSeconds = &device.addNumber("report_s", "Report every (seconds)", 30)
                         .range(5, 3600)
                         .editableInManagement();
}

}  // namespace

void setup()
{
    Serial.begin(115200);
    esp.setLogLevel(espmgr::LogLevel::Info);

    declareSettings();

    // The Settings tab appears because Settings above are marked for it.
    esp.management().enable();

    // Called from process(), once per Setting that changed. A password's text() is always empty,
    // so this can't print one. A Setting that only takes effect after a restart could call
    // esp.restart() here.
    esp.onSettingChanged(
        [](const espmgr::Setting& setting)
        { Serial.printf("%s is now '%s'\n", setting.key().c_str(), setting.text().c_str()); });

    esp.begin();

    // A firmware update can add a required Setting. It never affects Wi-Fi or reopens the portal
    // by itself, so the sketch decides what to do about it.
    if (!esp.settings().complete())
    {
        Serial.println("A required Setting is empty: fill it in on the web pages");
    }
}

void loop()
{
    esp.process();

    if (esp.settings().checkbox("report") &&
        millis() - lastReportMs >= static_cast<uint32_t>(reportSeconds->value()) * 1000)
    {
        lastReportMs = millis();
        Serial.printf("MQTT server %s:%d, unit %s\n", esp.settings().text("mqtt_host").c_str(),
                      static_cast<int>(esp.settings().number("mqtt_port")),
                      esp.settings().text("unit").c_str());
    }
}
