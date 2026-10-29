#include "core/ProvisioningPortal.h"

#include "core/JsonWriter.h"
#include "web/Pages.h"

#include <algorithm>

namespace espmgr
{

namespace
{

constexpr size_t kMaxSsidLength = 32;

// The codes the page turns into its own text.
const char* code(SavedNetwork::Problem problem)
{
    switch (problem)
    {
    case SavedNetwork::Problem::SsidEmpty:
        return "ssid-empty";
    case SavedNetwork::Problem::SsidTooLong:
        return "ssid-too-long";
    case SavedNetwork::Problem::PasswordTooShort:
        return "password-too-short";
    case SavedNetwork::Problem::PasswordTooLong:
        return "password-too-long";
    default:
        return "password-invalid";
    }
}

const char* code(FailureReason reason)
{
    switch (reason)
    {
    case FailureReason::NetworkNotFound:
        return "network-not-found";
    case FailureReason::WrongPassword:
        return "wrong-password";
    case FailureReason::Timeout:
        return "timeout";
    case FailureReason::ConnectionLost:
        return "connection-lost";
    default:
        return "other";
    }
}

}  // namespace

bool ProvisioningPortal::setSsid(const std::string& ssid)
{
    if (ssid.size() > kMaxSsidLength)
    {
        log_.warn("portal", "ignoring an access point SSID of %u bytes",
                  static_cast<unsigned>(ssid.size()));
        return false;
    }
    ssid_ = ssid;
    return true;
}

std::string ProvisioningPortal::ssid() const
{
    return ssid_.empty() ? deviceName_() : ssid_;
}

bool ProvisioningPortal::setPassword(const std::string& password)
{
    if (!password.empty() && !isValidPassword(password))
    {
        log_.warn("portal", "ignoring an access point password that isn't 8 to 63 characters");
        return false;
    }
    password_ = password;
    return true;
}

void ProvisioningPortal::setTimeout(uint32_t ms)
{
    timeoutMs_ = ms;
}

void ProvisioningPortal::setRestartOnTimeout(bool restart)
{
    restartOnTimeout_ = restart;
}

void ProvisioningPortal::setOpenAfterFailingFor(uint32_t ms)
{
    openAfterFailingMs_ = ms;
}

void ProvisioningPortal::setCaptivePortal(bool enabled)
{
    captivePortal_ = enabled;
}

void ProvisioningPortal::setMinSignal(int8_t dbm)
{
    minSignalDbm_ = dbm;
}

void ProvisioningPortal::begin()
{
    begun_ = true;
    lastConnectedMs_ = clock_.nowMs();
}

void ProvisioningPortal::start()
{
    // The Developer's own request; it no longer closes by itself once connected.
    reason_ = Opened::ByRequest;
    if (state_ == PortalState::Off && !starting_)
    {
        startRequested_ = true;
    }
}

void ProvisioningPortal::stop()
{
    startRequested_ = false;
    starting_ = false;
    if (state_ == PortalState::Active)
    {
        close();
    }
}

void ProvisioningPortal::process()
{
    if (!begun_)
    {
        return;
    }
    if (connection_.state() == ConnectionState::Connected)
    {
        lastConnectedMs_ = clock_.nowMs();
    }
    if (startRequested_)
    {
        startRequested_ = false;
        open(Opened::ByRequest);
    }

    if (starting_)
    {
        if (!scanner_.busy())
        {
            bringUp(chooseChannel(networks_.list(), scanner_.results()));
        }
        return;
    }

    if (state_ == PortalState::Active)
    {
        followTrial();
        if (closing_)
        {
            if (clock_.nowMs() >= closeAtMs_)
            {
                close();
            }
            return;
        }
        if (timeoutMs_ > 0 && clock_.nowMs() - lastActivityMs_ >= timeoutMs_)
        {
            const bool reopen = networks_.count() == 0 && restartOnTimeout_;
            log_.info("portal", "no requests for %u s; closing%s",
                      static_cast<unsigned>(timeoutMs_ / 1000), reopen ? " and reopening" : "");
            close();
            if (reopen)
            {
                open(Opened::NoNetwork);
            }
            return;
        }
        if (reason_ == Opened::NotConnecting && result_ != Result::Testing &&
            connection_.state() == ConnectionState::Connected)
        {
            log_.info("portal", "connected again; closing");
            close();
            return;
        }
    }

    applyRules();
}

void ProvisioningPortal::applyRules()
{
    if (state_ != PortalState::Off || starting_)
    {
        if (networks_.count() == 0)
        {
            openedForNoNetwork_ = true;
        }
        return;
    }
    if (networks_.count() == 0)
    {
        if (!openedForNoNetwork_)
        {
            openedForNoNetwork_ = true;
            log_.info("portal", "no Saved Network; opening");
            open(Opened::NoNetwork);
        }
        return;
    }
    openedForNoNetwork_ = false;
    if (openAfterFailingMs_ > 0 && connection_.state() != ConnectionState::Connected &&
        clock_.nowMs() - lastConnectedMs_ >= openAfterFailingMs_)
    {
        log_.info("portal", "not connected for %u s; opening",
                  static_cast<unsigned>(openAfterFailingMs_ / 1000));
        open(Opened::NotConnecting);
    }
}

void ProvisioningPortal::open(Opened reason)
{
    reason_ = reason;
    starting_ = true;
    if (connection_.state() == ConnectionState::Connected)
    {
        // The access point has to use the station's channel anyway.
        bringUp(wifi_.channel());
        return;
    }
    if (!scanner_.start())
    {
        bringUp(chooseChannel(networks_.list(), scanner_.results()));
    }
}

void ProvisioningPortal::bringUp(uint8_t channel)
{
    starting_ = false;
    activeSsid_ = ssid();
    if (!wifi_.startAccessPoint(activeSsid_, password_, channel))
    {
        log_.error("portal", "access point %s didn't start", activeSsid_.c_str());
        return;
    }
    registerRoutes();
    routes_.setActive(WebRoutes::Site::Portal, true);
    dnsRunning_ = captivePortal_ && http_.startCaptiveDns(wifi_.accessPointIp());
    if (captivePortal_ && !dnsRunning_)
    {
        log_.warn("portal", "DNS server didn't start; phones won't open the page by themselves");
    }
    state_ = PortalState::Active;
    lastActivityMs_ = clock_.nowMs();
    log_.info("portal", "open as %s on channel %u, %s", activeSsid_.c_str(),
              static_cast<unsigned>(channel), password_.empty() ? "open" : "WPA2");
    events_.raisePortalStarted(activeSsid_);
}

void ProvisioningPortal::close()
{
    closing_ = false;
    result_ = Result::None;
    if (dnsRunning_)
    {
        http_.stopCaptiveDns();
        dnsRunning_ = false;
    }
    routes_.setActive(WebRoutes::Site::Portal, false);
    wifi_.stopAccessPoint();
    state_ = PortalState::Off;
    lastConnectedMs_ = clock_.nowMs();
    log_.info("portal", "closed");
    events_.raisePortalStopped();
}

void ProvisioningPortal::registerRoutes()
{
    if (routesRegistered_)
    {
        return;
    }
    routesRegistered_ = true;
    using Site = WebRoutes::Site;

    routes_.on(Site::Portal, HttpMethod::Get, "/",
               [this](const HttpRequest& request)
               {
                   seen();
                   // Asked for under another name, which only the captive DNS gives out: move
                   // the browser to the access point's address, so the page's own requests and
                   // a reload keep working.
                   std::string host = request.header("Host").value_or("");
                   host = host.substr(0, host.find(':'));
                   if (dnsRunning_ && !host.empty() && host != wifi_.accessPointIp().toString())
                   {
                       http_.sendRedirect(pageUrl());
                       return;
                   }
                   web::sendPortalPage(http_);
               });
    routes_.on(Site::Portal, HttpMethod::Get, "/api/scan",
               [this](const HttpRequest& request)
               {
                   seen();
                   sendScan(request);
               });
    routes_.on(Site::Portal, HttpMethod::Get, "/api/setup",
               [this](const HttpRequest&)
               {
                   seen();
                   sendSetup();
               });
    routes_.on(Site::Portal, HttpMethod::Get, "/api/status",
               [this](const HttpRequest&)
               {
                   seen();
                   JsonWriter json;
                   writeJson(json, status_());
                   http_.send(200, "application/json", json.str());
               });
    routes_.on(Site::Portal, HttpMethod::Post, "/api/connect",
               [this](const HttpRequest& request)
               {
                   seen();
                   receiveNetwork(request);
               });
    routes_.on(Site::Portal, HttpMethod::Get, "/api/result",
               [this](const HttpRequest&)
               {
                   seen();
                   sendResult();
               });
    // Every other address: the phones' connectivity checks and whatever the Owner typed.
    routes_.onNotFound(Site::Portal,
                       [this](const HttpRequest&)
                       {
                           seen();
                           if (dnsRunning_)
                           {
                               http_.sendRedirect(pageUrl());
                           }
                           else
                           {
                               http_.send(404, "text/plain", "Not found");
                           }
                       });
}

void ProvisioningPortal::seen()
{
    lastActivityMs_ = clock_.nowMs();
}

// What the page asks for besides the network: "new" when the Owner has to choose a Management
// Interface password, "keep" when one is stored already, "off" without a Management Interface.
void ProvisioningPortal::sendSetup()
{
    const char* management = "off";
    if (management_.enabled())
    {
        management = management_.hasPassword() ? "keep" : "new";
    }
    JsonWriter json;
    json.beginObject()
        .key("management")
        .string(management)
        .key("user")
        .string(management_.enabled() ? management_.user() : std::string())
        .endObject();
    http_.send(200, "application/json", json.str());
}

// The network list for the page. Answers at once with what the last scan found, and starts a
// new scan when that is old, or when the page asks with refresh=1; "scanning" tells the page to
// ask again.
void ProvisioningPortal::sendScan(const HttpRequest& request)
{
    const bool refresh = request.arg("refresh").value_or("") == "1";
    const bool old =
        !scanner_.hasFinished() || clock_.nowMs() - scanner_.finishedAtMs() >= kScanMaxAgeMs;
    if (!scanner_.busy() && (refresh || old))
    {
        scanner_.start();
    }

    JsonWriter json;
    json.beginObject().key("scanning").boolean(scanner_.busy()).key("networks").beginArray();
    for (const ScanResult& network : NetworkScanner::networkList(scanner_.results(), minSignalDbm_))
    {
        json.beginObject()
            .key("ssid")
            .string(network.ssid)
            .key("rssi")
            .number(network.rssi)
            .key("secured")
            .boolean(network.secured)
            .endObject();
    }
    json.endArray().endObject();
    http_.send(200, "application/json", json.str());
}

// POST /api/connect with ssid, password and, to skip the test, saveAnyway=1. With the
// Management Interface enabled, managementPassword too; it may be left empty when one is stored.
void ProvisioningPortal::receiveNetwork(const HttpRequest& request)
{
    if (result_ == Result::Testing)
    {
        sendError(409, "busy");
        return;
    }
    if (closing_)
    {
        sendError(409, "done");
        return;
    }

    SavedNetwork network;
    network.ssid = request.arg("ssid").value_or("");
    network.password = request.arg("password").value_or("");
    const SavedNetwork::Problem problem = network.validate();
    if (problem != SavedNetwork::Problem::None)
    {
        log_.info("portal", "refused a network: %s", toString(problem));
        sendError(400, code(problem));
        return;
    }
    std::string managementPassword;
    if (management_.enabled())
    {
        managementPassword = request.arg("managementPassword").value_or("");
        const bool keep = managementPassword.empty() && management_.hasPassword();
        if (!keep && !ManagementInterface::isValidPassword(managementPassword))
        {
            log_.info("portal", "refused a network: management password isn't 8 to 63 characters");
            sendError(400, "management-password-invalid");
            return;
        }
    }

    resultSsid_ = network.ssid;
    resultFailure_ = FailureReason::None;
    resultIp_ = Ipv4Address();
    if (request.arg("saveAnyway").value_or("") == "1")
    {
        if (!networks_.save(network))
        {
            sendError(500, "not-saved");
            return;
        }
        log_.info("portal", "saved %s without testing it", network.ssid.c_str());
        candidateManagementPassword_ = managementPassword;
        result_ = Result::Saved;
        provisioned(network.ssid);
    }
    else
    {
        log_.info("portal", "testing %s", network.ssid.c_str());
        candidate_ = network;
        candidateManagementPassword_ = managementPassword;
        result_ = Result::Testing;
        connection_.tryNetwork(network);
    }
    sendResult();
}

void ProvisioningPortal::followTrial()
{
    if (result_ != Result::Testing)
    {
        return;
    }
    // A test takes up to 20 s and can drop the Owner's phone for a while; that isn't
    // inactivity.
    lastActivityMs_ = clock_.nowMs();
    switch (connection_.trial())
    {
    case WifiConnection::Trial::Connected:
        resultIp_ = wifi_.localIp();
        connection_.endTrial();
        if (!networks_.save(candidate_))
        {
            result_ = Result::Failed;
            resultFailure_ = FailureReason::Other;
            log_.error("portal", "%s works but couldn't be saved", candidate_.ssid.c_str());
            break;
        }
        result_ = Result::Connected;
        provisioned(candidate_.ssid);
        break;
    case WifiConnection::Trial::Failed:
        result_ = Result::Failed;
        resultFailure_ = connection_.trialFailure();
        connection_.endTrial();
        break;
    default:
        break;
    }
    // The password isn't needed any more.
    if (result_ != Result::Testing)
    {
        candidate_ = SavedNetwork();
        candidateManagementPassword_.clear();
    }
}

void ProvisioningPortal::provisioned(const std::string& ssid)
{
    if (!candidateManagementPassword_.empty())
    {
        management_.setPassword(candidateManagementPassword_);
        candidateManagementPassword_.clear();
    }
    events_.raiseProvisioningCompleted(ssid);
    closing_ = true;
    closeAtMs_ = clock_.nowMs() + kCloseDelayMs;
}

void ProvisioningPortal::sendResult()
{
    static const char* const kStates[] = {"idle", "testing", "connected", "failed", "saved"};
    JsonWriter json;
    json.beginObject().key("state").string(kStates[static_cast<int>(result_)]);
    if (result_ != Result::None)
    {
        json.key("ssid").string(resultSsid_);
    }
    if (result_ == Result::Connected)
    {
        json.key("ip").string(resultIp_.toString());
    }
    if (result_ == Result::Failed)
    {
        json.key("reason").string(code(resultFailure_));
    }
    json.endObject();
    http_.send(200, "application/json", json.str());
}

void ProvisioningPortal::sendError(int status, const char* error)
{
    JsonWriter json;
    json.beginObject().key("error").string(error).endObject();
    http_.send(status, "application/json", json.str());
}

std::string ProvisioningPortal::pageUrl() const
{
    return "http://" + wifi_.accessPointIp().toString() + "/";
}

uint8_t ProvisioningPortal::chooseChannel(const std::vector<SavedNetwork>& networks,
                                          const std::vector<ScanResult>& scan)
{
    const ScanResult* strongest = nullptr;
    const ScanResult* strongestSaved = nullptr;
    for (const ScanResult& result : scan)
    {
        if (result.channel == 0)
        {
            continue;
        }
        if (strongest == nullptr || result.rssi > strongest->rssi)
        {
            strongest = &result;
        }
        const bool saved =
            std::any_of(networks.begin(), networks.end(), [&result](const SavedNetwork& network)
                        { return network.ssid == result.ssid; });
        if (saved && (strongestSaved == nullptr || result.rssi > strongestSaved->rssi))
        {
            strongestSaved = &result;
        }
    }
    if (strongestSaved != nullptr)
    {
        return strongestSaved->channel;
    }
    return strongest != nullptr ? strongest->channel : kDefaultChannel;
}

bool ProvisioningPortal::isValidPassword(const std::string& password)
{
    if (password.size() < 8 || password.size() > 63)
    {
        return false;
    }
    return std::all_of(password.begin(), password.end(),
                       [](char c) { return c >= 0x20 && c <= 0x7e; });
}

}  // namespace espmgr
