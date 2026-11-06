#include "core/SettingsApi.h"

#include "core/JsonWriter.h"

#include <algorithm>

namespace espmgr
{

namespace
{

constexpr const char* kFieldPrefix = "setting.";
constexpr const char* kClearPrefix = "clear.";

// The part of `name` after `prefix`, or nothing if it doesn't start with it.
bool afterPrefix(const std::string& name, const char* prefix, std::string& rest)
{
    const std::string start(prefix);
    if (name.compare(0, start.size(), start) != 0)
    {
        return false;
    }
    rest = name.substr(start.size());
    return true;
}

const char* typeName(Setting::Type type)
{
    switch (type)
    {
    case Setting::Type::Password:
        return "password";
    case Setting::Type::Number:
        return "number";
    case Setting::Type::Checkbox:
        return "checkbox";
    case Setting::Type::Select:
        return "select";
    default:
        return "text";
    }
}

void writeSetting(JsonWriter& json, const Setting& setting)
{
    json.beginObject()
        .key("key")
        .string(setting.key())
        .key("label")
        .string(setting.label())
        .key("type")
        .string(typeName(setting.type()))
        .key("required")
        .boolean(setting.isRequired());
    switch (setting.type())
    {
    case Setting::Type::Text:
        json.key("value")
            .string(setting.text())
            .key("maxLength")
            .number(
                static_cast<int64_t>(static_cast<const TextSetting&>(setting).maxLengthValue()));
        break;
    case Setting::Type::Password:
        json.key("set").boolean(!setting.isEmpty());
        json.key("maxLength")
            .number(static_cast<int64_t>(
                static_cast<const PasswordSetting&>(setting).maxLengthValue()));
        break;
    case Setting::Type::Number:
    {
        const NumberSetting& number = static_cast<const NumberSetting&>(setting);
        json.key("value")
            .string(setting.text())
            .key("min")
            .number(number.min())
            .key("max")
            .number(number.max());
        break;
    }
    case Setting::Type::Checkbox:
        json.key("value").string(setting.text());
        break;
    case Setting::Type::Select:
        json.key("value").string(setting.text()).key("options").beginArray();
        for (const SelectSetting::Option& option :
             static_cast<const SelectSetting&>(setting).options())
        {
            json.beginObject()
                .key("value")
                .string(option.value)
                .key("label")
                .string(option.label)
                .endObject();
        }
        json.endArray();
        break;
    }
    json.endObject();
}

}  // namespace

void SettingsApi::registerRoutes(WebRoutes::Site site, Authorize authorize)
{
    routes_.on(site, HttpMethod::Get, "/api/settings",
               [this, authorize](const HttpRequest& request)
               {
                   if (authorize(request))
                   {
                       sendSettings();
                   }
               });
    // A change is allowed for an authorised request that comes from the page itself.
    const auto onPost =
        [this, site, authorize](const char* path, std::function<void(const Fields&)> action)
    {
        routes_.on(site, HttpMethod::Post, path,
                   [this, authorize, action](const HttpRequest& request)
                   {
                       if (!authorize(request))
                       {
                           return;
                       }
                       if (request.header("X-Requested-With").value_or("") != "EspManager")
                       {
                           log_.warn("settings", "refused a request without X-Requested-With");
                           routes_.http().send(403, "text/plain", "Use the setup page.");
                           return;
                       }
                       action(fieldsFrom(request));
                   });
    };
    onPost("/api/settings",
           [this](const Fields& fields)
           {
               const std::vector<Settings::FieldProblem> problems = settings_.apply(fields);
               if (problems.empty())
               {
                   sendSettings();
                   return;
               }
               const bool notStored = problems[0].check.problem == Setting::Problem::NotStored;
               sendProblems(notStored ? 500 : 400, problems);
           });
    onPost("/api/settings/check",
           [this](const Fields& fields)
           {
               const std::vector<Settings::FieldProblem> problems = settings_.check(fields);
               if (!problems.empty())
               {
                   sendProblems(400, problems);
                   return;
               }
               routes_.http().send(200, "application/json", R"({"ok":true})");
           });
}

SettingsApi::Fields SettingsApi::fieldsFrom(const HttpRequest& request) const
{
    Fields fields;
    for (const auto& [name, value] : request.args)
    {
        std::string key;
        if (afterPrefix(name, kFieldPrefix, key))
        {
            const Setting* const setting = settings_.find(key);
            // An empty password keeps the stored one; without one, a required Setting must hear
            // about it.
            const bool keep = setting != nullptr && setting->type() == Setting::Type::Password &&
                              value.empty() && !(setting->isRequired() && setting->isEmpty());
            if (!keep)
            {
                fields.emplace_back(std::move(key), value);
            }
        }
        else if (afterPrefix(name, kClearPrefix, key) && value == "1")
        {
            const Setting* const setting = settings_.find(key);
            if (setting != nullptr && setting->type() == Setting::Type::Password)
            {
                fields.emplace_back(std::move(key), std::string());
            }
        }
    }
    return fields;
}

void SettingsApi::sendSettings()
{
    JsonWriter json;
    json.beginObject().key("complete").boolean(settings_.complete()).key("sections").beginArray();
    for (const SettingsSection& section : settings_.sections())
    {
        if (section.settings().empty())
        {
            continue;
        }
        json.beginObject().key("title").string(section.title()).key("settings").beginArray();
        for (const Setting* const setting : section.settings())
        {
            writeSetting(json, *setting);
        }
        json.endArray().endObject();
    }
    json.endArray().endObject();
    routes_.http().send(200, "application/json", json.str());
}

void SettingsApi::sendProblems(int status, const std::vector<Settings::FieldProblem>& problems)
{
    JsonWriter json;
    json.beginObject().key("error").string("settings").key("errors").beginArray();
    for (const Settings::FieldProblem& problem : problems)
    {
        json.beginObject()
            .key("key")
            .string(problem.key)
            .key("error")
            .string(errorCode(problem.check.problem))
            .key("message")
            .string(problem.check.message)
            .endObject();
    }
    json.endArray().endObject();
    routes_.http().send(status, "application/json", json.str());
}

}  // namespace espmgr
