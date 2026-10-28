#include "core/SavedNetworkStore.h"

namespace espmgr
{

// Record format, version 1:
//   u8 version, u8 count, then per network:
//   u8 ssid length, ssid bytes, u8 password length, password bytes, u8 priority,
//   u8 flags (bit 0: static IP), then ip, gateway, subnet, dns1, dns2 as 4 bytes each.

namespace
{

constexpr uint8_t kStaticIpFlag = 0x01;

void putAddress(std::vector<uint8_t>& out, const Ipv4Address& address)
{
    for (size_t i = 0; i < 4; ++i)
    {
        out.push_back(address[i]);
    }
}

class Reader
{
public:
    explicit Reader(const std::vector<uint8_t>& data) : data_(data)
    {
    }

    bool byte(uint8_t& value)
    {
        if (pos_ >= data_.size())
        {
            return false;
        }
        value = data_[pos_++];
        return true;
    }

    bool text(std::string& value)
    {
        uint8_t length = 0;
        if (!byte(length) || data_.size() - pos_ < length)
        {
            return false;
        }
        value.assign(data_.begin() + pos_, data_.begin() + pos_ + length);
        pos_ += length;
        return true;
    }

    bool address(Ipv4Address& value)
    {
        if (data_.size() - pos_ < 4)
        {
            return false;
        }
        value = Ipv4Address(data_[pos_], data_[pos_ + 1], data_[pos_ + 2], data_[pos_ + 3]);
        pos_ += 4;
        return true;
    }

    bool atEnd() const
    {
        return pos_ == data_.size();
    }

private:
    const std::vector<uint8_t>& data_;
    size_t pos_ = 0;
};

}  // namespace

std::vector<uint8_t> SavedNetworkStore::encode(const std::vector<SavedNetwork>& networks)
{
    std::vector<uint8_t> out{kFormatVersion, static_cast<uint8_t>(networks.size())};
    for (const SavedNetwork& network : networks)
    {
        out.push_back(static_cast<uint8_t>(network.ssid.size()));
        out.insert(out.end(), network.ssid.begin(), network.ssid.end());
        out.push_back(static_cast<uint8_t>(network.password.size()));
        out.insert(out.end(), network.password.begin(), network.password.end());
        out.push_back(network.priority);
        out.push_back(network.dhcp ? 0 : kStaticIpFlag);
        putAddress(out, network.staticIp.ip);
        putAddress(out, network.staticIp.gateway);
        putAddress(out, network.staticIp.subnet);
        putAddress(out, network.staticIp.dns1);
        putAddress(out, network.staticIp.dns2);
    }
    return out;
}

bool SavedNetworkStore::decode(const std::vector<uint8_t>& record,
                               std::vector<SavedNetwork>& networks)
{
    Reader in(record);
    uint8_t version = 0;
    uint8_t count = 0;
    if (!in.byte(version) || version != kFormatVersion || !in.byte(count))
    {
        return false;
    }
    std::vector<SavedNetwork> decoded(count);
    for (SavedNetwork& network : decoded)
    {
        uint8_t flags = 0;
        StaticIpConfig& ip = network.staticIp;
        if (!in.text(network.ssid) || !in.text(network.password) || !in.byte(network.priority) ||
            !in.byte(flags) || !in.address(ip.ip) || !in.address(ip.gateway) ||
            !in.address(ip.subnet) || !in.address(ip.dns1) || !in.address(ip.dns2))
        {
            return false;
        }
        network.dhcp = (flags & kStaticIpFlag) == 0;
        // Only valid networks are ever saved, so anything else means the record is damaged.
        if ((flags & ~kStaticIpFlag) != 0 || network.validate() != SavedNetwork::Problem::None)
        {
            return false;
        }
    }
    if (!in.atEnd())
    {
        return false;
    }
    networks = std::move(decoded);
    return true;
}

SavedNetworkStore::LoadResult SavedNetworkStore::load() const
{
    const std::optional<std::vector<uint8_t>> record = storage_.getBytes(kNamespace, kKey);
    if (!record)
    {
        return {LoadStatus::Missing, {}};
    }
    std::vector<SavedNetwork> networks;
    if (!decode(*record, networks))
    {
        log_.warn("storage", "Saved Networks record unreadable (%u bytes, format %u); ignoring it",
                  static_cast<unsigned>(record->size()),
                  record->empty() ? 0u : static_cast<unsigned>((*record)[0]));
        return {LoadStatus::Unreadable, {}};
    }
    return {LoadStatus::Ok, std::move(networks)};
}

bool SavedNetworkStore::save(const std::vector<SavedNetwork>& networks)
{
    if (networks.size() > UINT8_MAX)
    {
        log_.warn("storage", "not saving %u Saved Networks; at most %u fit",
                  static_cast<unsigned>(networks.size()), static_cast<unsigned>(UINT8_MAX));
        return false;
    }
    for (size_t i = 0; i < networks.size(); ++i)
    {
        const SavedNetwork::Problem problem = networks[i].validate();
        if (problem != SavedNetwork::Problem::None)
        {
            log_.warn("storage", "not saving Saved Network %u: %s", static_cast<unsigned>(i),
                      toString(problem));
            return false;
        }
    }
    if (!storage_.putBytes(kNamespace, kKey, encode(networks)))
    {
        log_.error("storage", "could not write the Saved Networks");
        return false;
    }
    return true;
}

bool SavedNetworkStore::clear()
{
    return storage_.remove(kNamespace, kKey);
}

}  // namespace espmgr
