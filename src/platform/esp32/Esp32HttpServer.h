#pragma once

#include "core/HttpServer.h"

#include <memory>

class DNSServer;
class WebServer;

namespace espmgr
{

// HttpServer on Arduino-ESP32's WebServer and DNSServer. The servers are created by begin() and
// startCaptiveDns(), so nothing is allocated while the web pages aren't in use.
class Esp32HttpServer : public HttpServer
{
public:
    Esp32HttpServer();
    ~Esp32HttpServer() override;

    using HttpServer::send;

    void begin(uint16_t port) override;
    void stop() override;
    void handle() override;

    void on(HttpMethod method, const std::string& path, HttpHandler handler) override;
    void onNotFound(HttpHandler handler) override;

    void send(int status, const std::string& contentType, const uint8_t* body, size_t length,
              bool gzipped) override;
    void sendRedirect(const std::string& location) override;

    bool authenticateDigest(const std::string& realm, const std::string& user,
                            const std::string& password) override;

    bool startCaptiveDns(const Ipv4Address& apIp) override;
    void stopCaptiveDns() override;

private:
    struct Route
    {
        HttpMethod method;
        std::string path;
        HttpHandler handler;
    };

    void registerRoute(const Route& route);
    HttpRequest currentRequest() const;

    std::unique_ptr<WebServer> server_;
    std::unique_ptr<DNSServer> dns_;
    // Kept so routes added before begin(), or before a restart, are registered on the new server.
    std::vector<Route> routes_;
    HttpHandler notFound_;
};

}  // namespace espmgr
