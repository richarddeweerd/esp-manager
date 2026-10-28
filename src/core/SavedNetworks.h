#pragma once

#include "core/Logger.h"
#include "core/SavedNetwork.h"
#include "core/SavedNetworkStore.h"

#include <cstddef>
#include <string>
#include <vector>

namespace espmgr
{

// The Saved Networks the device connects to, reached through esp.networks(). Changes are
// stored straight away and used from the next connection attempt on.
class SavedNetworks
{
public:
    // v0.1 keeps a single Saved Network; saving another replaces it.
    static constexpr size_t kMaxNetworks = 1;

    SavedNetworks(SavedNetworkStore& store, Logger& log) : store_(store), log_(log)
    {
    }

    SavedNetworks(const SavedNetworks&) = delete;
    SavedNetworks& operator=(const SavedNetworks&) = delete;

    // Saves a network that uses DHCP. An empty password means an open network. Returns false if
    // the SSID or password isn't valid or the network couldn't be stored.
    bool save(const std::string& ssid, const std::string& password);
    bool save(const SavedNetwork& network);

    // Removes every Saved Network.
    bool clear();

    size_t count() const
    {
        return networks_.size();
    }

    // Copies, in the order they are tried. They include the passwords; never log them.
    std::vector<SavedNetwork> list() const
    {
        return networks_;
    }

    // Reads the stored list. Called by begin().
    void load();

private:
    SavedNetworkStore& store_;
    Logger& log_;
    std::vector<SavedNetwork> networks_;
};

}  // namespace espmgr
