#pragma once

#include "core/Logger.h"
#include "core/SavedNetwork.h"
#include "core/Storage.h"

#include <vector>

namespace espmgr
{

// Keeps the Saved Networks in Storage (US05.1.1), as one versioned record holding the whole
// list, so a save is all or nothing. It stores whatever list it's given; how many Saved
// Networks are allowed is decided by its caller.
class SavedNetworkStore
{
public:
    static constexpr const char* kNamespace = "espmgr-wifi";
    static constexpr const char* kKey = "networks";
    // Bump when the record layout changes, and teach decode() to read the old one.
    static constexpr uint8_t kFormatVersion = 1;

    enum class LoadStatus : uint8_t
    {
        Ok,
        Missing,     // nothing saved yet, or cleared by a Wi-Fi reset
        Unreadable,  // damaged, or written by newer firmware; left in place
    };

    struct LoadResult
    {
        LoadStatus status;
        std::vector<SavedNetwork> networks;
    };

    SavedNetworkStore(Storage& storage, Logger& log) : storage_(storage), log_(log)
    {
    }

    // An unreadable record counts as no Saved Networks, but isn't deleted: firmware that
    // understands it can still read it, and the next save() replaces it.
    LoadResult load() const;

    // Replaces the stored list. Returns false, and keeps the old list, if a network fails
    // validate() or Storage can't write.
    bool save(const std::vector<SavedNetwork>& networks);

    // Removes the record, for a Wi-Fi reset. Other data in Storage is left alone.
    bool clear();

    // The record format, exposed for tests.
    static std::vector<uint8_t> encode(const std::vector<SavedNetwork>& networks);
    static bool decode(const std::vector<uint8_t>& record, std::vector<SavedNetwork>& networks);

private:
    Storage& storage_;
    Logger& log_;
};

}  // namespace espmgr
