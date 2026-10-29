#pragma once

#include "core/HttpServer.h"

namespace espmgr
{

// An HttpServer without a network. A test sends a request with request() and reads back what
// the handler answered.
class FakeHttpServer : public HttpServer
{
public:
    struct Response
    {
        int status = 0;
        std::string contentType;
        std::string body;
        bool gzipped = false;
        std::string location;
        HttpRequest::Fields headers;

        std::string header(const std::string& name) const
        {
            for (const auto& [key, value] : headers)
            {
                if (key == name)
                {
                    return value;
                }
            }
            return std::string();
        }
        // False if the handler answered nothing.
        bool sent = false;
    };

    using HttpServer::send;

    void begin(uint16_t listenPort) override
    {
        running = true;
        port = listenPort;
    }

    void stop() override
    {
        running = false;
    }

    void handle() override
    {
        ++handleCalls;
    }

    void on(HttpMethod method, const std::string& path, HttpHandler handler) override
    {
        routes_.push_back({method, path, std::move(handler)});
    }

    void onNotFound(HttpHandler handler) override
    {
        notFound_ = std::move(handler);
    }

    void sendHeader(const std::string& name, const std::string& value) override
    {
        response_.headers.emplace_back(name, value);
    }

    void send(int status, const std::string& contentType, const uint8_t* body, size_t length,
              bool gzipped) override
    {
        response_.status = status;
        response_.contentType = contentType;
        response_.body.assign(reinterpret_cast<const char*>(body), length);
        response_.gzipped = gzipped;
        response_.sent = true;
    }

    void sendRedirect(const std::string& location) override
    {
        response_.status = 302;
        response_.location = location;
        response_.sent = true;
    }

    bool startCaptiveDns(const Ipv4Address& apIp) override
    {
        captiveDnsActive = true;
        captiveDnsIp = apIp;
        return true;
    }

    void stopCaptiveDns() override
    {
        captiveDnsActive = false;
    }

    // --- Driven by the test ---

    // Runs the handler for `method` and `path`, or the not-found handler, and returns its
    // answer. A server that isn't running answers nothing.
    Response request(HttpMethod method, const std::string& path, HttpRequest::Fields args = {},
                     HttpRequest::Fields headers = {})
    {
        ++requests;
        response_ = Response();
        if (!running)
        {
            return response_;
        }

        HttpRequest request;
        request.method = method;
        request.path = path;
        request.args = std::move(args);
        request.headers = std::move(headers);
        request.localIp = localIp;

        for (const Route& route : routes_)
        {
            if (route.path == path && (route.method == HttpMethod::Any || route.method == method))
            {
                route.handler(request);
                return response_;
            }
        }
        if (notFound_)
        {
            notFound_(request);
        }
        return response_;
    }

    // The address requests arrive on; the access point's by default.
    Ipv4Address localIp{192, 168, 4, 1};

    // --- Recorded for the test ---

    bool running = false;
    uint16_t port = 0;
    int handleCalls = 0;
    int requests = 0;
    bool captiveDnsActive = false;
    Ipv4Address captiveDnsIp;

private:
    struct Route
    {
        HttpMethod method;
        std::string path;
        HttpHandler handler;
    };

    std::vector<Route> routes_;
    HttpHandler notFound_;
    Response response_;
};

}  // namespace espmgr
