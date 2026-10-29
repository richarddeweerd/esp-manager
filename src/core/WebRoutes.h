#pragma once

#include "core/HttpServer.h"
#include "core/WifiDriver.h"

#include <cstdint>
#include <string>
#include <vector>

namespace espmgr
{

// Shares the one web server between the Provisioning Portal and the Management Interface. Each
// path is registered with the server once; a request goes to the Provisioning Portal when it
// arrives on the access point's address, and to the Management Interface otherwise. A site
// that isn't active answers nothing: such requests get 404, or 403 when they reached the
// station's address while only the Provisioning Portal is active.
//
// The server runs while either site is active.
class WebRoutes
{
public:
    enum class Site
    {
        Portal,
        Management,
    };

    static constexpr uint16_t kHttpPort = 80;

    WebRoutes(WifiDriver& wifi, HttpServer& http);

    WebRoutes(const WebRoutes&) = delete;
    WebRoutes& operator=(const WebRoutes&) = delete;

    void on(Site site, HttpMethod method, const std::string& path, HttpHandler handler);
    // For every path the site has no handler for.
    void onNotFound(Site site, HttpHandler handler);

    // Starts the server when the first site becomes active, and stops it after the last.
    void setActive(Site site, bool active);
    bool active(Site site) const
    {
        return active_[index(site)];
    }

    // Serves pending requests. Called from process().
    void handle();

    HttpServer& http()
    {
        return http_;
    }

private:
    struct Route
    {
        Site site;
        HttpMethod method;
        std::string path;
        HttpHandler handler;
    };

    static int index(Site site)
    {
        return site == Site::Portal ? 0 : 1;
    }

    void dispatch(const HttpRequest& request, const std::string& path);
    void registerPath(HttpMethod method, const std::string& path);
    bool running() const
    {
        return active_[0] || active_[1];
    }

    WifiDriver& wifi_;
    HttpServer& http_;
    std::vector<Route> routes_;
    HttpHandler notFound_[2];
    bool active_[2] = {false, false};
};

}  // namespace espmgr
