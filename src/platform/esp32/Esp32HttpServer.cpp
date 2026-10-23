#include "platform/esp32/Esp32HttpServer.h"

#include <DNSServer.h>
#include <WebServer.h>

namespace espmgr
{

namespace
{

constexpr uint16_t kDnsPort = 53;

HTTPMethod toArduino(HttpMethod method)
{
    switch (method)
    {
    case HttpMethod::Get:
        return HTTP_GET;
    case HttpMethod::Post:
        return HTTP_POST;
    case HttpMethod::Put:
        return HTTP_PUT;
    case HttpMethod::Delete:
        return HTTP_DELETE;
    default:
        return HTTP_ANY;
    }
}

HttpMethod fromArduino(HTTPMethod method)
{
    switch (method)
    {
    case HTTP_GET:
        return HttpMethod::Get;
    case HTTP_POST:
        return HttpMethod::Post;
    case HTTP_PUT:
        return HttpMethod::Put;
    case HTTP_DELETE:
        return HttpMethod::Delete;
    default:
        return HttpMethod::Other;
    }
}

std::string toStd(const String& value)
{
    return std::string(value.c_str(), value.length());
}

}  // namespace

Esp32HttpServer::Esp32HttpServer() = default;

Esp32HttpServer::~Esp32HttpServer() = default;

void Esp32HttpServer::begin(uint16_t port)
{
    stop();
    server_ = std::make_unique<WebServer>(port);
    // The captive portal needs the Host header, and handlers may read any other.
    server_->collectAllHeaders();
    for (const Route& route : routes_)
    {
        registerRoute(route);
    }
    if (notFound_)
    {
        onNotFound(notFound_);
    }
    server_->begin();
}

void Esp32HttpServer::stop()
{
    if (server_)
    {
        server_->stop();
        server_.reset();
    }
}

void Esp32HttpServer::handle()
{
    if (server_)
    {
        server_->handleClient();
    }
    if (dns_)
    {
        dns_->processNextRequest();
    }
}

void Esp32HttpServer::on(HttpMethod method, const std::string& path, HttpHandler handler)
{
    routes_.push_back({method, path, std::move(handler)});
    if (server_)
    {
        registerRoute(routes_.back());
    }
}

void Esp32HttpServer::registerRoute(const Route& route)
{
    HttpHandler handler = route.handler;
    server_->on(route.path.c_str(), toArduino(route.method),
                [this, handler]() { handler(currentRequest()); });
}

void Esp32HttpServer::onNotFound(HttpHandler handler)
{
    notFound_ = std::move(handler);
    if (server_)
    {
        HttpHandler notFound = notFound_;
        server_->onNotFound([this, notFound]() { notFound(currentRequest()); });
    }
}

HttpRequest Esp32HttpServer::currentRequest() const
{
    HttpRequest request;
    request.method = fromArduino(server_->method());
    request.path = toStd(server_->uri());
    for (int i = 0; i < server_->args(); ++i)
    {
        request.args.emplace_back(toStd(server_->argName(i)), toStd(server_->arg(i)));
    }
    for (int i = 0; i < server_->headers(); ++i)
    {
        request.headers.emplace_back(toStd(server_->headerName(i)), toStd(server_->header(i)));
    }
    return request;
}

void Esp32HttpServer::send(int status, const std::string& contentType, const uint8_t* body,
                           size_t length, bool gzipped)
{
    if (!server_)
    {
        return;
    }
    if (gzipped)
    {
        server_->sendHeader("Content-Encoding", "gzip");
    }
    server_->send_P(status, contentType.c_str(), reinterpret_cast<const char*>(body), length);
}

void Esp32HttpServer::sendRedirect(const std::string& location)
{
    if (!server_)
    {
        return;
    }
    server_->sendHeader("Location", location.c_str(), true);
    server_->send(302, "text/plain", "");
}

bool Esp32HttpServer::authenticateDigest(const std::string& realm, const std::string& user,
                                         const std::string& password)
{
    if (!server_)
    {
        return false;
    }
    if (server_->authenticate(user.c_str(), password.c_str()))
    {
        return true;
    }
    server_->requestAuthentication(DIGEST_AUTH, realm.c_str());
    return false;
}

bool Esp32HttpServer::startCaptiveDns(const Ipv4Address& apIp)
{
    stopCaptiveDns();
    dns_ = std::make_unique<DNSServer>();
    if (!dns_->start(kDnsPort, "*", IPAddress(apIp[0], apIp[1], apIp[2], apIp[3])))
    {
        dns_.reset();
        return false;
    }
    return true;
}

void Esp32HttpServer::stopCaptiveDns()
{
    if (dns_)
    {
        dns_->stop();
        dns_.reset();
    }
}

}  // namespace espmgr
