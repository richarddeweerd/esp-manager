#include "platform/esp32/NvsStorage.h"

#include <Preferences.h>
#include <nvs.h>

namespace espmgr
{

namespace
{

// Preferences logs an error when it opens a namespace that doesn't exist yet read-only, which
// is normal on a new device. Asking NVS directly first stays quiet.
bool namespaceExists(const std::string& ns)
{
    nvs_iterator_t it = nullptr;
    const esp_err_t err = nvs_entry_find(NVS_DEFAULT_PART_NAME, ns.c_str(), NVS_TYPE_ANY, &it);
    nvs_release_iterator(it);
    return err == ESP_OK;
}

// Opens a namespace for one call. A namespace that doesn't exist yet isn't opened for reading,
// which the getters treat as "no value".
class Session
{
public:
    Session(const std::string& ns, bool readOnly)
        : open_(Storage::isValidName(ns) && (!readOnly || namespaceExists(ns)) &&
                prefs_.begin(ns.c_str(), readOnly))
    {
    }

    ~Session()
    {
        if (open_)
        {
            prefs_.end();
        }
    }

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    bool isOpen() const
    {
        return open_;
    }

    // True if the namespace is open and `key` is a valid name.
    bool usable(const std::string& key) const
    {
        return open_ && Storage::isValidName(key);
    }

    bool has(const std::string& key, PreferenceType type)
    {
        return usable(key) && prefs_.isKey(key.c_str()) && prefs_.getType(key.c_str()) == type;
    }

    Preferences& prefs()
    {
        return prefs_;
    }

private:
    Preferences prefs_;
    bool open_;
};

}  // namespace

std::optional<std::string> NvsStorage::getString(const std::string& ns, const std::string& key)
{
    Session session(ns, true);
    if (!session.has(key, PT_STR))
    {
        return std::nullopt;
    }
    const String value = session.prefs().getString(key.c_str());
    return std::string(value.c_str(), value.length());
}

std::optional<uint32_t> NvsStorage::getU32(const std::string& ns, const std::string& key)
{
    Session session(ns, true);
    if (!session.has(key, PT_U32))
    {
        return std::nullopt;
    }
    return session.prefs().getUInt(key.c_str());
}

std::optional<bool> NvsStorage::getBool(const std::string& ns, const std::string& key)
{
    // Preferences stores a bool as an unsigned byte.
    Session session(ns, true);
    if (!session.has(key, PT_U8))
    {
        return std::nullopt;
    }
    return session.prefs().getBool(key.c_str());
}

std::optional<std::vector<uint8_t>> NvsStorage::getBytes(const std::string& ns,
                                                         const std::string& key)
{
    Session session(ns, true);
    if (!session.has(key, PT_BLOB))
    {
        return std::nullopt;
    }
    std::vector<uint8_t> value(session.prefs().getBytesLength(key.c_str()));
    if (!value.empty() && session.prefs().getBytes(key.c_str(), value.data(), value.size()) == 0)
    {
        return std::nullopt;
    }
    return value;
}

bool NvsStorage::putString(const std::string& ns, const std::string& key, const std::string& value)
{
    Session session(ns, false);
    // An empty string writes zero bytes, so success can't be judged by the length written.
    return session.usable(key) &&
           (session.prefs().putString(key.c_str(), value.c_str()) > 0 || value.empty());
}

bool NvsStorage::putU32(const std::string& ns, const std::string& key, uint32_t value)
{
    Session session(ns, false);
    return session.usable(key) && session.prefs().putUInt(key.c_str(), value) > 0;
}

bool NvsStorage::putBool(const std::string& ns, const std::string& key, bool value)
{
    Session session(ns, false);
    return session.usable(key) && session.prefs().putBool(key.c_str(), value) > 0;
}

bool NvsStorage::putBytes(const std::string& ns, const std::string& key,
                          const std::vector<uint8_t>& value)
{
    Session session(ns, false);
    return session.usable(key) &&
           (session.prefs().putBytes(key.c_str(), value.data(), value.size()) > 0 || value.empty());
}

bool NvsStorage::remove(const std::string& ns, const std::string& key)
{
    Session session(ns, false);
    if (!session.usable(key))
    {
        return false;
    }
    return !session.prefs().isKey(key.c_str()) || session.prefs().remove(key.c_str());
}

bool NvsStorage::clear(const std::string& ns)
{
    Session session(ns, false);
    return session.isOpen() && session.prefs().clear();
}

}  // namespace espmgr
