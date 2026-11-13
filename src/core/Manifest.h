#pragma once

#include <cstddef>
#include <optional>
#include <string>

namespace espmgr
{

// The JSON file on the Update Server that describes the latest firmware of one Update Channel
// (US17.1.2), for example
//   {"version": "1.4.2", "url": "firmware-1.4.2.bin", "size": 1048576,
//    "sha256": "…", "signature": "…", "minimumVersion": "1.2.0", "notes": "…"}
// Unknown fields are ignored.
struct Manifest
{
    // The largest Manifest read.
    static constexpr size_t kMaxSize = 4096;
    // Longer release notes are cut off.
    static constexpr size_t kMaxNotes = 1024;

    std::string version;
    // As written; resolveUrl() makes it absolute.
    std::string url;
    size_t size = 0;
    // 64 hex digits, lower case.
    std::string sha256;
    // Base64; may be empty.
    std::string signature;
    std::string minimumVersion;
    std::string notes;

    // Nothing if `json` isn't one JSON object, or lacks version, url, size (above 0) or sha256
    // (64 hex digits). Versions aren't checked here.
    static std::optional<Manifest> parse(const std::string& json);
};

// The image URL of a Manifest fetched from `manifestUrl`: `url` itself when it starts with
// "https://", the origin's path for "/…", or else the Manifest's folder plus `url`. Empty for
// any other scheme, so images only ever come over HTTPS.
std::string resolveUrl(const std::string& manifestUrl, const std::string& url);

// `url` without its query string, for logging: a token in the query must not reach the log.
std::string withoutQuery(const std::string& url);

}  // namespace espmgr
