#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace espmgr
{

// An IPv4 address as four bytes, so core code doesn't depend on Arduino's IPAddress.
class Ipv4Address
{
public:
    // 0.0.0.0, which means "not set".
    constexpr Ipv4Address() = default;

    constexpr Ipv4Address(uint8_t a, uint8_t b, uint8_t c, uint8_t d) : bytes_{a, b, c, d}
    {
    }

    // Parses dotted-decimal notation ("192.168.1.10"). Returns nothing for anything else,
    // including leading zeros, missing parts and surrounding whitespace.
    static std::optional<Ipv4Address> parse(std::string_view text);

    std::string toString() const;

    constexpr uint8_t operator[](size_t index) const
    {
        return bytes_[index];
    }

    constexpr bool isUnset() const
    {
        return bytes_[0] == 0 && bytes_[1] == 0 && bytes_[2] == 0 && bytes_[3] == 0;
    }

    constexpr bool operator==(const Ipv4Address& other) const
    {
        return bytes_[0] == other.bytes_[0] && bytes_[1] == other.bytes_[1] &&
               bytes_[2] == other.bytes_[2] && bytes_[3] == other.bytes_[3];
    }

    constexpr bool operator!=(const Ipv4Address& other) const
    {
        return !(*this == other);
    }

private:
    std::array<uint8_t, 4> bytes_{};
};

}  // namespace espmgr
