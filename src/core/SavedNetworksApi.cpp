#include "core/SavedNetworksApi.h"

#include "core/JsonWriter.h"

#include <cstdlib>

namespace espmgr
{

void SavedNetworksApi::registerRoutes(WebRoutes::Site site, Authorize authorize, Options options)
{
    routes_.on(site, HttpMethod::Get, "/api/networks",
               [this, authorize](const HttpRequest& request)
               {
                   if (authorize(request))
                   {
                       sendList();
                   }
               });
    // A change is allowed for an authorised request that comes from the page itself.
    const auto onChange =
        [this, site, authorize](const char* path, std::function<void(const HttpRequest&)> change)
    {
        routes_.on(site, HttpMethod::Post, path,
                   [this, authorize, change](const HttpRequest& request)
                   {
                       if (!authorize(request))
                       {
                           return;
                       }
                       if (request.header("X-Requested-With").value_or("") != "EspManager")
                       {
                           log_.warn("networks", "refused a change without X-Requested-With");
                           routes_.http().send(403, "text/plain", "Use the setup page.");
                           return;
                       }
                       change(request);
                   });
    };
    onChange("/api/networks/remove",
             [this, options](const HttpRequest& request) { remove(request, options); });
    onChange("/api/networks/move", [this](const HttpRequest& request) { move(request); });
    if (options.add)
    {
        onChange("/api/networks/add", [this](const HttpRequest& request) { add(request); });
    }
}

void SavedNetworksApi::sendList()
{
    JsonWriter json;
    json.beginObject()
        .key("connected")
        .string(connection_.state() == ConnectionState::Connected ? connection_.ssid() : "")
        .key("max")
        .number(static_cast<int64_t>(networks_.maxNetworks()))
        .key("networks")
        .beginArray();
    for (const SavedNetwork& network : networks_.list())
    {
        json.beginObject()
            .key("ssid")
            .string(network.ssid)
            .key("priority")
            .number(network.priority)
            .endObject();
    }
    json.endArray().endObject();
    routes_.http().send(200, "application/json", json.str());
}

void SavedNetworksApi::sendError(int status, const char* error)
{
    JsonWriter json;
    json.beginObject().key("error").string(error).endObject();
    routes_.http().send(status, "application/json", json.str());
}

void SavedNetworksApi::add(const HttpRequest& request)
{
    SavedNetwork network;
    network.ssid = request.arg("ssid").value_or("");
    network.password = request.arg("password").value_or("");
    const SavedNetwork::Problem problem = network.validate();
    if (problem != SavedNetwork::Problem::None)
    {
        log_.info("networks", "refused a network: %s", toString(problem));
        sendError(400, errorCode(problem));
        return;
    }
    if (networks_.full() && !networks_.contains(network.ssid))
    {
        sendError(409, "full");
        return;
    }
    if (!networks_.save(network))
    {
        sendError(500, "not-saved");
        return;
    }
    sendList();
}

void SavedNetworksApi::remove(const HttpRequest& request, const Options& options)
{
    const std::string ssid = request.arg("ssid").value_or("");
    if (!networks_.contains(ssid))
    {
        sendError(404, "not-found");
        return;
    }
    if (!options.removeLast && networks_.count() == 1)
    {
        sendError(409, "last-network");
        return;
    }
    if (!networks_.remove(ssid))
    {
        sendError(500, "not-saved");
        return;
    }
    sendList();
}

void SavedNetworksApi::move(const HttpRequest& request)
{
    const std::string ssid = request.arg("ssid").value_or("");
    const std::string text = request.arg("position").value_or("");
    char* end = nullptr;
    const unsigned long position = std::strtoul(text.c_str(), &end, 10);
    if (!networks_.contains(ssid))
    {
        sendError(404, "not-found");
        return;
    }
    if (text.empty() || *end != '\0' || text[0] == '-' || position >= networks_.count())
    {
        sendError(400, "position");
        return;
    }
    if (!networks_.move(ssid, position))
    {
        sendError(500, "not-saved");
        return;
    }
    sendList();
}

}  // namespace espmgr
