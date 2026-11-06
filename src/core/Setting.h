#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace espmgr
{

class Settings;

// A named, typed value the Developer declares for their firmware through esp.settings(), which
// the Owner edits in the web pages (EP06). The library owns the value, so every change is
// validated, stored and reported the same way wherever it comes from.
//
// This base class is what the web pages and onSettingChanged listeners see, with values as text.
// A sketch keeps the typed handle that add…() returns, such as NumberSetting.
class Setting
{
public:
    enum class Type : uint8_t
    {
        Text,
        Password,
        Number,
        Checkbox,
        Select
    };

    // What makes a value unacceptable.
    enum class Problem : uint8_t
    {
        None,
        Required,          // empty, but .required()
        TooLong,           // more bytes than .maxLength()
        InvalidCharacter,  // a control character, or not valid UTF-8
        NotANumber,        // not a whole number in decimal
        OutOfRange,        // outside .range()
        NotACheckbox,      // not "1" or "0"
        UnknownOption,     // not one of the .option() values
        Rejected,          // the Developer's .validate() refused it; see Check::message
        WrongType,         // stored as another type, by firmware that declared it differently
        UnknownSetting,    // no Setting with this key
        NotStored          // storing failed, or begin() hasn't been called
    };

    // The result of checking a value.
    struct Check
    {
        Problem problem = Problem::None;
        std::string message;  // the Developer's error text, for Problem::Rejected

        bool ok() const
        {
            return problem == Problem::None;
        }
    };

    Setting(Settings& owner, Type type, std::string key, std::string label)
        : owner_(owner), type_(type), key_(std::move(key)), label_(std::move(label))
    {
    }

    virtual ~Setting() = default;

    Setting(const Setting&) = delete;
    Setting& operator=(const Setting&) = delete;

    Type type() const
    {
        return type_;
    }

    const std::string& key() const
    {
        return key_;
    }

    const std::string& label() const
    {
        return label_;
    }

    // The title of its Section; empty for the default one.
    const std::string& section() const
    {
        return section_;
    }

    // False if the declaration was a mistake (a duplicate or invalid key, or a declaration after
    // begin()). Such a Setting keeps its default, and isn't stored or shown.
    bool valid() const
    {
        return valid_;
    }

    bool isRequired() const
    {
        return required_;
    }

    bool isEditableInManagement() const
    {
        return editableInManagement_;
    }

    // True if it has no value: an empty text, password or selection.
    virtual bool isEmpty() const = 0;

    // The value as the web pages show it: "1" or "0" for a checkbox, and always empty for a
    // password, which never goes to a page (use isEmpty() to see whether one is set).
    virtual std::string text() const = 0;

    // Checks a value as the web pages send it, without changing anything.
    virtual Check checkText(const std::string& text) const = 0;

    // Checks, stores and reports a value as the web pages send it. False if it was refused or
    // couldn't be stored, changing nothing.
    virtual bool setText(const std::string& text) = 0;

protected:
    // Reads the stored value. False if there is none of this type.
    bool readStored(std::string& value);
    bool readStored(int32_t& value);
    bool readStored(bool& value);
    // True if the key holds a value of any type.
    bool storedAsAnyType();
    bool writeStored(const std::string& value);
    bool writeStored(int32_t value);
    bool writeStored(bool value);
    bool removeStored();

    // False, with a warning, if the value can't change now.
    bool canChange();
    void announceChange();
    void warnStoredValue(Problem problem);
    void warnDefaultValue(Problem problem);
    void warnOption(const char* message);
    void warnStoreFailed();

    // For .required() and .editableInManagement() in the classes below.
    bool required_ = false;
    bool editableInManagement_ = false;

private:
    friend class Settings;

    // Reads the stored value, or the default if there is none or it no longer fits.
    virtual void load() = 0;
    // Stores a value that has passed its checks, without reporting it. Sets `changed`. Returns
    // false, changing nothing, if storing failed.
    virtual bool commitText(const std::string& text, bool& changed) = 0;
    // The value as text, passwords included. Never for a page or the logger.
    virtual std::string rawText() const = 0;
    // Back to the default, after the stored values are gone. Returns true if the value changed.
    virtual bool restoreDefault() = 0;

    Settings& owner_;
    Type type_;
    std::string key_;
    std::string label_;
    std::string section_;
    bool valid_ = true;
};

// A short English name for logs, such as "out of range".
const char* toString(Setting::Problem problem);

namespace detail
{

inline bool hasNoValue(const std::string& value)
{
    return value.empty();
}

inline bool hasNoValue(int32_t)
{
    return false;
}

inline bool hasNoValue(bool)
{
    return false;
}

}  // namespace detail

// A Setting holding a T, with the parts every type shares. `Self` is the final class, so the
// chained calls return it.
template <typename T, typename Self> class TypedSetting : public Setting
{
public:
    using Validator = std::function<std::string(const T&)>;

    TypedSetting(Settings& owner, Type type, std::string key, std::string label, T defaultValue)
        : Setting(owner, type, std::move(key), std::move(label)), default_(defaultValue),
          value_(std::move(defaultValue))
    {
    }

    const T& value() const
    {
        return value_;
    }

    const T& defaultValue() const
    {
        return default_;
    }

    // Lets the Owner edit it in the Management Interface too. Off by default.
    Self& editableInManagement()
    {
        editableInManagement_ = true;
        return self();
    }

    // Your own check, after the built-in ones. Return an error text to show the Owner, or an empty
    // string to accept the value. It isn't called for an empty value. It's also called for the
    // stored value at begin(), so it mustn't depend on anything set up later.
    Self& validate(Validator validator)
    {
        validator_ = std::move(validator);
        return self();
    }

    Check check(const T& value) const
    {
        if (required_ && detail::hasNoValue(value))
        {
            return {Problem::Required, {}};
        }
        return checkContent(value);
    }

    // Checks and stores the value, and reports it to onSettingChanged listeners from process().
    // False if it was refused or couldn't be stored, or begin() hasn't been called, changing
    // nothing.
    bool set(const T& value)
    {
        if (!canChange() || !check(value).ok())
        {
            return false;
        }
        bool changed = false;
        if (!commit(value, changed))
        {
            return false;
        }
        if (changed)
        {
            announceChange();
        }
        return true;
    }

    bool isEmpty() const override
    {
        return detail::hasNoValue(value_);
    }

    Check checkText(const std::string& text) const override
    {
        T value{};
        const Problem problem = parse(text, value);
        if (problem != Problem::None)
        {
            return {problem, {}};
        }
        return check(value);
    }

    bool setText(const std::string& text) override
    {
        T value{};
        return parse(text, value) == Problem::None && set(value);
    }

protected:
    // Turns text from a web page into a value.
    virtual Problem parse(const std::string& text, T& value) const = 0;
    // The type's own rules, apart from .required().
    virtual Problem checkBuiltIn(const T& value) const = 0;

    Self& self()
    {
        return static_cast<Self&>(*this);
    }

private:
    Check checkContent(const T& value) const
    {
        const Problem problem = checkBuiltIn(value);
        if (problem != Problem::None)
        {
            return {problem, {}};
        }
        if (validator_ && !detail::hasNoValue(value))
        {
            std::string message = validator_(value);
            if (!message.empty())
            {
                return {Problem::Rejected, std::move(message)};
            }
        }
        return {};
    }

    void load() override
    {
        value_ = default_;
        stale_ = false;
        const Problem defaultProblem = checkBuiltIn(default_);
        if (defaultProblem != Problem::None)
        {
            warnDefaultValue(defaultProblem);
        }
        T stored{};
        if (readStored(stored))
        {
            const Check result = checkContent(stored);
            if (result.ok())
            {
                value_ = std::move(stored);
                return;
            }
            stale_ = true;
            warnStoredValue(result.problem);
        }
        else if (storedAsAnyType())
        {
            stale_ = true;
            warnStoredValue(Problem::WrongType);
        }
    }

    bool commit(const T& value, bool& changed)
    {
        changed = !(value == value_);
        if (!changed && !stale_)
        {
            return true;
        }
        // A value of another type under this key isn't replaced by writing the new one.
        const bool stored = (value == default_ || !stale_ || removeStored()) &&
                            (value == default_ ? removeStored() : writeStored(value));
        if (!stored)
        {
            changed = false;
            warnStoreFailed();
            return false;
        }
        value_ = value;
        stale_ = false;
        return true;
    }

    bool commitText(const std::string& text, bool& changed) override
    {
        T value{};
        changed = false;
        return parse(text, value) == Problem::None && commit(value, changed);
    }

    bool restoreDefault() override
    {
        const bool changed = !(value_ == default_);
        value_ = default_;
        stale_ = false;
        return changed;
    }

    T default_;
    T value_;
    Validator validator_;
    bool stale_ = false;  // storage holds a value that didn't fit, until the next save
};

// The built-in rules for text and password values: the length in bytes, UTF-8 and no control
// characters.
Setting::Problem checkTextValue(const std::string& value, size_t maxLength);

// The parts text and password Settings share.
template <typename Self> class LimitedTextSetting : public TypedSetting<std::string, Self>
{
public:
    static constexpr size_t kDefaultMaxLength = 64;
    static constexpr size_t kMaxLengthLimit = 512;

    using TypedSetting<std::string, Self>::TypedSetting;

    // The Owner must enter a value during setup.
    Self& required()
    {
        this->required_ = true;
        return this->self();
    }

    // The longest value in bytes, 1 to 512; 64 by default. Anything else is ignored.
    Self& maxLength(size_t length)
    {
        if (length < 1 || length > kMaxLengthLimit)
        {
            this->warnOption("a maximum length outside 1 to 512");
        }
        else
        {
            maxLength_ = length;
        }
        return this->self();
    }

    size_t maxLengthValue() const
    {
        return maxLength_;
    }

protected:
    Setting::Problem parse(const std::string& text, std::string& value) const override
    {
        value = text;
        return Setting::Problem::None;
    }

    Setting::Problem checkBuiltIn(const std::string& value) const override
    {
        return checkTextValue(value, maxLength_);
    }

    std::string rawText() const override
    {
        return this->value();
    }

private:
    size_t maxLength_ = kDefaultMaxLength;
};

// Free text, such as a server's name. UTF-8; no control characters.
class TextSetting : public LimitedTextSetting<TextSetting>
{
public:
    TextSetting(Settings& owner, std::string key, std::string label, std::string defaultValue)
        : LimitedTextSetting(owner, Type::Text, std::move(key), std::move(label),
                             std::move(defaultValue))
    {
    }

    std::string text() const override
    {
        return value();
    }
};

// Like text, but the value never goes to a web page or the logger. It's stored as plain text in
// NVS, like Wi-Fi passwords, unless the Developer enables flash and NVS encryption.
class PasswordSetting : public LimitedTextSetting<PasswordSetting>
{
public:
    PasswordSetting(Settings& owner, std::string key, std::string label)
        : LimitedTextSetting(owner, Type::Password, std::move(key), std::move(label), {})
    {
    }

    std::string text() const override
    {
        return {};
    }
};

// A whole number.
class NumberSetting : public TypedSetting<int32_t, NumberSetting>
{
public:
    NumberSetting(Settings& owner, std::string key, std::string label, int32_t defaultValue)
        : TypedSetting(owner, Type::Number, std::move(key), std::move(label), defaultValue)
    {
    }

    // The lowest and highest value accepted. Ignored if `min` is above `max`.
    NumberSetting& range(int32_t min, int32_t max);

    int32_t min() const
    {
        return min_;
    }

    int32_t max() const
    {
        return max_;
    }

    std::string text() const override;

protected:
    Problem parse(const std::string& text, int32_t& value) const override;
    Problem checkBuiltIn(const int32_t& value) const override;

private:
    std::string rawText() const override
    {
        return text();
    }

    int32_t min_ = std::numeric_limits<int32_t>::min();
    int32_t max_ = std::numeric_limits<int32_t>::max();
};

// On or off.
class CheckboxSetting : public TypedSetting<bool, CheckboxSetting>
{
public:
    CheckboxSetting(Settings& owner, std::string key, std::string label, bool defaultValue)
        : TypedSetting(owner, Type::Checkbox, std::move(key), std::move(label), defaultValue)
    {
    }

    std::string text() const override
    {
        return value() ? "1" : "0";
    }

protected:
    Problem parse(const std::string& text, bool& value) const override;
    Problem checkBuiltIn(const bool&) const override
    {
        return Problem::None;
    }

private:
    std::string rawText() const override
    {
        return text();
    }
};

// One of a list of options. The value is the option's own value, which is what's stored, so
// labels can change freely. Empty means nothing is selected.
class SelectSetting : public TypedSetting<std::string, SelectSetting>
{
public:
    struct Option
    {
        std::string value;
        std::string label;
    };

    SelectSetting(Settings& owner, std::string key, std::string label, std::string defaultValue)
        : TypedSetting(owner, Type::Select, std::move(key), std::move(label),
                       std::move(defaultValue))
    {
    }

    // The Owner must choose an option during setup.
    SelectSetting& required()
    {
        required_ = true;
        return *this;
    }

    // Adds an option, shown in this order. An empty or repeated value is ignored.
    SelectSetting& option(const std::string& value, const std::string& label);

    const std::vector<Option>& options() const
    {
        return options_;
    }

    std::string text() const override
    {
        return value();
    }

protected:
    Problem parse(const std::string& text, std::string& value) const override
    {
        value = text;
        return Problem::None;
    }

    Problem checkBuiltIn(const std::string& value) const override;

private:
    std::string rawText() const override
    {
        return value();
    }

    std::vector<Option> options_;
};

}  // namespace espmgr
