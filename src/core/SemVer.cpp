#include "core/SemVer.h"

#include <cstddef>

namespace espmgr
{

namespace
{

bool isDigits(const std::string& text)
{
    if (text.empty())
    {
        return false;
    }
    for (const char c : text)
    {
        if (c < '0' || c > '9')
        {
            return false;
        }
    }
    return true;
}

// A numeric part: digits without a leading zero, fitting 64 bits.
std::optional<uint64_t> number(const std::string& text)
{
    if (!isDigits(text) || (text.size() > 1 && text[0] == '0') || text.size() > 19)
    {
        return std::nullopt;
    }
    uint64_t value = 0;
    for (const char c : text)
    {
        value = value * 10 + static_cast<uint64_t>(c - '0');
    }
    return value;
}

// Letters, digits and hyphens, not empty.
bool isIdentifier(const std::string& text)
{
    if (text.empty())
    {
        return false;
    }
    for (const char c : text)
    {
        const bool ok =
            (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-';
        if (!ok)
        {
            return false;
        }
    }
    return true;
}

std::vector<std::string> split(const std::string& text, char separator)
{
    std::vector<std::string> parts;
    size_t start = 0;
    for (;;)
    {
        const size_t end = text.find(separator, start);
        parts.push_back(
            text.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos)
        {
            return parts;
        }
        start = end + 1;
    }
}

int compareIdentifiers(const std::string& a, const std::string& b)
{
    const bool aNumeric = isDigits(a);
    const bool bNumeric = isDigits(b);
    if (aNumeric && bNumeric)
    {
        if (a.size() != b.size())
        {
            return a.size() < b.size() ? -1 : 1;
        }
        return a.compare(b) < 0 ? -1 : (a == b ? 0 : 1);
    }
    if (aNumeric != bNumeric)
    {
        // Numeric identifiers come before alphanumeric ones.
        return aNumeric ? -1 : 1;
    }
    const int result = a.compare(b);
    return result < 0 ? -1 : (result > 0 ? 1 : 0);
}

}  // namespace

std::optional<SemVer> SemVer::parse(const std::string& text)
{
    std::string rest = text;
    const size_t plus = rest.find('+');
    if (plus != std::string::npos)
    {
        for (const std::string& part : split(rest.substr(plus + 1), '.'))
        {
            if (!isIdentifier(part))
            {
                return std::nullopt;
            }
        }
        rest.resize(plus);
    }
    SemVer version;
    const size_t dash = rest.find('-');
    if (dash != std::string::npos)
    {
        for (const std::string& part : split(rest.substr(dash + 1), '.'))
        {
            if (!isIdentifier(part) || (isDigits(part) && part.size() > 1 && part[0] == '0'))
            {
                return std::nullopt;
            }
            version.preRelease.push_back(part);
        }
        rest.resize(dash);
    }
    const std::vector<std::string> core = split(rest, '.');
    if (core.size() != 3)
    {
        return std::nullopt;
    }
    const std::optional<uint64_t> major = number(core[0]);
    const std::optional<uint64_t> minor = number(core[1]);
    const std::optional<uint64_t> patch = number(core[2]);
    if (!major || !minor || !patch)
    {
        return std::nullopt;
    }
    version.major = *major;
    version.minor = *minor;
    version.patch = *patch;
    return version;
}

int SemVer::compare(const SemVer& a, const SemVer& b)
{
    if (a.major != b.major)
    {
        return a.major < b.major ? -1 : 1;
    }
    if (a.minor != b.minor)
    {
        return a.minor < b.minor ? -1 : 1;
    }
    if (a.patch != b.patch)
    {
        return a.patch < b.patch ? -1 : 1;
    }
    // A release is newer than its pre-releases.
    if (a.preRelease.empty() || b.preRelease.empty())
    {
        if (a.preRelease.empty() && b.preRelease.empty())
        {
            return 0;
        }
        return a.preRelease.empty() ? 1 : -1;
    }
    const size_t count =
        a.preRelease.size() < b.preRelease.size() ? a.preRelease.size() : b.preRelease.size();
    for (size_t i = 0; i < count; ++i)
    {
        const int result = compareIdentifiers(a.preRelease[i], b.preRelease[i]);
        if (result != 0)
        {
            return result;
        }
    }
    if (a.preRelease.size() == b.preRelease.size())
    {
        return 0;
    }
    return a.preRelease.size() < b.preRelease.size() ? -1 : 1;
}

}  // namespace espmgr
