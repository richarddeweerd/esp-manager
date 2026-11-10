#pragma once

#include "core/HttpServer.h"
#include "core/Logger.h"
#include "core/WebRoutes.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace espmgr
{

// Light or dark pages. Auto follows the phone's setting.
enum class Theme : uint8_t
{
    Auto,
    Light,
    Dark,
};

// "auto", "light" or "dark".
const char* toString(Theme theme);

// The Developer's look for the web pages (EP15): a title, a logo, a theme and a CSS string. They
// apply to the Provisioning Portal and the Management Interface alike, and may be changed at any
// time, since the pages ask for them each time they load:
//
// - GET /api/ui: {"title":"Acme Thermostat","logo":true,"theme":"auto"}
// - GET /custom.css: the CSS string, empty without one; the pages load it after their own style
// - GET /logo: the logo, 404 without one
//
// All three answer with `Cache-Control: no-cache`, so a firmware update never leaves an old look
// in the Owner's browser.
class Branding
{
public:
    static constexpr size_t kMaxTitleLength = 64;

    // Answers the request itself and returns false if it isn't allowed.
    using Authorize = std::function<bool(const HttpRequest&)>;

    Branding(WebRoutes& routes, Logger& log) : routes_(routes), log_(log)
    {
    }

    Branding(const Branding&) = delete;
    Branding& operator=(const Branding&) = delete;

    // The product's name, shown as the heading of both sites and at the start of the browser's
    // tab title; the built-in headings become subtitles. Copied. At most 64 bytes without control
    // characters; an empty title goes back to none. A refused title changes nothing, is logged at
    // Error, and returns false.
    bool setTitle(const std::string& title);
    const std::string& title() const
    {
        return title_;
    }

    // A logo shown next to the title and as the tab's icon: `contentType` "image/svg+xml",
    // "image/png", "image/jpeg" or "image/webp". The bytes aren't copied, so they must stay valid,
    // as a `static const` array in flash does. `setLogo(nullptr, 0, nullptr)` removes it. A
    // refused logo changes nothing, is logged at Error, and returns false.
    bool setLogo(const uint8_t* data, size_t size, const char* contentType);
    bool hasLogo() const
    {
        return logo_ != nullptr;
    }

    // Auto (the default) follows the phone's light or dark setting.
    void setTheme(Theme theme)
    {
        theme_ = theme;
    }
    Theme theme() const
    {
        return theme_;
    }

    // CSS loaded after the pages' own style. The string isn't copied, so it must stay valid, as a
    // string literal does; nullptr removes it. Restyle the pages through their custom properties
    // (--accent, --bg, --surface, --text, --radius, --font, ...); `[data-theme="dark"]` selects
    // the dark theme, whether the phone or setTheme() chose it.
    void setCss(const char* css)
    {
        css_ = css;
    }
    const char* css() const
    {
        return css_;
    }

    // Used by the Provisioning Portal and the Management Interface.
    void registerRoutes(WebRoutes::Site site, Authorize authorize);

private:
    void sendUi();
    void sendCss();
    void sendLogo();

    WebRoutes& routes_;
    Logger& log_;
    std::string title_;
    Theme theme_ = Theme::Auto;
    const char* css_ = nullptr;
    const uint8_t* logo_ = nullptr;
    size_t logoSize_ = 0;
    const char* logoType_ = nullptr;
};

}  // namespace espmgr
