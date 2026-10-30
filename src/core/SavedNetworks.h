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
//
// The list is in the Owner's priority order: the first network in range is used. Each network's
// `priority` is its place in the list (0 for the first), kept up to date by every change.
class SavedNetworks
{
public:
    // How many networks fit unless the Developer sets another limit.
    static constexpr size_t kDefaultMaxNetworks = 5;
    // The highest limit setMaxNetworks() accepts.
    static constexpr size_t kMaxNetworksLimit = 20;

    SavedNetworks(SavedNetworkStore& store, Logger& log) : store_(store), log_(log)
    {
    }

    SavedNetworks(const SavedNetworks&) = delete;
    SavedNetworks& operator=(const SavedNetworks&) = delete;

    // How many networks the Owner can save, 1 to 20; 5 by default. Returns false, changing
    // nothing, otherwise. A stored list that is already longer is kept as it is, but no network
    // can be added until it's shorter than the limit.
    bool setMaxNetworks(size_t max);
    size_t maxNetworks() const
    {
        return max_;
    }

    // Saves a network that uses DHCP. An empty password means an open network.
    bool save(const std::string& ssid, const std::string& password);
    // A new SSID goes to the top of the list; a network already saved under the same SSID is
    // updated in its place. `priority` is ignored. Returns false if the network isn't valid,
    // the list is full, or it couldn't be stored.
    bool save(const SavedNetwork& network);

    // Forgets the network with this SSID. Returns false if there is none or storing failed.
    bool remove(const std::string& ssid);

    // Moves the network with this SSID to `position` (0 is the top), shifting the others.
    // Returns false if there is no such network, `position` is past the end, or storing failed.
    bool move(const std::string& ssid, size_t position);

    // Removes every Saved Network.
    bool clear();

    size_t count() const
    {
        return networks_.size();
    }

    // True when a new SSID can't be added.
    bool full() const
    {
        return networks_.size() >= max_;
    }

    bool contains(const std::string& ssid) const;

    // Copies, in the order they are tried. They include the passwords; never log them.
    std::vector<SavedNetwork> list() const
    {
        return networks_;
    }

    // Reads the stored list. Called by begin().
    void load();

private:
    // Numbers the networks by their place, stores them and, if that worked, keeps them.
    bool store(std::vector<SavedNetwork> updated);
    // Where the network with this SSID is, or count() if there is none.
    size_t find(const std::string& ssid) const;

    SavedNetworkStore& store_;
    Logger& log_;
    std::vector<SavedNetwork> networks_;
    size_t max_ = kDefaultMaxNetworks;
};

}  // namespace espmgr
