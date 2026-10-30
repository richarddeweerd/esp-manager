#include "core/SavedNetworks.h"

#include <algorithm>

namespace espmgr
{

bool SavedNetworks::setMaxNetworks(size_t max)
{
    if (max < 1 || max > kMaxNetworksLimit)
    {
        log_.warn("networks", "ignoring a limit of %u networks", static_cast<unsigned>(max));
        return false;
    }
    max_ = max;
    return true;
}

bool SavedNetworks::save(const std::string& ssid, const std::string& password)
{
    SavedNetwork network;
    network.ssid = ssid;
    network.password = password;
    return save(network);
}

bool SavedNetworks::save(const SavedNetwork& network)
{
    const SavedNetwork::Problem problem = network.validate();
    if (problem != SavedNetwork::Problem::None)
    {
        log_.warn("networks", "not saving %s: %s", network.ssid.c_str(), toString(problem));
        return false;
    }
    std::vector<SavedNetwork> updated = networks_;
    const size_t at = find(network.ssid);
    if (at < updated.size())
    {
        updated[at] = network;
    }
    else if (full())
    {
        log_.warn("networks", "not saving %s: already %u Saved Networks", network.ssid.c_str(),
                  static_cast<unsigned>(networks_.size()));
        return false;
    }
    else
    {
        updated.insert(updated.begin(), network);
    }
    if (!store(std::move(updated)))
    {
        return false;
    }
    log_.info("networks", "saved %s", network.ssid.c_str());
    return true;
}

bool SavedNetworks::remove(const std::string& ssid)
{
    const size_t at = find(ssid);
    if (at == networks_.size())
    {
        return false;
    }
    std::vector<SavedNetwork> updated = networks_;
    updated.erase(updated.begin() + static_cast<std::ptrdiff_t>(at));
    if (!store(std::move(updated)))
    {
        return false;
    }
    log_.info("networks", "removed %s", ssid.c_str());
    return true;
}

bool SavedNetworks::move(const std::string& ssid, size_t position)
{
    const size_t at = find(ssid);
    if (at == networks_.size() || position >= networks_.size())
    {
        return false;
    }
    if (at == position)
    {
        return true;
    }
    std::vector<SavedNetwork> updated = networks_;
    const SavedNetwork network = updated[at];
    updated.erase(updated.begin() + static_cast<std::ptrdiff_t>(at));
    updated.insert(updated.begin() + static_cast<std::ptrdiff_t>(position), network);
    if (!store(std::move(updated)))
    {
        return false;
    }
    log_.info("networks", "moved %s to place %u", ssid.c_str(),
              static_cast<unsigned>(position + 1));
    return true;
}

bool SavedNetworks::clear()
{
    if (!store_.clear())
    {
        return false;
    }
    networks_.clear();
    return true;
}

bool SavedNetworks::contains(const std::string& ssid) const
{
    return find(ssid) < networks_.size();
}

void SavedNetworks::load()
{
    SavedNetworkStore::LoadResult result = store_.load();
    networks_ = std::move(result.networks);
    // The stored priorities came from this class, but an older version may have left gaps.
    std::stable_sort(networks_.begin(), networks_.end(),
                     [](const SavedNetwork& a, const SavedNetwork& b)
                     { return a.priority < b.priority; });
    for (size_t i = 0; i < networks_.size(); ++i)
    {
        networks_[i].priority = static_cast<uint8_t>(i);
    }
    log_.info("networks", "%u Saved Network(s)", static_cast<unsigned>(networks_.size()));
}

bool SavedNetworks::store(std::vector<SavedNetwork> updated)
{
    for (size_t i = 0; i < updated.size(); ++i)
    {
        updated[i].priority = static_cast<uint8_t>(i);
    }
    if (!store_.save(updated))
    {
        return false;
    }
    networks_ = std::move(updated);
    return true;
}

size_t SavedNetworks::find(const std::string& ssid) const
{
    for (size_t i = 0; i < networks_.size(); ++i)
    {
        if (networks_[i].ssid == ssid)
        {
            return i;
        }
    }
    return networks_.size();
}

}  // namespace espmgr
