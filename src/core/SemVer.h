#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace espmgr
{

// A Semantic Versioning 2.0 version, such as "1.4.2" or "2.0.0-beta.1+build.7" (US17.1.3).
struct SemVer
{
    uint64_t major = 0;
    uint64_t minor = 0;
    uint64_t patch = 0;
    // The pre-release identifiers ("beta", "1"); empty for a release. Build metadata isn't
    // kept, since it doesn't count when comparing.
    std::vector<std::string> preRelease;

    // Nothing if `text` isn't a SemVer version: no "v" prefix, no leading zeros, no missing
    // parts.
    static std::optional<SemVer> parse(const std::string& text);

    // Negative, zero or positive as `a` is older than, as new as or newer than `b`, by SemVer
    // precedence: "1.10.0" is newer than "1.9.5", "1.5.0-beta.1" older than "1.5.0".
    static int compare(const SemVer& a, const SemVer& b);
};

}  // namespace espmgr
