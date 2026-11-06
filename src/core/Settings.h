#pragma once

#include "core/Events.h"
#include "core/Logger.h"
#include "core/Setting.h"
#include "core/Storage.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace espmgr
{

class Settings;

// A named group of Settings, shown together in the web pages (US06.2.1). Reached through
// esp.settings().section("MQTT"); the Settings keep the order they were declared in.
class SettingsSection
{
public:
    SettingsSection(Settings& owner, std::string title) : owner_(owner), title_(std::move(title))
    {
    }

    SettingsSection(const SettingsSection&) = delete;
    SettingsSection& operator=(const SettingsSection&) = delete;

    // See Settings for what these do.
    TextSetting& addText(const std::string& key, const std::string& label,
                         const std::string& defaultValue = {});
    PasswordSetting& addPassword(const std::string& key, const std::string& label);
    NumberSetting& addNumber(const std::string& key, const std::string& label,
                             int32_t defaultValue = 0);
    CheckboxSetting& addCheckbox(const std::string& key, const std::string& label,
                                 bool defaultValue = false);
    SelectSetting& addSelect(const std::string& key, const std::string& label,
                             const std::string& defaultValue = {});

    // Empty for the default Section.
    const std::string& title() const
    {
        return title_;
    }

    const std::vector<Setting*>& settings() const
    {
        return settings_;
    }

private:
    friend class Settings;

    Settings& owner_;
    std::string title_;
    std::vector<Setting*> settings_;
};

// The Settings, reached through esp.settings() (EP06). Declare them before esp.begin(), which
// loads their values, and keep the handles, or look values up by key:
//
//   auto& host = esp.settings().section("MQTT").addText("mqtt_host", "MQTT server").required();
//   auto& port = esp.settings().section("MQTT").addNumber("mqtt_port", "Port", 1883)
//                    .range(1, 65535);
//   esp.begin();
//   connect(host.value(), port.value());
//
// A key is 1 to 15 letters, digits, '_' and '-', and unique; it's also the name in NVS, so changing
// it loses the stored value. A mistake in a declaration logs an error, and gives a Setting that
// holds its default, isn't stored or shown, and answers false to valid().
//
// Only values that differ from the default are stored, each under its own key, so a new default
// in firmware reaches every Setting the Owner never changed. A stored value that no longer fits
// reads as the default and stays stored until the Setting is next saved.
class Settings
{
public:
    // The storage namespace; a Settings Reset clears it.
    static constexpr const char* kNamespace = "espmgr-set";

    // A value that was refused, from check() and apply().
    struct FieldProblem
    {
        std::string key;
        Setting::Check check;
    };

    Settings(Storage& storage, Events& events, Logger& log)
        : storage_(storage), events_(events), log_(log)
    {
    }

    Settings(const Settings&) = delete;
    Settings& operator=(const Settings&) = delete;

    // The Section with this title, made on first use. An empty title is the default Section.
    SettingsSection& section(const std::string& title);

    // Declares a Setting in the default Section.
    TextSetting& addText(const std::string& key, const std::string& label,
                         const std::string& defaultValue = {});
    PasswordSetting& addPassword(const std::string& key, const std::string& label);
    NumberSetting& addNumber(const std::string& key, const std::string& label,
                             int32_t defaultValue = 0);
    CheckboxSetting& addCheckbox(const std::string& key, const std::string& label,
                                 bool defaultValue = false);
    SelectSetting& addSelect(const std::string& key, const std::string& label,
                             const std::string& defaultValue = {});

    // The current value by key. Empty, 0 or false, with a warning, if there's no such Setting or
    // it's of another type. text() is for text, password and select Settings.
    std::string text(const std::string& key) const;
    int32_t number(const std::string& key) const;
    bool checkbox(const std::string& key) const;

    // The Setting with this key, or nullptr.
    Setting* find(const std::string& key) const;

    // The Sections in the order they were declared.
    const std::deque<SettingsSection>& sections() const
    {
        return sections_;
    }

    size_t count() const
    {
        return settings_.size();
    }

    // False while a required Setting is empty, for example after a firmware update that added
    // one. Wi-Fi doesn't depend on this; call esp.startPortal() if your firmware does.
    bool complete() const;

    // Checks values as the web pages send them (key, text), without changing anything. Empty if
    // all are fine. A Setting that isn't mentioned isn't checked.
    std::vector<FieldProblem> check(const std::vector<std::pair<std::string, std::string>>& fields);

    // Stores values as the web pages send them, all or nothing, and reports the ones that changed.
    // Empty if they were stored.
    std::vector<FieldProblem> apply(const std::vector<std::pair<std::string, std::string>>& fields);

    // Settings Reset: every Setting back to its default, reporting the ones that changed. Returns
    // false, changing nothing, if the stored values couldn't be erased.
    bool reset();

    // Reads the stored values and allows changes. Called by esp.begin().
    void begin();
    // Reads the stored values again, without reporting anything. Called after a Factory Reset.
    void load();

private:
    friend class Setting;
    friend class SettingsSection;

    template <typename T, typename... Args>
    T& declare(SettingsSection& section, const std::string& key, const std::string& label,
               Args&&... args);
    // Why this key can't be declared, or nullptr if it can.
    const char* declarationMistake(const std::string& key) const;
    void announce(const Setting& setting);

    Storage& storage_;
    Events& events_;
    Logger& log_;
    std::deque<SettingsSection> sections_;
    std::vector<std::unique_ptr<Setting>> settings_;
    // Declarations that were refused. They live on, because the sketch holds references to them.
    std::vector<std::unique_ptr<Setting>> refused_;
    bool loaded_ = false;
};

}  // namespace espmgr
