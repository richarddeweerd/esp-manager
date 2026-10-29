#include "core/WebRoutes.h"

#include <utility>

namespace espmgr
{

WebRoutes::WebRoutes(WifiDriver& wifi, HttpServer& http) : wifi_(wifi), http_(http)
{
    http_.onNotFound([this](const HttpRequest& request) { dispatch(request, std::string()); });
}

void WebRoutes::on(Site site, HttpMethod method, const std::string& path, HttpHandler handler)
{
    bool known = false;
    for (const Route& route : routes_)
    {
        known = known || (route.method == method && route.path == path);
    }
    routes_.push_back({site, method, path, std::move(handler)});
    if (!known)
    {
        registerPath(method, path);
    }
}

void WebRoutes::onNotFound(Site site, HttpHandler handler)
{
    notFound_[index(site)] = std::move(handler);
}

void WebRoutes::registerPath(HttpMethod method, const std::string& path)
{
    http_.on(method, path, [this, path](const HttpRequest& request) { dispatch(request, path); });
}

void WebRoutes::setActive(Site site, bool active)
{
    const bool wasRunning = running();
    active_[index(site)] = active;
    if (!wasRunning && running())
    {
        http_.begin(kHttpPort);
    }
    else if (wasRunning && !running())
    {
        http_.stop();
    }
}

void WebRoutes::handle()
{
    if (running())
    {
        http_.handle();
    }
}

// `path` is the registered path, or empty for one nobody registered.
void WebRoutes::dispatch(const HttpRequest& request, const std::string& path)
{
    const Site site = request.localIp == wifi_.accessPointIp() ? Site::Portal : Site::Management;
    if (!active(site))
    {
        // Anyone who reaches the Provisioning Portal can change the Wi-Fi settings, so it
        // answers only on its own access point, not on the network the station is connected to.
        if (site == Site::Management && active(Site::Portal))
        {
            http_.send(403, "text/plain",
                       "The setup pages are only available on the device's own Wi-Fi network.");
        }
        else
        {
            http_.send(404, "text/plain", "Not found");
        }
        return;
    }
    if (!path.empty())
    {
        for (const Route& route : routes_)
        {
            if (route.site == site && route.path == path &&
                (route.method == HttpMethod::Any || route.method == request.method))
            {
                route.handler(request);
                return;
            }
        }
    }
    if (notFound_[index(site)])
    {
        notFound_[index(site)](request);
    }
    else
    {
        http_.send(404, "text/plain", "Not found");
    }
}

}  // namespace espmgr
