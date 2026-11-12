#include "core/NetworkTime.h"

#include <iterator>

namespace espmgr
{

namespace
{

// 1 to `maxLength` bytes from `first` to '~'.
bool printable(const std::string& text, size_t maxLength, char first)
{
    if (text.empty() || text.size() > maxLength)
    {
        return false;
    }
    for (const char c : text)
    {
        if (c < first || c > '~')
        {
            return false;
        }
    }
    return true;
}

}  // namespace

NetworkTime::NetworkTime(TimeDriver& driver, Clock& clock, const WifiConnection& connection,
                         Settings& settings, Events& events, Logger& log)
    : driver_(driver), clock_(clock), connection_(connection), settings_(settings), events_(events),
      log_(log), servers_(std::begin(kDefaultServers), std::end(kDefaultServers))
{
}

NetworkTime& NetworkTime::enable()
{
    if (!enabled_)
    {
        enabled_ = true;
        // The next process() starts syncing if the station is connected.
        wasConnected_ = false;
    }
    return *this;
}

NetworkTime& NetworkTime::disable()
{
    if (enabled_)
    {
        enabled_ = false;
        watching_ = false;
        if (started_)
        {
            driver_.stop();
            started_ = false;
            log_.info("time", "stopped syncing");
        }
    }
    return *this;
}

bool NetworkTime::validName(const std::string& name, const char* what) const
{
    if (!printable(name, kMaxNameLength, '!'))
    {
        log_.error("time", "%s must be 1 to %u printable characters without spaces", what,
                   static_cast<unsigned>(kMaxNameLength));
        return false;
    }
    return true;
}

bool NetworkTime::setServer(const std::string& name)
{
    if (!validName(name, "a time server"))
    {
        return false;
    }
    servers_[0] = name;
    if (started_)
    {
        startDriver();
    }
    return true;
}

bool NetworkTime::addFallbackServer(const std::string& name)
{
    if (!validName(name, "a time server"))
    {
        return false;
    }
    if (defaultFallbacks_)
    {
        servers_.resize(1);
        defaultFallbacks_ = false;
    }
    if (servers_.size() >= TimeDriver::kMaxServers)
    {
        log_.error("time", "at most %u time servers",
                   static_cast<unsigned>(TimeDriver::kMaxServers));
        return false;
    }
    servers_.push_back(name);
    if (started_)
    {
        startDriver();
    }
    return true;
}

NetworkTime& NetworkTime::useDhcpServer(bool use)
{
    if (useDhcp_ != use)
    {
        useDhcp_ = use;
        if (started_)
        {
            startDriver();
        }
    }
    return *this;
}

bool NetworkTime::setTimezone(const std::string& posix)
{
    if (!printable(posix, kMaxNameLength, ' '))
    {
        log_.error("time", "a time zone must be 1 to %u printable characters",
                   static_cast<unsigned>(kMaxNameLength));
        return false;
    }
    timezone_ = posix;
    // The Owner's zone, if there is one, stays in force.
    if (!ownerZone())
    {
        applyTimezone();
    }
    return true;
}

std::string NetworkTime::timezone() const
{
    return ownerZone() ? posixSetting_->value() : timezone_;
}

void NetworkTime::applyTimezone()
{
    const std::string posix = timezone();
    driver_.setTimezone(posix);
    log_.debug("time", "time zone %s", posix.c_str());
}

bool NetworkTime::ownerZone() const
{
    return posixSetting_ != nullptr && !posixSetting_->value().empty();
}

std::string NetworkTime::zone() const
{
    return ownerZone() ? zoneSetting_->value() : std::string();
}

bool NetworkTime::dst() const
{
    return ownerZone() ? dstSetting_->value() : true;
}

NetworkTime& NetworkTime::ownerCanEdit()
{
    if (zoneSetting_ != nullptr)
    {
        return *this;
    }
    SettingsSection& section = settings_.section(kSectionTitle);
    zoneSetting_ = &section.addText(kZoneKey, "Time zone").maxLength(kMaxNameLength);
    dstSetting_ = &section.addCheckbox(kDstKey, "Daylight saving time", true);
    posixSetting_ = &section.addText(kPosixKey, "POSIX time zone")
                         .maxLength(kMaxNameLength)
                         .validate(
                             [](const std::string& value)
                             {
                                 return value.empty() || printable(value, kMaxNameLength, ' ')
                                            ? std::string()
                                            : std::string("Not a time zone.");
                             });
    if (editableInManagement_)
    {
        editableInManagement_ = false;
        editableInManagement();
    }
    return *this;
}

NetworkTime& NetworkTime::ownerCanEditServers()
{
    ownerCanEdit();
    if (serverSetting_ != nullptr)
    {
        return *this;
    }
    const auto serverName = [](const std::string& value)
    {
        return value.empty() || printable(value, kMaxNameLength, '!')
                   ? std::string()
                   : std::string("Use a host name or IP address without spaces.");
    };
    SettingsSection& section = settings_.section(kSectionTitle);
    serverSetting_ =
        &section.addText(kServerKey, "NTP server").maxLength(kMaxNameLength).validate(serverName);
    fallbackSetting_ = &section.addText(kFallbackKey, "Fallback NTP server")
                            .maxLength(kMaxNameLength)
                            .validate(serverName);
    if (editableInManagement_)
    {
        serverSetting_->editableInManagement();
        fallbackSetting_->editableInManagement();
    }
    return *this;
}

NetworkTime& NetworkTime::editableInManagement()
{
    editableInManagement_ = true;
    for (TextSetting* setting : {zoneSetting_, posixSetting_, serverSetting_, fallbackSetting_})
    {
        if (setting != nullptr)
        {
            setting->editableInManagement();
        }
    }
    if (dstSetting_ != nullptr)
    {
        dstSetting_->editableInManagement();
    }
    return *this;
}

std::vector<std::string> NetworkTime::activeServers() const
{
    std::vector<std::string> servers = servers_;
    if (serverSetting_ != nullptr && !serverSetting_->value().empty())
    {
        servers[0] = serverSetting_->value();
    }
    if (fallbackSetting_ != nullptr && !fallbackSetting_->value().empty())
    {
        if (servers.size() >= 2)
        {
            servers[1] = fallbackSetting_->value();
        }
        else
        {
            servers.push_back(fallbackSetting_->value());
        }
    }
    return servers;
}

void NetworkTime::settingChanged(const Setting& setting)
{
    if (&setting == posixSetting_)
    {
        applyTimezone();
    }
    else if ((&setting == serverSetting_ || &setting == fallbackSetting_) && started_)
    {
        startDriver();
    }
}

bool NetworkTime::setSyncInterval(uint32_t ms)
{
    if (ms < kMinSyncIntervalMs)
    {
        log_.error("time", "a sync interval must be at least %u ms",
                   static_cast<unsigned>(kMinSyncIntervalMs));
        return false;
    }
    intervalMs_ = ms;
    if (started_)
    {
        startDriver();
    }
    return true;
}

bool NetworkTime::sync()
{
    if (!enabled_)
    {
        log_.debug("time", "not syncing: Network Time is off");
        return false;
    }
    if (!started_ || connection_.state() != ConnectionState::Connected)
    {
        log_.debug("time", "syncing when the station connects");
        return false;
    }
    log_.debug("time", "syncing now");
    driver_.restart();
    watching_ = true;
    answerDueMs_ = clock_.nowMs() + kAnswerTimeoutMs;
    return true;
}

int64_t NetworkTime::now() const
{
    return driver_.nowEpoch();
}

std::string NetworkTime::localTime() const
{
    return synchronised_ ? driver_.localTime(now()) : std::string();
}

std::string NetworkTime::server() const
{
    return synchronised_ ? driver_.answeringServer() : std::string();
}

void NetworkTime::begin()
{
    begun_ = true;
    // The Owner's zone has just been loaded with the Settings.
    if (ownerZone())
    {
        applyTimezone();
    }
}

void NetworkTime::startDriver()
{
    const std::vector<std::string> servers = activeServers();
    driver_.start(servers, intervalMs_, useDhcp_);
    started_ = true;
    watching_ = true;
    answerDueMs_ = clock_.nowMs() + kAnswerTimeoutMs;
    std::string names;
    for (const std::string& name : servers)
    {
        names += names.empty() ? name : ", " + name;
    }
    log_.info("time", "syncing from %s%s every %u s", names.c_str(),
              useDhcp_ ? " (or DHCP's server)" : "", static_cast<unsigned>(intervalMs_ / 1000));
}

void NetworkTime::process()
{
    // Drained even while off, so an old answer isn't reported later.
    const bool answered = driver_.takeSynced();
    if (!enabled_ || !begun_)
    {
        return;
    }
    const bool connected = connection_.state() == ConnectionState::Connected;
    if (connected && !wasConnected_)
    {
        // Every new connection starts a sync, the first one starts the client.
        if (started_)
        {
            driver_.restart();
            watching_ = true;
            answerDueMs_ = clock_.nowMs() + kAnswerTimeoutMs;
        }
        else
        {
            startDriver();
        }
    }
    wasConnected_ = connected;
    if (!started_)
    {
        return;
    }

    const uint64_t nowMs = clock_.nowMs();
    if (answered)
    {
        const bool first = !synchronised_;
        synchronised_ = true;
        lastSync_ = driver_.nowEpoch();
        const std::string local = driver_.localTime(lastSync_);
        const std::string from = driver_.answeringServer();
        if (first)
        {
            log_.info("time", "synchronised with %s: %s", from.c_str(), local.c_str());
        }
        else
        {
            log_.debug("time", "synchronised with %s: %s", from.c_str(), local.c_str());
        }
        events_.raiseTimeSynced(TimeSyncInfo{lastSync_, first});
        // The next sync is due an interval from now.
        watching_ = true;
        answerDueMs_ = nowMs + intervalMs_ + kAnswerTimeoutMs;
        return;
    }
    // Without a connection no answer can come; the next connection starts a new attempt.
    if (watching_ && connected && nowMs >= answerDueMs_)
    {
        log_.warn("time", "no answer from the time servers within %u s",
                  static_cast<unsigned>(kAnswerTimeoutMs / 1000));
        events_.raiseTimeSyncFailed();
        // The client keeps trying; report again only if an interval goes by without an answer.
        answerDueMs_ = nowMs + intervalMs_;
    }
}

}  // namespace espmgr
