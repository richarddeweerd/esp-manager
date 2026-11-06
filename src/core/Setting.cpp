#include "core/Setting.h"

#include "core/Settings.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>

namespace espmgr
{

namespace
{

// Control characters, and anything that isn't well-formed UTF-8.
bool isCleanText(const std::string& text)
{
    size_t i = 0;
    while (i < text.size())
    {
        const uint8_t first = static_cast<uint8_t>(text[i]);
        if (first < 0x20 || first == 0x7f)
        {
            return false;
        }
        size_t continuation = 0;
        uint32_t lowest = 0;
        uint32_t point = first;
        if (first >= 0xf0 && first <= 0xf4)
        {
            continuation = 3;
            lowest = 0x10000;
            point = first & 0x07;
        }
        else if (first >= 0xe0 && first < 0xf0)
        {
            continuation = 2;
            lowest = 0x800;
            point = first & 0x0f;
        }
        else if (first >= 0xc2 && first < 0xe0)
        {
            continuation = 1;
            lowest = 0x80;
            point = first & 0x1f;
        }
        else if (first >= 0x80)
        {
            return false;
        }
        if (continuation >= text.size() - i)
        {
            return false;
        }
        for (size_t c = 1; c <= continuation; ++c)
        {
            const uint8_t next = static_cast<uint8_t>(text[i + c]);
            if ((next & 0xc0) != 0x80)
            {
                return false;
            }
            point = (point << 6) | (next & 0x3f);
        }
        // Overlong forms, surrogates and points past the end of Unicode.
        if (continuation > 0 &&
            (point < lowest || (point >= 0xd800 && point <= 0xdfff) || point > 0x10ffff))
        {
            return false;
        }
        i += continuation + 1;
    }
    return true;
}

}  // namespace

const char* toString(Setting::Problem problem)
{
    switch (problem)
    {
    case Setting::Problem::None:
        return "no problem";
    case Setting::Problem::Required:
        return "required";
    case Setting::Problem::TooLong:
        return "too long";
    case Setting::Problem::InvalidCharacter:
        return "invalid character";
    case Setting::Problem::NotANumber:
        return "not a number";
    case Setting::Problem::OutOfRange:
        return "out of range";
    case Setting::Problem::NotACheckbox:
        return "not on or off";
    case Setting::Problem::UnknownOption:
        return "unknown option";
    case Setting::Problem::Rejected:
        return "refused by the sketch";
    case Setting::Problem::WrongType:
        return "stored as another type";
    case Setting::Problem::UnknownSetting:
        return "unknown Setting";
    case Setting::Problem::NotStored:
        return "not stored";
    }
    return "unknown";
}

const char* errorCode(Setting::Problem problem)
{
    switch (problem)
    {
    case Setting::Problem::None:
        return "";
    case Setting::Problem::Required:
        return "required";
    case Setting::Problem::TooLong:
        return "too-long";
    case Setting::Problem::InvalidCharacter:
        return "invalid-character";
    case Setting::Problem::NotANumber:
        return "not-a-number";
    case Setting::Problem::OutOfRange:
        return "out-of-range";
    case Setting::Problem::NotACheckbox:
        return "not-a-checkbox";
    case Setting::Problem::UnknownOption:
        return "unknown-option";
    case Setting::Problem::Rejected:
        return "rejected";
    case Setting::Problem::WrongType:
        return "wrong-type";
    case Setting::Problem::UnknownSetting:
        return "unknown-setting";
    case Setting::Problem::NotStored:
        return "not-stored";
    }
    return "other";
}

bool Setting::readStored(std::string& value)
{
    auto stored = owner_.storage_.getString(Settings::kNamespace, key_);
    if (!stored)
    {
        return false;
    }
    value = std::move(*stored);
    return true;
}

bool Setting::readStored(int32_t& value)
{
    const auto stored = owner_.storage_.getU32(Settings::kNamespace, key_);
    if (!stored)
    {
        return false;
    }
    value = static_cast<int32_t>(*stored);
    return true;
}

bool Setting::readStored(bool& value)
{
    const auto stored = owner_.storage_.getBool(Settings::kNamespace, key_);
    if (!stored)
    {
        return false;
    }
    value = *stored;
    return true;
}

bool Setting::storedAsAnyType()
{
    Storage& storage = owner_.storage_;
    return storage.getString(Settings::kNamespace, key_) ||
           storage.getU32(Settings::kNamespace, key_) ||
           storage.getBool(Settings::kNamespace, key_);
}

bool Setting::writeStored(const std::string& value)
{
    return owner_.storage_.putString(Settings::kNamespace, key_, value);
}

bool Setting::writeStored(int32_t value)
{
    return owner_.storage_.putU32(Settings::kNamespace, key_, static_cast<uint32_t>(value));
}

bool Setting::writeStored(bool value)
{
    return owner_.storage_.putBool(Settings::kNamespace, key_, value);
}

bool Setting::removeStored()
{
    return owner_.storage_.remove(Settings::kNamespace, key_);
}

bool Setting::canChange()
{
    if (!valid_)
    {
        owner_.log_.warn("settings", "%s: not changed, its declaration was refused", key_.c_str());
        return false;
    }
    if (!owner_.loaded_)
    {
        owner_.log_.warn("settings", "%s: not changed, begin() hasn't been called", key_.c_str());
        return false;
    }
    return true;
}

void Setting::announceChange()
{
    owner_.announce(*this);
}

void Setting::warnStoredValue(Problem problem)
{
    owner_.log_.warn("settings", "%s: using the default, the stored value fails: %s", key_.c_str(),
                     toString(problem));
}

void Setting::warnDefaultValue(Problem problem)
{
    owner_.log_.error("settings", "%s: the default value fails: %s", key_.c_str(),
                      toString(problem));
}

void Setting::warnOption(const char* message)
{
    owner_.log_.warn("settings", "%s: ignoring %s", key_.c_str(), message);
}

void Setting::warnStoreFailed()
{
    owner_.log_.error("settings", "%s: couldn't store the value", key_.c_str());
}

Setting::Problem checkTextValue(const std::string& value, size_t maxLength)
{
    if (value.size() > maxLength)
    {
        return Setting::Problem::TooLong;
    }
    return isCleanText(value) ? Setting::Problem::None : Setting::Problem::InvalidCharacter;
}

NumberSetting& NumberSetting::range(int32_t min, int32_t max)
{
    if (min > max)
    {
        warnOption("a range with its minimum above its maximum");
    }
    else
    {
        min_ = min;
        max_ = max;
    }
    return *this;
}

std::string NumberSetting::text() const
{
    char text[12];
    std::snprintf(text, sizeof(text), "%ld", static_cast<long>(value()));
    return text;
}

Setting::Problem NumberSetting::parse(const std::string& text, int32_t& value) const
{
    if (text.empty() || !(text[0] == '-' || (text[0] >= '0' && text[0] <= '9')))
    {
        return Problem::NotANumber;
    }
    errno = 0;
    char* end = nullptr;
    const long long parsed = std::strtoll(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0' || end != text.c_str() + text.size())
    {
        return Problem::NotANumber;
    }
    if (errno == ERANGE || parsed < std::numeric_limits<int32_t>::min() ||
        parsed > std::numeric_limits<int32_t>::max())
    {
        return Problem::OutOfRange;
    }
    value = static_cast<int32_t>(parsed);
    return Problem::None;
}

Setting::Problem NumberSetting::checkBuiltIn(const int32_t& value) const
{
    return value < min_ || value > max_ ? Problem::OutOfRange : Problem::None;
}

Setting::Problem CheckboxSetting::parse(const std::string& text, bool& value) const
{
    if (text != "1" && text != "0")
    {
        return Problem::NotACheckbox;
    }
    value = text == "1";
    return Problem::None;
}

SelectSetting& SelectSetting::option(const std::string& value, const std::string& label)
{
    const bool repeated = std::any_of(options_.begin(), options_.end(),
                                      [&value](const Option& o) { return o.value == value; });
    if (value.empty() || repeated)
    {
        warnOption("an empty or repeated option");
    }
    else
    {
        options_.push_back({value, label});
    }
    return *this;
}

Setting::Problem SelectSetting::checkBuiltIn(const std::string& value) const
{
    if (value.empty())
    {
        return Problem::None;
    }
    const bool known = std::any_of(options_.begin(), options_.end(),
                                   [&value](const Option& o) { return o.value == value; });
    return known ? Problem::None : Problem::UnknownOption;
}

}  // namespace espmgr
