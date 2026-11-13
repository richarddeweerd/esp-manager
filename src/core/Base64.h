#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace espmgr
{

// Decodes standard base64 (RFC 4648, with or without "=" padding). Nothing if `text` holds any
// other character or has an impossible length.
std::optional<std::vector<uint8_t>> decodeBase64(const std::string& text);

}  // namespace espmgr
