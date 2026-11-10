#include "core/Branding.h"

#include "core/JsonWriter.h"

#include <cstring>

namespace espmgr
{

namespace
{

const char* const kLogoTypes[] = {"image/svg+xml", "image/png", "image/jpeg", "image/webp"};

bool knownLogoType(const char* contentType)
{
    for (const char* type : kLogoTypes)
    {
        if (std::strcmp(type, contentType) == 0)
        {
            return true;
        }
    }
    return false;
}

}  // namespace

const char* toString(Theme theme)
{
    switch (theme)
    {
    case Theme::Auto:
        return "auto";
    case Theme::Light:
        return "light";
    case Theme::Dark:
        return "dark";
    }
    return "auto";
}

bool Branding::setTitle(const std::string& title)
{
    if (title.size() > kMaxTitleLength)
    {
        log_.error("ui", "a title of %u bytes is over the limit of %u",
                   static_cast<unsigned>(title.size()), static_cast<unsigned>(kMaxTitleLength));
        return false;
    }
    for (const char c : title)
    {
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 0x20 || byte == 0x7f)
        {
            log_.error("ui", "the title has a control character");
            return false;
        }
    }
    title_ = title;
    return true;
}

bool Branding::setLogo(const uint8_t* data, size_t size, const char* contentType)
{
    if (data == nullptr)
    {
        if (size != 0)
        {
            log_.error("ui", "a logo without data");
            return false;
        }
        logo_ = nullptr;
        logoSize_ = 0;
        logoType_ = nullptr;
        return true;
    }
    if (size == 0 || contentType == nullptr || !knownLogoType(contentType))
    {
        log_.error("ui", "a logo must be SVG, PNG, JPEG or WebP, with data");
        return false;
    }
    logo_ = data;
    logoSize_ = size;
    logoType_ = contentType;
    return true;
}

void Branding::registerRoutes(WebRoutes::Site site, Authorize authorize)
{
    routes_.on(site, HttpMethod::Get, "/api/ui",
               [this, authorize](const HttpRequest& request)
               {
                   if (authorize(request))
                   {
                       sendUi();
                   }
               });
    routes_.on(site, HttpMethod::Get, "/custom.css",
               [this, authorize](const HttpRequest& request)
               {
                   if (authorize(request))
                   {
                       sendCss();
                   }
               });
    routes_.on(site, HttpMethod::Get, "/logo",
               [this, authorize](const HttpRequest& request)
               {
                   if (authorize(request))
                   {
                       sendLogo();
                   }
               });
}

void Branding::sendUi()
{
    JsonWriter json;
    json.beginObject()
        .key("title")
        .string(title_)
        .key("logo")
        .boolean(hasLogo())
        .key("theme")
        .string(toString(theme_))
        .endObject();
    HttpServer& http = routes_.http();
    http.sendHeader("Cache-Control", "no-cache");
    http.send(200, "application/json", json.str());
}

void Branding::sendCss()
{
    HttpServer& http = routes_.http();
    http.sendHeader("Cache-Control", "no-cache");
    const char* css = css_ != nullptr ? css_ : "";
    http.send(200, "text/css", reinterpret_cast<const uint8_t*>(css), std::strlen(css), false);
}

void Branding::sendLogo()
{
    HttpServer& http = routes_.http();
    if (logo_ == nullptr)
    {
        http.send(404, "text/plain", "No logo.");
        return;
    }
    http.sendHeader("Cache-Control", "no-cache");
    http.send(200, logoType_, logo_, logoSize_, false);
}

}  // namespace espmgr
