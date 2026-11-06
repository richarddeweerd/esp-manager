#include "core/Settings.h"

#include <algorithm>

namespace espmgr
{

SettingsSection& Settings::section(const std::string& title)
{
    for (SettingsSection& section : sections_)
    {
        if (section.title() == title)
        {
            return section;
        }
    }
    sections_.emplace_back(*this, title);
    return sections_.back();
}

const char* Settings::declarationMistake(const std::string& key) const
{
    if (loaded_)
    {
        return "declared after begin()";
    }
    if (!Storage::isValidName(key))
    {
        return "a key must be 1 to 15 characters";
    }
    const bool cleanKey = std::all_of(key.begin(), key.end(),
                                      [](char c)
                                      {
                                          return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                                 (c >= '0' && c <= '9') || c == '_' || c == '-';
                                      });
    if (!cleanKey)
    {
        return "a key may only hold letters, digits, '_' and '-'";
    }
    return find(key) != nullptr ? "the key is used already" : nullptr;
}

template <typename T, typename... Args>
T& Settings::declare(SettingsSection& section, const std::string& key, const std::string& label,
                     Args&&... args)
{
    auto setting = std::make_unique<T>(*this, key, label, std::forward<Args>(args)...);
    T& result = *setting;
    setting->section_ = section.title();
    const char* const mistake = declarationMistake(key);
    if (mistake != nullptr)
    {
        log_.error("settings", "refused %s: %s", key.c_str(), mistake);
        setting->valid_ = false;
        refused_.push_back(std::move(setting));
        return result;
    }
    section.settings_.push_back(setting.get());
    settings_.push_back(std::move(setting));
    return result;
}

TextSetting& SettingsSection::addText(const std::string& key, const std::string& label,
                                      const std::string& defaultValue)
{
    return owner_.declare<TextSetting>(*this, key, label, defaultValue);
}

PasswordSetting& SettingsSection::addPassword(const std::string& key, const std::string& label)
{
    return owner_.declare<PasswordSetting>(*this, key, label);
}

NumberSetting& SettingsSection::addNumber(const std::string& key, const std::string& label,
                                          int32_t defaultValue)
{
    return owner_.declare<NumberSetting>(*this, key, label, defaultValue);
}

CheckboxSetting& SettingsSection::addCheckbox(const std::string& key, const std::string& label,
                                              bool defaultValue)
{
    return owner_.declare<CheckboxSetting>(*this, key, label, defaultValue);
}

SelectSetting& SettingsSection::addSelect(const std::string& key, const std::string& label,
                                          const std::string& defaultValue)
{
    return owner_.declare<SelectSetting>(*this, key, label, defaultValue);
}

TextSetting& Settings::addText(const std::string& key, const std::string& label,
                               const std::string& defaultValue)
{
    return section({}).addText(key, label, defaultValue);
}

PasswordSetting& Settings::addPassword(const std::string& key, const std::string& label)
{
    return section({}).addPassword(key, label);
}

NumberSetting& Settings::addNumber(const std::string& key, const std::string& label,
                                   int32_t defaultValue)
{
    return section({}).addNumber(key, label, defaultValue);
}

CheckboxSetting& Settings::addCheckbox(const std::string& key, const std::string& label,
                                       bool defaultValue)
{
    return section({}).addCheckbox(key, label, defaultValue);
}

SelectSetting& Settings::addSelect(const std::string& key, const std::string& label,
                                   const std::string& defaultValue)
{
    return section({}).addSelect(key, label, defaultValue);
}

std::string Settings::text(const std::string& key) const
{
    const Setting* const setting = find(key);
    if (setting == nullptr || setting->type() == Setting::Type::Number ||
        setting->type() == Setting::Type::Checkbox)
    {
        log_.warn("settings", "no text Setting %s", key.c_str());
        return {};
    }
    return setting->rawText();
}

int32_t Settings::number(const std::string& key) const
{
    const Setting* const setting = find(key);
    if (setting == nullptr || setting->type() != Setting::Type::Number)
    {
        log_.warn("settings", "no number Setting %s", key.c_str());
        return 0;
    }
    return static_cast<const NumberSetting*>(setting)->value();
}

bool Settings::checkbox(const std::string& key) const
{
    const Setting* const setting = find(key);
    if (setting == nullptr || setting->type() != Setting::Type::Checkbox)
    {
        log_.warn("settings", "no checkbox Setting %s", key.c_str());
        return false;
    }
    return static_cast<const CheckboxSetting*>(setting)->value();
}

Setting* Settings::find(const std::string& key) const
{
    for (const std::unique_ptr<Setting>& setting : settings_)
    {
        if (setting->key() == key)
        {
            return setting.get();
        }
    }
    return nullptr;
}

bool Settings::complete() const
{
    return std::none_of(settings_.begin(), settings_.end(),
                        [](const std::unique_ptr<Setting>& setting)
                        { return setting->isRequired() && setting->isEmpty(); });
}

std::vector<Settings::FieldProblem>
Settings::check(const std::vector<std::pair<std::string, std::string>>& fields)
{
    std::vector<FieldProblem> problems;
    for (const auto& [key, text] : fields)
    {
        const Setting* const setting = find(key);
        if (setting == nullptr)
        {
            problems.push_back({key, {Setting::Problem::UnknownSetting, {}}});
            continue;
        }
        Setting::Check result = setting->checkText(text);
        if (!result.ok())
        {
            problems.push_back({key, std::move(result)});
        }
    }
    return problems;
}

std::vector<Settings::FieldProblem>
Settings::missingRequired(const std::vector<std::pair<std::string, std::string>>& fields) const
{
    std::vector<FieldProblem> problems;
    for (const std::unique_ptr<Setting>& setting : settings_)
    {
        const bool given = std::any_of(fields.begin(), fields.end(), [&setting](const auto& field)
                                       { return field.first == setting->key(); });
        if (setting->isRequired() && setting->isEmpty() && !given)
        {
            problems.push_back({setting->key(), {Setting::Problem::Required, {}}});
        }
    }
    return problems;
}

std::vector<Settings::FieldProblem>
Settings::apply(const std::vector<std::pair<std::string, std::string>>& fields)
{
    std::vector<FieldProblem> problems = check(fields);
    if (!problems.empty())
    {
        return problems;
    }
    if (!loaded_)
    {
        log_.warn("settings", "not changed, begin() hasn't been called");
        for (const auto& field : fields)
        {
            problems.push_back({field.first, {Setting::Problem::NotStored, {}}});
        }
        return problems;
    }
    // What each one held, to put back if a later one can't be stored.
    std::vector<std::pair<Setting*, std::string>> done;
    std::vector<Setting*> changed;
    for (const auto& [key, text] : fields)
    {
        Setting* const setting = find(key);
        std::string previous = setting->rawText();
        bool settingChanged = false;
        if (!setting->commitText(text, settingChanged))
        {
            for (auto undo = done.rbegin(); undo != done.rend(); ++undo)
            {
                bool ignored = false;
                undo->first->commitText(undo->second, ignored);
            }
            problems.push_back({key, {Setting::Problem::NotStored, {}}});
            return problems;
        }
        done.emplace_back(setting, std::move(previous));
        if (settingChanged && std::find(changed.begin(), changed.end(), setting) == changed.end())
        {
            changed.push_back(setting);
        }
    }
    for (const Setting* const setting : changed)
    {
        announce(*setting);
    }
    return problems;
}

bool Settings::reset()
{
    if (!storage_.clear(kNamespace))
    {
        log_.error("settings", "couldn't erase the stored values");
        return false;
    }
    for (const std::unique_ptr<Setting>& setting : settings_)
    {
        if (setting->restoreDefault())
        {
            announce(*setting);
        }
    }
    return true;
}

void Settings::begin()
{
    load();
    loaded_ = true;
}

void Settings::load()
{
    for (const std::unique_ptr<Setting>& setting : settings_)
    {
        setting->load();
    }
    log_.info("settings", "%u Setting(s)", static_cast<unsigned>(settings_.size()));
}

void Settings::announce(const Setting& setting)
{
    // The key only: the value may be a password.
    log_.info("settings", "%s changed", setting.key().c_str());
    events_.raiseSettingChanged(setting);
}

}  // namespace espmgr
