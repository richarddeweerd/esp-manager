#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace espmgr
{

// Persistent key/value storage, grouped in namespaces. Wi-Fi data and Settings live in separate
// namespaces, so each reset can clear its own.
class Storage
{
public:
    // NVS allows at most 15 characters for namespace and key names.
    static constexpr size_t kMaxNameLength = 15;

    static bool isValidName(const std::string& name)
    {
        return !name.empty() && name.size() <= kMaxNameLength;
    }

    virtual ~Storage() = default;

    // Getters return nothing if the key doesn't exist or holds another type.
    virtual std::optional<std::string> getString(const std::string& ns, const std::string& key) = 0;
    virtual std::optional<uint32_t> getU32(const std::string& ns, const std::string& key) = 0;
    virtual std::optional<bool> getBool(const std::string& ns, const std::string& key) = 0;
    virtual std::optional<std::vector<uint8_t>> getBytes(const std::string& ns,
                                                         const std::string& key) = 0;

    // Setters return false if the value couldn't be stored, for example because a name is
    // invalid.
    virtual bool putString(const std::string& ns, const std::string& key,
                           const std::string& value) = 0;
    virtual bool putU32(const std::string& ns, const std::string& key, uint32_t value) = 0;
    virtual bool putBool(const std::string& ns, const std::string& key, bool value) = 0;
    virtual bool putBytes(const std::string& ns, const std::string& key,
                          const std::vector<uint8_t>& value) = 0;

    // Both return true if the key or namespace is gone afterwards, also if it never existed.
    virtual bool remove(const std::string& ns, const std::string& key) = 0;
    virtual bool clear(const std::string& ns) = 0;
};

}  // namespace espmgr
