#include "core/Ipv4Address.h"

namespace espmgr
{

std::optional<Ipv4Address> Ipv4Address::parse(std::string_view text)
{
    std::array<uint8_t, 4> bytes{};
    size_t pos = 0;
    for (size_t part = 0; part < bytes.size(); ++part)
    {
        if (part > 0)
        {
            if (pos >= text.size() || text[pos] != '.')
            {
                return std::nullopt;
            }
            ++pos;
        }

        const size_t start = pos;
        unsigned value = 0;
        while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9' && pos - start < 3)
        {
            value = value * 10 + static_cast<unsigned>(text[pos] - '0');
            ++pos;
        }

        const size_t digits = pos - start;
        if (digits == 0 || value > 255 || (digits > 1 && text[start] == '0'))
        {
            return std::nullopt;
        }
        bytes[part] = static_cast<uint8_t>(value);
    }

    if (pos != text.size())
    {
        return std::nullopt;
    }
    return Ipv4Address(bytes[0], bytes[1], bytes[2], bytes[3]);
}

std::string Ipv4Address::toString() const
{
    std::string text;
    for (size_t part = 0; part < bytes_.size(); ++part)
    {
        if (part > 0)
        {
            text += '.';
        }
        text += std::to_string(bytes_[part]);
    }
    return text;
}

}  // namespace espmgr
