#pragma once

#include "core/WifiDriver.h"

#include <cstddef>
#include <string>

namespace espmgr
{

// The default device name, used as the hostname and later as the Provisioning Portal's SSID:
// "<prefix>-<last 6 hex digits of the MAC>", such as "esp-abcdef".
constexpr const char* kDefaultNamePrefix = "esp";

// Longest hostname kept: Arduino-ESP32 stores at most 31 characters.
constexpr size_t kMaxHostnameLength = 31;

// A DNS label: 1 to kMaxHostnameLength letters, digits and hyphens, not starting or ending with
// a hyphen.
bool isValidHostname(const std::string& name);

// "<prefix>-xxxxxx" with the last three bytes of `mac` in lower-case hex.
std::string defaultDeviceName(const std::string& prefix, const MacAddress& mac);

// True if `prefix` makes a valid default device name.
bool isValidNamePrefix(const std::string& prefix);

}  // namespace espmgr
