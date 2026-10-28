#include "core/SavedNetworks.h"

namespace espmgr
{

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
    // With room for one network, a new one replaces the old.
    std::vector<SavedNetwork> updated{network};
    if (!store_.save(updated))
    {
        return false;
    }
    networks_ = std::move(updated);
    log_.info("networks", "saved %s", network.ssid.c_str());
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

void SavedNetworks::load()
{
    SavedNetworkStore::LoadResult result = store_.load();
    networks_ = std::move(result.networks);
    if (networks_.size() > kMaxNetworks)
    {
        networks_.resize(kMaxNetworks);
    }
    log_.info("networks", "%u Saved Network(s)", static_cast<unsigned>(networks_.size()));
}

}  // namespace espmgr
