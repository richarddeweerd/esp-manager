#include "core/Base64.h"

namespace espmgr
{

namespace
{

int value(char c)
{
    if (c >= 'A' && c <= 'Z')
    {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z')
    {
        return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9')
    {
        return c - '0' + 52;
    }
    if (c == '+')
    {
        return 62;
    }
    if (c == '/')
    {
        return 63;
    }
    return -1;
}

}  // namespace

std::optional<std::vector<uint8_t>> decodeBase64(const std::string& text)
{
    std::string data = text;
    while (!data.empty() && data.back() == '=')
    {
        data.pop_back();
    }
    if (text.size() - data.size() > 2 || data.size() % 4 == 1)
    {
        return std::nullopt;
    }
    std::vector<uint8_t> bytes;
    bytes.reserve(data.size() * 3 / 4);
    uint32_t buffer = 0;
    int bits = 0;
    for (const char c : data)
    {
        const int v = value(c);
        if (v < 0)
        {
            return std::nullopt;
        }
        buffer = (buffer << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            bytes.push_back(static_cast<uint8_t>((buffer >> bits) & 0xff));
        }
    }
    return bytes;
}

}  // namespace espmgr
