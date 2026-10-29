#pragma once

#include "core/Ipv4Address.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace espmgr
{

enum class HttpMethod
{
    Any,
    Get,
    Post,
    Put,
    Delete,
    Other,
};

struct HttpRequest
{
    using Fields = std::vector<std::pair<std::string, std::string>>;

    HttpMethod method = HttpMethod::Get;
    std::string path;
    // Query and form arguments. A body that isn't a form arrives as the argument "plain".
    Fields args;
    Fields headers;
    // The device's address the request came in on: the station's or the access point's.
    Ipv4Address localIp;

    std::optional<std::string> arg(const std::string& name) const
    {
        return find(args, name);
    }

    // Header names are matched without regard to case.
    std::optional<std::string> header(const std::string& name) const
    {
        for (const auto& [key, value] : headers)
        {
            if (equalsIgnoreCase(key, name))
            {
                return value;
            }
        }
        return std::nullopt;
    }

private:
    static std::optional<std::string> find(const Fields& fields, const std::string& name)
    {
        for (const auto& [key, value] : fields)
        {
            if (key == name)
            {
                return value;
            }
        }
        return std::nullopt;
    }

    static bool equalsIgnoreCase(const std::string& a, const std::string& b)
    {
        if (a.size() != b.size())
        {
            return false;
        }
        for (size_t i = 0; i < a.size(); ++i)
        {
            const char ca = (a[i] >= 'A' && a[i] <= 'Z') ? static_cast<char>(a[i] + 32) : a[i];
            const char cb = (b[i] >= 'A' && b[i] <= 'Z') ? static_cast<char>(b[i] + 32) : b[i];
            if (ca != cb)
            {
                return false;
            }
        }
        return true;
    }
};

// Handlers run inside handle(), so inside process(). Each handler answers its request by
// calling send() or sendRedirect() exactly once.
using HttpHandler = std::function<void(const HttpRequest&)>;

// The web server for the Provisioning Portal and the Management Interface, plus the DNS server
// that makes the Provisioning Portal a captive portal.
class HttpServer
{
public:
    virtual ~HttpServer() = default;

    virtual void begin(uint16_t port) = 0;
    virtual void stop() = 0;
    // Serves pending requests. Called from process().
    virtual void handle() = 0;

    virtual void on(HttpMethod method, const std::string& path, HttpHandler handler) = 0;
    virtual void onNotFound(HttpHandler handler) = 0;

    // Answers the current request. With `gzipped`, the body is already gzip-compressed.
    virtual void send(int status, const std::string& contentType, const uint8_t* body,
                      size_t length, bool gzipped) = 0;
    virtual void sendRedirect(const std::string& location) = 0;

    void send(int status, const std::string& contentType, const std::string& body)
    {
        send(status, contentType, reinterpret_cast<const uint8_t*>(body.data()), body.size(),
             false);
    }

    // True if the current request carries valid Digest credentials. Otherwise sends the 401
    // challenge, and the handler must return without sending anything else.
    virtual bool authenticateDigest(const std::string& realm, const std::string& user,
                                    const std::string& password) = 0;

    // Answers every DNS name with `apIp`, so phones open the Provisioning Portal by themselves.
    virtual bool startCaptiveDns(const Ipv4Address& apIp) = 0;
    virtual void stopCaptiveDns() = 0;
};

}  // namespace espmgr
