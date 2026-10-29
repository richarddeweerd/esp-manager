#pragma once

#include "core/Clock.h"
#include "core/HttpServer.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace espmgr
{

// HTTP Digest authentication (RFC 7616 with MD5 and qop=auth, which every browser supports).
// The password itself is never kept: only HA1 = MD5(user:realm:password), which is what the
// check needs. Nonces are random, valid for 10 minutes, and each request has to count up
// (nc), so a recorded request can't be sent again.
//
// Digest keeps the password off the network, but not the pages: without HTTPS, anyone on the
// network can read the answers.
class DigestAuth
{
public:
    static constexpr uint64_t kNonceLifetimeMs = 10 * 60 * 1000;
    // Nonces remembered at a time; the oldest is dropped for a new one.
    static constexpr size_t kMaxNonces = 8;

    explicit DigestAuth(Clock& clock) : clock_(clock)
    {
    }

    static std::string ha1(const std::string& user, const std::string& realm,
                           const std::string& password);

    enum class Check
    {
        Ok,
        // No Authorization header, or one that isn't valid Digest for this user and realm.
        Refused,
        // The right password, but with a nonce that's expired or unknown, for instance after a
        // restart; the browser retries with a new one without asking the Owner.
        Stale,
    };

    Check check(const HttpRequest& request, const std::string& user, const std::string& realm,
                const std::string& ha1);

    // The WWW-Authenticate header for a 401, with a new nonce.
    std::string challenge(const std::string& realm, bool stale);

private:
    struct Nonce
    {
        std::string value;
        uint64_t issuedMs = 0;
        uint32_t lastCount = 0;
    };

    Clock& clock_;
    Nonce nonces_[kMaxNonces];
    size_t next_ = 0;
};

}  // namespace espmgr
