#pragma once

#include "core/WifiDriver.h"

#include <cstdint>
#include <string>

namespace espmgr
{

// A wireless network the device remembers: its SSID, credentials and priority, plus its own
// address settings.
struct SavedNetwork
{
    // What makes a Saved Network unusable, from validate().
    enum class Problem : uint8_t
    {
        None,
        SsidEmpty,
        SsidTooLong,               // more than 32 bytes
        SsidInvalidCharacter,      // a NUL byte, which would cut the name short
        PasswordTooShort,          // 1 to 7 characters; empty means an open network
        PasswordTooLong,           // more than 64 characters
        PasswordInvalidCharacter,  // a passphrase may only hold printable ASCII
        PasswordNotHex,            // a 64-character password is a raw key in hex
        StaticIpMissing,           // static addressing needs an IP and a gateway
        SubnetInvalid              // not a valid netmask, such as 255.255.255.0
    };

    static constexpr size_t kMaxSsidLength = 32;
    static constexpr size_t kMinPasswordLength = 8;
    static constexpr size_t kMaxPassphraseLength = 63;
    static constexpr size_t kHexKeyLength = 64;

    std::string ssid;
    std::string password;  // never log it
    // Networks with a lower number are tried first; signal strength breaks ties.
    uint8_t priority = 0;
    // False uses staticIp instead of DHCP.
    bool dhcp = true;
    StaticIpConfig staticIp;

    Problem validate() const;
};

const char* toString(SavedNetwork::Problem problem);

bool operator==(const SavedNetwork& a, const SavedNetwork& b);

inline bool operator!=(const SavedNetwork& a, const SavedNetwork& b)
{
    return !(a == b);
}

}  // namespace espmgr
