#pragma once

#include "core/Storage.h"

namespace espmgr
{

// Storage on NVS through Arduino-ESP32's Preferences. Opens the namespace on every call and
// closes it again, so no handles stay open.
class NvsStorage : public Storage
{
public:
    std::optional<std::string> getString(const std::string& ns, const std::string& key) override;
    std::optional<uint32_t> getU32(const std::string& ns, const std::string& key) override;
    std::optional<bool> getBool(const std::string& ns, const std::string& key) override;
    std::optional<std::vector<uint8_t>> getBytes(const std::string& ns,
                                                 const std::string& key) override;

    bool putString(const std::string& ns, const std::string& key,
                   const std::string& value) override;
    bool putU32(const std::string& ns, const std::string& key, uint32_t value) override;
    bool putBool(const std::string& ns, const std::string& key, bool value) override;
    bool putBytes(const std::string& ns, const std::string& key,
                  const std::vector<uint8_t>& value) override;

    bool remove(const std::string& ns, const std::string& key) override;
    bool clear(const std::string& ns) override;
    bool eraseAll() override;
};

}  // namespace espmgr
