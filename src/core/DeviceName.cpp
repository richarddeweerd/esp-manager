#include "core/DeviceName.h"

#include <cctype>
#include <cstdio>

namespace espmgr
{

bool isValidHostname(const std::string& name)
{
    if (name.empty() || name.size() > kMaxHostnameLength || name.front() == '-' ||
        name.back() == '-')
    {
        return false;
    }
    for (unsigned char c : name)
    {
        if (!std::isalnum(c) && c != '-')
        {
            return false;
        }
    }
    return true;
}

std::string defaultDeviceName(const std::string& prefix, const MacAddress& mac)
{
    char suffix[8];
    std::snprintf(suffix, sizeof(suffix), "-%02x%02x%02x", mac[3], mac[4], mac[5]);
    return prefix + suffix;
}

bool isValidNamePrefix(const std::string& prefix)
{
    return isValidHostname(defaultDeviceName(prefix, MacAddress{})) && !prefix.empty();
}

}  // namespace espmgr
