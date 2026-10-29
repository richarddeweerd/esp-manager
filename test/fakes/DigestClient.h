#pragma once

#include "core/Md5.h"

#include <string>

namespace espmgr
{

// What a browser sends for HTTP Digest (qop=auth, MD5), for tests of the Management Interface.
struct DigestClient
{
    std::string user = "admin";
    std::string realm;
    std::string password;
    std::string cnonce = "0a4f113b";

    // The nonce from a WWW-Authenticate header, or empty.
    static std::string nonceFrom(const std::string& challenge)
    {
        const std::string start = "nonce=\"";
        const size_t at = challenge.find(start);
        if (at == std::string::npos)
        {
            return std::string();
        }
        const size_t from = at + start.size();
        return challenge.substr(from, challenge.find('"', from) - from);
    }

    std::string response(const std::string& nonce, const std::string& method,
                         const std::string& uri, const std::string& count) const
    {
        const std::string ha1 = md5Hex(user + ":" + realm + ":" + password);
        const std::string ha2 = md5Hex(method + ":" + uri);
        return md5Hex(ha1 + ":" + nonce + ":" + count + ":" + cnonce + ":auth:" + ha2);
    }

    // The Authorization header.
    std::string header(const std::string& nonce, const std::string& method, const std::string& uri,
                       const std::string& count = "00000001") const
    {
        return "Digest username=\"" + user + "\", realm=\"" + realm + "\", nonce=\"" + nonce +
               "\", uri=\"" + uri + "\", algorithm=MD5, qop=auth, nc=" + count + ", cnonce=\"" +
               cnonce + "\", response=\"" + response(nonce, method, uri, count) + "\"";
    }
};

}  // namespace espmgr
