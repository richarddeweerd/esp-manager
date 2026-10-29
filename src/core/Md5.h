#pragma once

#include <string>
#include <string_view>

namespace espmgr
{

// MD5 of `data` as 32 lower-case hex digits. Only for HTTP Digest authentication, which
// requires it; MD5 is not a secure hash for anything else.
std::string md5Hex(std::string_view data);

}  // namespace espmgr
