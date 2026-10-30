#include "core/ManagementInterface.h"

#include "core/JsonWriter.h"
#include "web/Pages.h"

#include <algorithm>

namespace espmgr
{

namespace
{

constexpr const char* kHashKey = "ha1";
// The user and realm the hash belongs to.
constexpr const char* kIdKey = "id";

bool isQuotable(const std::string& text)
{
    return std::none_of(text.begin(), text.end(),
                        [](char c) { return c == '"' || c == '\\' || c < 0x20 || c == 0x7f; });
}

}  // namespace

ManagementInterface& ManagementInterface::enable()
{
    if (begun_)
    {
        log_.warn("mgmt", "enable() after begin() has no effect");
        return *this;
    }
    enabled_ = true;
    return *this;
}

bool ManagementInterface::setUser(const std::string& user)
{
    if (user.empty() || user.size() > 32 || !isQuotable(user) ||
        user.find(':') != std::string::npos)
    {
        log_.warn("mgmt", "ignoring an invalid user name");
        return false;
    }
    user_ = user;
    if (begun_)
    {
        begin();
    }
    return true;
}

bool ManagementInterface::setRealm(const std::string& realm)
{
    if (realm.size() > 64 || !isQuotable(realm))
    {
        log_.warn("mgmt", "ignoring an invalid realm");
        return false;
    }
    realm_ = realm;
    if (begun_)
    {
        begin();
    }
    return true;
}

std::string ManagementInterface::realm() const
{
    return realm_.empty() ? deviceName_() : realm_;
}

bool ManagementInterface::setPassword(const std::string& password)
{
    if (!isValidPassword(password))
    {
        log_.warn("mgmt", "ignoring a password that isn't 8 to 63 characters");
        return false;
    }
    const std::string hash = DigestAuth::ha1(user_, realm(), password);
    if (!storage_.putString(kStorageNamespace, kHashKey, hash) ||
        !storage_.putString(kStorageNamespace, kIdKey, storedId()))
    {
        log_.error("mgmt", "couldn't store the password");
        return false;
    }
    ha1_ = hash;
    log_.info("mgmt", "password set for user %s", user_.c_str());
    return true;
}

bool ManagementInterface::hasPassword() const
{
    return !ha1_.empty();
}

bool ManagementInterface::isValidPassword(const std::string& password)
{
    if (password.size() < 8 || password.size() > 63)
    {
        return false;
    }
    return std::all_of(password.begin(), password.end(),
                       [](char c) { return c >= 0x20 && c <= 0x7e; });
}

std::string ManagementInterface::storedId() const
{
    return user_ + ":" + realm();
}

void ManagementInterface::begin()
{
    if (!enabled_)
    {
        return;
    }
    const bool first = !begun_;
    begun_ = true;
    ha1_.clear();
    const auto hash = storage_.getString(kStorageNamespace, kHashKey);
    if (hash && storage_.getString(kStorageNamespace, kIdKey).value_or("") == storedId())
    {
        ha1_ = *hash;
    }
    else if (hash)
    {
        log_.warn("mgmt", "the stored password is for another user or realm; set it again in "
                          "the Provisioning Portal");
    }
    else
    {
        log_.warn("mgmt", "no password yet; off until the Owner sets one in the Provisioning "
                          "Portal");
    }
    if (first)
    {
        registerRoutes();
    }
}

void ManagementInterface::process()
{
    if (!begun_)
    {
        return;
    }
    if (pending_ && clock_.nowMs() >= actionAtMs_)
    {
        const std::function<void()> action = std::move(pending_);
        pending_ = nullptr;
        action();
    }
    const bool wanted = hasPassword() && connection_.state() == ConnectionState::Connected;
    if (wanted != active())
    {
        routes_.setActive(WebRoutes::Site::Management, wanted);
        if (wanted)
        {
            log_.info("mgmt", "answering on port %u", static_cast<unsigned>(WebRoutes::kHttpPort));
        }
    }
}

void ManagementInterface::registerRoutes()
{
    using Site = WebRoutes::Site;
    routes_.on(Site::Management, HttpMethod::Get, "/",
               [this](const HttpRequest& request)
               {
                   if (authorize(request))
                   {
                       web::sendManagementPage(routes_.http());
                   }
               });
    routes_.on(Site::Management, HttpMethod::Get, "/api/status",
               [this](const HttpRequest& request)
               {
                   if (authorize(request))
                   {
                       JsonWriter json;
                       writeJson(json, status_());
                       routes_.http().send(200, "application/json", json.str());
                   }
               });
    // Removing the last Saved Network here would be a Network Reset, which has its own button.
    networksApi_.registerRoutes(Site::Management,
                                [this](const HttpRequest& request) { return authorize(request); },
                                {/*add=*/true, /*removeLast=*/false});
    registerAction("/api/restart", "restart", &Actions::restart);
    registerAction("/api/network-reset", "Network Reset", &Actions::networkReset);
    registerAction("/api/factory-reset", "Factory Reset", &Actions::factoryReset);
}

void ManagementInterface::registerAction(const char* path, const char* name,
                                         std::function<void()> Actions::* action)
{
    routes_.on(WebRoutes::Site::Management, HttpMethod::Post, path,
               [this, name, action](const HttpRequest& request)
               {
                   if (!authorize(request))
                   {
                       return;
                   }
                   HttpServer& http = routes_.http();
                   if (request.header("X-Requested-With").value_or("") != "EspManager")
                   {
                       log_.warn("mgmt", "refused %s without X-Requested-With", name);
                       http.send(403, "text/plain", "Use the management page.");
                       return;
                   }
                   log_.info("mgmt", "%s requested", name);
                   pending_ = actions_.*action;
                   actionAtMs_ = clock_.nowMs() + kActionDelayMs;
                   http.send(200, "application/json", "{\"ok\":true}");
               });
}

bool ManagementInterface::authorize(const HttpRequest& request)
{
    const DigestAuth::Check check = auth_.check(request, user_, realm(), ha1_);
    if (check == DigestAuth::Check::Ok)
    {
        return true;
    }
    // Arduino's WebServer reports an empty Authorization header when there is none.
    if (!request.header("Authorization").value_or("").empty() &&
        check == DigestAuth::Check::Refused)
    {
        log_.info("mgmt", "refused a login");
    }
    HttpServer& http = routes_.http();
    http.sendHeader("WWW-Authenticate",
                    auth_.challenge(realm(), check == DigestAuth::Check::Stale));
    http.send(401, "text/plain", "Log in to manage this device.");
    return false;
}

}  // namespace espmgr
