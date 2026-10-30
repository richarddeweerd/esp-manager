#pragma once

#include "core/HttpServer.h"
#include "core/Logger.h"
#include "core/SavedNetworks.h"
#include "core/WebRoutes.h"
#include "core/WifiConnection.h"

#include <functional>

namespace espmgr
{

// The Saved Networks as the web pages see them, shared by the Provisioning Portal and the
// Management Interface (US05.2.1, US05.2.3, US05.2.4):
//
// - GET /api/networks: {"connected":"<ssid>","max":5,"networks":[{"ssid":…,"priority":0},…]}
// - POST /api/networks/remove: ssid
// - POST /api/networks/move: ssid, position (0 is the top)
// - POST /api/networks/add: ssid, password; saved without testing (only where enabled)
//
// Passwords are never sent back. A change answers with the updated list, a refusal with
// {"error":"<code>"}. The POST requests must carry the header `X-Requested-With: EspManager`,
// which another web site can't add, so it can't change the list through the Owner's browser.
class SavedNetworksApi
{
public:
    // What a site allows beyond reading, removing and moving.
    struct Options
    {
        // POST /api/networks/add. The Management Interface has it; the Provisioning Portal tests
        // a network before saving it, through /api/connect.
        bool add = false;
        // False refuses to remove the last Saved Network (409 "last-network"): on the station's
        // network that's a Network Reset, which has its own confirmation.
        bool removeLast = true;
    };

    // Answers the request itself and returns false if it isn't allowed.
    using Authorize = std::function<bool(const HttpRequest&)>;

    SavedNetworksApi(WebRoutes& routes, SavedNetworks& networks, WifiConnection& connection,
                     Logger& log)
        : routes_(routes), networks_(networks), connection_(connection), log_(log)
    {
    }

    SavedNetworksApi(const SavedNetworksApi&) = delete;
    SavedNetworksApi& operator=(const SavedNetworksApi&) = delete;

    void registerRoutes(WebRoutes::Site site, Authorize authorize, Options options);

private:
    void sendList();
    void sendError(int status, const char* error);
    void add(const HttpRequest& request);
    void remove(const HttpRequest& request, const Options& options);
    void move(const HttpRequest& request);

    WebRoutes& routes_;
    SavedNetworks& networks_;
    WifiConnection& connection_;
    Logger& log_;
};

}  // namespace espmgr
