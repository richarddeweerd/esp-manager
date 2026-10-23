#pragma once

#include "core/Storage.h"

#include <map>
#include <variant>

namespace espmgr
{

// In-memory Storage. Rejects the same names NVS rejects, so a too-long key fails in the native
// tests and not only on a board. Values keep their type: getU32() on a string returns nothing.
class FakeStorage : public Storage
{
public:
    using Value = std::variant<std::string, uint32_t, bool, std::vector<uint8_t>>;

    std::optional<std::string> getString(const std::string& ns, const std::string& key) override
    {
        return get<std::string>(ns, key);
    }

    std::optional<uint32_t> getU32(const std::string& ns, const std::string& key) override
    {
        return get<uint32_t>(ns, key);
    }

    std::optional<bool> getBool(const std::string& ns, const std::string& key) override
    {
        return get<bool>(ns, key);
    }

    std::optional<std::vector<uint8_t>> getBytes(const std::string& ns,
                                                 const std::string& key) override
    {
        return get<std::vector<uint8_t>>(ns, key);
    }

    bool putString(const std::string& ns, const std::string& key, const std::string& value) override
    {
        return put(ns, key, value);
    }

    bool putU32(const std::string& ns, const std::string& key, uint32_t value) override
    {
        return put(ns, key, value);
    }

    bool putBool(const std::string& ns, const std::string& key, bool value) override
    {
        return put(ns, key, value);
    }

    bool putBytes(const std::string& ns, const std::string& key,
                  const std::vector<uint8_t>& value) override
    {
        return put(ns, key, value);
    }

    bool remove(const std::string& ns, const std::string& key) override
    {
        if (!isValidName(ns) || !isValidName(key))
        {
            return false;
        }
        auto found = data.find(ns);
        if (found != data.end())
        {
            found->second.erase(key);
        }
        return true;
    }

    bool clear(const std::string& ns) override
    {
        if (!isValidName(ns))
        {
            return false;
        }
        data.erase(ns);
        return true;
    }

    // When false, every put fails, as when the NVS partition is full.
    bool writable = true;

    // Namespace → key → value. Tests may read or prefill it directly.
    std::map<std::string, std::map<std::string, Value>> data;

private:
    template <typename T> std::optional<T> get(const std::string& ns, const std::string& key) const
    {
        auto found = data.find(ns);
        if (found == data.end())
        {
            return std::nullopt;
        }
        auto value = found->second.find(key);
        if (value == found->second.end() || !std::holds_alternative<T>(value->second))
        {
            return std::nullopt;
        }
        return std::get<T>(value->second);
    }

    bool put(const std::string& ns, const std::string& key, Value value)
    {
        if (!writable || !isValidName(ns) || !isValidName(key))
        {
            return false;
        }
        data[ns][key] = std::move(value);
        return true;
    }
};

}  // namespace espmgr
