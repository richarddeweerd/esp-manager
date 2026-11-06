#pragma once

#include "core/HttpServer.h"
#include "core/Logger.h"
#include "core/Settings.h"
#include "core/WebRoutes.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace espmgr
{

// The Settings as the web pages see them (US06.1.1, US06.1.3, US06.2.1):
//
// - GET /api/settings: {"complete":true,"sections":[{"title":"MQTT","settings":[…]},…]}
// - POST /api/settings: validates and stores the fields, all or nothing
// - POST /api/settings/check: validates the fields without storing them
//
// A field is `setting.<key>` with the value as text. For a password, an empty value keeps the
// stored one and `clear.<key>=1` removes it. Fields that aren't mentioned stay as they are.
//
// A password's value is never sent: its entry only says whether one is set. A save answers with
// the updated Settings, a check with {"ok":true}, and a refusal with
// {"error":"settings","errors":[{"key":…,"error":"<code>","message":…},…]}. The POST requests
// must carry the header `X-Requested-With: EspManager`, which another web site can't add.
//
// The Management Interface only sees the Settings marked .editableInManagement(): the others are
// left out of its answers, and a request naming one is refused as if it didn't exist. `complete`
// covers the Settings that site shows, while `hasSettings` says whether the sketch has any at all.
class SettingsApi
{
public:
    using Fields = std::vector<std::pair<std::string, std::string>>;
    // Answers the request itself and returns false if it isn't allowed.
    using Authorize = std::function<bool(const HttpRequest&)>;

    SettingsApi(WebRoutes& routes, Settings& settings, Logger& log)
        : routes_(routes), settings_(settings), log_(log)
    {
    }

    SettingsApi(const SettingsApi&) = delete;
    SettingsApi& operator=(const SettingsApi&) = delete;

    void registerRoutes(WebRoutes::Site site, Authorize authorize);

    // The Settings fields in a request, as (key, value) for Settings::check() and apply().
    Fields fieldsFrom(const HttpRequest& request) const;

    // Answers with the problems, as described above.
    void sendProblems(int status, const std::vector<Settings::FieldProblem>& problems);

private:
    void sendSettings(WebRoutes::Site site);
    // The fields in `request` naming a Setting that the Management Interface doesn't show.
    std::vector<Settings::FieldProblem> hiddenFields(const HttpRequest& request) const;

    WebRoutes& routes_;
    Settings& settings_;
    Logger& log_;
};

}  // namespace espmgr
