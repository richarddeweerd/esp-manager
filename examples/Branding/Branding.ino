// Your product's look on the web pages: a title, a logo, a fixed theme and a few colours,
// for the Provisioning Portal and the Management Interface alike.
//
// The CSS changes the pages' custom properties (--accent, --radius, ...); rules under
// [data-theme="dark"] apply in the dark theme. Leave out setTheme() to follow the phone.

#include <Arduino.h>
#include <EspManager.h>

namespace
{

EspManager esp;

// The logo, kept in flash: the library serves these bytes as they are, without copying them.
const uint8_t kLogo[] = R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32">)"
                        R"(<rect width="32" height="32" rx="8" fill="#0f766e"/>)"
                        R"(<path d="M16 7a6 6 0 0 0-6 6v7a6 6 0 1 0 12 0v-7a6 6 0 0 0-6-6z")"
                        R"( fill="#fff"/></svg>)";

const char* const kCss = R"(
:root { --accent: #0f766e; --radius: 14px; --radius-sm: 10px; }
[data-theme="dark"] { --accent: #2dd4bf; --accent-text: #042f2e; }
)";

}  // namespace

void setup()
{
    Serial.begin(115200);

    esp.ui().setTitle("Acme Thermostat");
    esp.ui().setLogo(kLogo, sizeof(kLogo) - 1, "image/svg+xml");  // without the closing NUL
    esp.ui().setTheme(espmgr::Theme::Light);
    esp.ui().setCss(kCss);

    // The Management Interface shows the same Branding; the Owner picks its password in the
    // Provisioning Portal.
    esp.management().enable();
    esp.begin();
}

void loop()
{
    esp.process();
}
