#include "core/SavedNetwork.h"

#include <cctype>

namespace espmgr
{

namespace
{

bool isNetmask(const Ipv4Address& mask)
{
    const uint32_t bits = (uint32_t{mask[0]} << 24) | (uint32_t{mask[1]} << 16) |
                          (uint32_t{mask[2]} << 8) | uint32_t{mask[3]};
    // The ones must be contiguous from the top: inverting gives 0…01…1, and adding one to that
    // leaves a single bit set.
    const uint32_t hostBits = ~bits;
    return bits != 0 && (hostBits & (hostBits + 1)) == 0;
}

bool allHex(const std::string& text)
{
    for (unsigned char c : text)
    {
        if (!std::isxdigit(c))
        {
            return false;
        }
    }
    return true;
}

bool allPrintableAscii(const std::string& text)
{
    for (unsigned char c : text)
    {
        if (c < 0x20 || c > 0x7e)
        {
            return false;
        }
    }
    return true;
}

}  // namespace

SavedNetwork::Problem SavedNetwork::validate() const
{
    if (ssid.empty())
    {
        return Problem::SsidEmpty;
    }
    if (ssid.size() > kMaxSsidLength)
    {
        return Problem::SsidTooLong;
    }
    if (!password.empty())
    {
        if (password.size() < kMinPasswordLength)
        {
            return Problem::PasswordTooShort;
        }
        if (password.size() > kHexKeyLength)
        {
            return Problem::PasswordTooLong;
        }
        if (password.size() == kHexKeyLength && !allHex(password))
        {
            return Problem::PasswordNotHex;
        }
        if (!allPrintableAscii(password))
        {
            return Problem::PasswordInvalidCharacter;
        }
    }
    if (!dhcp)
    {
        if (staticIp.ip.isUnset() || staticIp.gateway.isUnset())
        {
            return Problem::StaticIpMissing;
        }
        if (!isNetmask(staticIp.subnet))
        {
            return Problem::SubnetInvalid;
        }
    }
    return Problem::None;
}

const char* toString(SavedNetwork::Problem problem)
{
    switch (problem)
    {
    case SavedNetwork::Problem::None:
        return "none";
    case SavedNetwork::Problem::SsidEmpty:
        return "SSID is empty";
    case SavedNetwork::Problem::SsidTooLong:
        return "SSID is longer than 32 bytes";
    case SavedNetwork::Problem::PasswordTooShort:
        return "password is shorter than 8 characters";
    case SavedNetwork::Problem::PasswordTooLong:
        return "password is longer than 64 characters";
    case SavedNetwork::Problem::PasswordInvalidCharacter:
        return "password has a character other than printable ASCII";
    case SavedNetwork::Problem::PasswordNotHex:
        return "64-character password is not hexadecimal";
    case SavedNetwork::Problem::StaticIpMissing:
        return "static IP or gateway is missing";
    case SavedNetwork::Problem::SubnetInvalid:
        return "subnet mask is invalid";
    }
    return "unknown";
}

bool operator==(const SavedNetwork& a, const SavedNetwork& b)
{
    const StaticIpConfig& x = a.staticIp;
    const StaticIpConfig& y = b.staticIp;
    return a.ssid == b.ssid && a.password == b.password && a.priority == b.priority &&
           a.dhcp == b.dhcp && x.ip == y.ip && x.gateway == y.gateway && x.subnet == y.subnet &&
           x.dns1 == y.dns1 && x.dns2 == y.dns2;
}

}  // namespace espmgr
