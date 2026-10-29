#include "core/DigestAuth.h"

#include "core/Md5.h"

#include <cstdio>
#include <cstdlib>
#include <map>

namespace espmgr
{

namespace
{

const char* methodName(HttpMethod method)
{
    switch (method)
    {
    case HttpMethod::Get:
        return "GET";
    case HttpMethod::Post:
        return "POST";
    case HttpMethod::Put:
        return "PUT";
    case HttpMethod::Delete:
        return "DELETE";
    default:
        return "";
    }
}

// The fields of `Digest a="b", c=d, …`; empty if it isn't Digest or can't be read.
std::map<std::string, std::string> parseFields(const std::string& header)
{
    std::map<std::string, std::string> fields;
    const std::string scheme = "Digest ";
    if (header.compare(0, scheme.size(), scheme) != 0)
    {
        return fields;
    }
    size_t i = scheme.size();
    while (i < header.size())
    {
        while (i < header.size() && (header[i] == ' ' || header[i] == ','))
        {
            ++i;
        }
        const size_t equals = header.find('=', i);
        if (equals == std::string::npos)
        {
            break;
        }
        const std::string name = header.substr(i, equals - i);
        i = equals + 1;
        std::string value;
        if (i < header.size() && header[i] == '"')
        {
            ++i;
            while (i < header.size() && header[i] != '"')
            {
                if (header[i] == '\\' && i + 1 < header.size())
                {
                    ++i;
                }
                value += header[i++];
            }
            if (i >= header.size())
            {
                return {};
            }
            ++i;
        }
        else
        {
            while (i < header.size() && header[i] != ',' && header[i] != ' ')
            {
                value += header[i++];
            }
        }
        fields[name] = value;
    }
    return fields;
}

// Compares without stopping at the first difference.
bool sameText(const std::string& a, const std::string& b)
{
    if (a.size() != b.size())
    {
        return false;
    }
    unsigned char difference = 0;
    for (size_t i = 0; i < a.size(); ++i)
    {
        difference |= static_cast<unsigned char>(a[i] ^ b[i]);
    }
    return difference == 0;
}

}  // namespace

std::string DigestAuth::ha1(const std::string& user, const std::string& realm,
                            const std::string& password)
{
    return md5Hex(user + ":" + realm + ":" + password);
}

DigestAuth::Check DigestAuth::check(const HttpRequest& request, const std::string& user,
                                    const std::string& realm, const std::string& ha1)
{
    auto fields = parseFields(request.header("Authorization").value_or(""));
    const std::string& nonce = fields["nonce"];
    const std::string& uri = fields["uri"];
    const std::string& count = fields["nc"];
    const std::string& cnonce = fields["cnonce"];
    if (ha1.empty() || fields["username"] != user || fields["realm"] != realm || nonce.empty() ||
        fields["qop"] != "auth" || count.size() != 8 || cnonce.empty() ||
        (!fields["algorithm"].empty() && fields["algorithm"] != "MD5"))
    {
        return Check::Refused;
    }
    // The uri carries the query, the path doesn't.
    if (uri.substr(0, uri.find('?')) != request.path)
    {
        return Check::Refused;
    }

    const std::string ha2 = md5Hex(std::string(methodName(request.method)) + ":" + uri);
    const std::string expected =
        md5Hex(ha1 + ":" + nonce + ":" + count + ":" + cnonce + ":auth:" + ha2);
    if (!sameText(expected, fields["response"]))
    {
        return Check::Refused;
    }

    char* end = nullptr;
    const unsigned long counter = std::strtoul(count.c_str(), &end, 16);
    for (Nonce& known : nonces_)
    {
        if (known.value.empty() || known.value != nonce)
        {
            continue;
        }
        if (clock_.nowMs() - known.issuedMs >= kNonceLifetimeMs)
        {
            return Check::Stale;
        }
        // The same count again is a replayed request.
        if (*end != '\0' || counter <= known.lastCount)
        {
            return Check::Refused;
        }
        known.lastCount = static_cast<uint32_t>(counter);
        return Check::Ok;
    }
    return Check::Stale;
}

std::string DigestAuth::challenge(const std::string& realm, bool stale)
{
    Nonce& nonce = nonces_[next_];
    next_ = (next_ + 1) % kMaxNonces;
    char hex[9];
    nonce.value.clear();
    for (int i = 0; i < 4; ++i)
    {
        std::snprintf(hex, sizeof(hex), "%08lx",
                      static_cast<unsigned long>(clock_.random(0xffffffff)));
        nonce.value += hex;
    }
    nonce.issuedMs = clock_.nowMs();
    nonce.lastCount = 0;
    return "Digest realm=\"" + realm + "\", qop=\"auth\", algorithm=MD5, nonce=\"" + nonce.value +
           "\"" + (stale ? ", stale=true" : "");
}

}  // namespace espmgr
