#include "core/Manifest.h"

#include <cstdint>

namespace espmgr
{

namespace
{

// A reader for one flat JSON object; nested objects and arrays are skipped.
class Reader
{
public:
    explicit Reader(const std::string& text) : text_(text)
    {
    }

    void skipSpace()
    {
        while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\t' ||
                                       text_[pos_] == '\n' || text_[pos_] == '\r'))
        {
            ++pos_;
        }
    }

    bool take(char c)
    {
        skipSpace();
        if (pos_ < text_.size() && text_[pos_] == c)
        {
            ++pos_;
            return true;
        }
        return false;
    }

    char peek()
    {
        skipSpace();
        return pos_ < text_.size() ? text_[pos_] : '\0';
    }

    bool atEnd()
    {
        skipSpace();
        return pos_ >= text_.size();
    }

    std::optional<std::string> string()
    {
        if (!take('"'))
        {
            return std::nullopt;
        }
        std::string out;
        while (pos_ < text_.size())
        {
            const char c = text_[pos_++];
            if (c == '"')
            {
                return out;
            }
            if (static_cast<unsigned char>(c) < 0x20)
            {
                return std::nullopt;
            }
            if (c != '\\')
            {
                out += c;
                continue;
            }
            if (pos_ >= text_.size())
            {
                return std::nullopt;
            }
            const char e = text_[pos_++];
            switch (e)
            {
            case '"':
            case '\\':
            case '/':
                out += e;
                break;
            case 'b':
                out += '\b';
                break;
            case 'f':
                out += '\f';
                break;
            case 'n':
                out += '\n';
                break;
            case 'r':
                out += '\r';
                break;
            case 't':
                out += '\t';
                break;
            case 'u':
            {
                const std::optional<uint32_t> code = hex4();
                if (!code)
                {
                    return std::nullopt;
                }
                appendUtf8(out, *code);
                break;
            }
            default:
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    // A non-negative integer.
    std::optional<uint64_t> integer()
    {
        skipSpace();
        const size_t start = pos_;
        uint64_t value = 0;
        while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9')
        {
            if (pos_ - start >= 15)
            {
                return std::nullopt;
            }
            value = value * 10 + static_cast<uint64_t>(text_[pos_] - '0');
            ++pos_;
        }
        if (pos_ == start)
        {
            return std::nullopt;
        }
        return value;
    }

    // Skips any value; false if it isn't valid JSON.
    bool skipValue(int depth = 0)
    {
        if (depth > 8)
        {
            return false;
        }
        const char c = peek();
        if (c == '"')
        {
            return string().has_value();
        }
        if (c == '{' || c == '[')
        {
            const char close = c == '{' ? '}' : ']';
            ++pos_;
            if (take(close))
            {
                return true;
            }
            for (;;)
            {
                if (c == '{' && (!string() || !take(':')))
                {
                    return false;
                }
                if (!skipValue(depth + 1))
                {
                    return false;
                }
                if (take(close))
                {
                    return true;
                }
                if (!take(','))
                {
                    return false;
                }
            }
        }
        // A number, true, false or null.
        const size_t start = pos_;
        while (pos_ < text_.size())
        {
            const char d = text_[pos_];
            const bool part = (d >= '0' && d <= '9') || (d >= 'a' && d <= 'z') || d == '-' ||
                              d == '+' || d == '.' || d == 'E';
            if (!part)
            {
                break;
            }
            ++pos_;
        }
        const std::string word = text_.substr(start, pos_ - start);
        if (word.empty())
        {
            return false;
        }
        if (word == "true" || word == "false" || word == "null")
        {
            return true;
        }
        const char first = word[0];
        return first == '-' || (first >= '0' && first <= '9');
    }

private:
    std::optional<uint32_t> hex4()
    {
        if (pos_ + 4 > text_.size())
        {
            return std::nullopt;
        }
        uint32_t code = 0;
        for (int i = 0; i < 4; ++i)
        {
            const char h = text_[pos_++];
            code <<= 4;
            if (h >= '0' && h <= '9')
            {
                code |= static_cast<uint32_t>(h - '0');
            }
            else if (h >= 'a' && h <= 'f')
            {
                code |= static_cast<uint32_t>(h - 'a' + 10);
            }
            else if (h >= 'A' && h <= 'F')
            {
                code |= static_cast<uint32_t>(h - 'A' + 10);
            }
            else
            {
                return std::nullopt;
            }
        }
        return code;
    }

    // Surrogate pairs aren't combined; release notes rarely need characters beyond the BMP.
    static void appendUtf8(std::string& out, uint32_t code)
    {
        if (code < 0x80)
        {
            out += static_cast<char>(code);
        }
        else if (code < 0x800)
        {
            out += static_cast<char>(0xc0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3f));
        }
        else
        {
            out += static_cast<char>(0xe0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (code & 0x3f));
        }
    }

    const std::string& text_;
    size_t pos_ = 0;
};

bool isHex64(std::string& text)
{
    if (text.size() != 64)
    {
        return false;
    }
    for (char& c : text)
    {
        if (c >= 'A' && c <= 'F')
        {
            c = static_cast<char>(c - 'A' + 'a');
        }
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
        {
            return false;
        }
    }
    return true;
}

// Cuts `text` to at most `max` bytes without splitting a UTF-8 character.
void cutUtf8(std::string& text, size_t max)
{
    if (text.size() <= max)
    {
        return;
    }
    size_t end = max;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80)
    {
        --end;
    }
    text.resize(end);
}

}  // namespace

std::optional<Manifest> Manifest::parse(const std::string& json)
{
    if (json.size() > kMaxSize)
    {
        return std::nullopt;
    }
    Reader reader(json);
    if (!reader.take('{'))
    {
        return std::nullopt;
    }
    Manifest manifest;
    bool haveSize = false;
    if (!reader.take('}'))
    {
        for (;;)
        {
            const std::optional<std::string> key = reader.string();
            if (!key || !reader.take(':'))
            {
                return std::nullopt;
            }
            std::string* field = nullptr;
            if (*key == "version")
            {
                field = &manifest.version;
            }
            else if (*key == "url")
            {
                field = &manifest.url;
            }
            else if (*key == "sha256")
            {
                field = &manifest.sha256;
            }
            else if (*key == "signature")
            {
                field = &manifest.signature;
            }
            else if (*key == "minimumVersion")
            {
                field = &manifest.minimumVersion;
            }
            else if (*key == "notes")
            {
                field = &manifest.notes;
            }
            if (field != nullptr)
            {
                const std::optional<std::string> value = reader.string();
                if (!value)
                {
                    return std::nullopt;
                }
                *field = *value;
            }
            else if (*key == "size")
            {
                const std::optional<uint64_t> value = reader.integer();
                if (!value)
                {
                    return std::nullopt;
                }
                manifest.size = static_cast<size_t>(*value);
                haveSize = true;
            }
            else if (!reader.skipValue())
            {
                return std::nullopt;
            }
            if (reader.take('}'))
            {
                break;
            }
            if (!reader.take(','))
            {
                return std::nullopt;
            }
        }
    }
    if (!reader.atEnd())
    {
        return std::nullopt;
    }
    if (manifest.version.empty() || manifest.url.empty() || !haveSize || manifest.size == 0 ||
        !isHex64(manifest.sha256))
    {
        return std::nullopt;
    }
    cutUtf8(manifest.notes, kMaxNotes);
    return manifest;
}

std::string resolveUrl(const std::string& manifestUrl, const std::string& url)
{
    if (url.rfind("https://", 0) == 0)
    {
        return url;
    }
    const size_t scheme = url.find("://");
    const size_t slash = url.find('/');
    if (scheme != std::string::npos && (slash == std::string::npos || scheme < slash))
    {
        return std::string();
    }
    if (url.rfind("//", 0) == 0)
    {
        return "https:" + url;
    }
    const std::string base = withoutQuery(manifestUrl);
    if (base.rfind("https://", 0) != 0)
    {
        return std::string();
    }
    const size_t pathStart = base.find('/', 8);
    if (!url.empty() && url[0] == '/')
    {
        return (pathStart == std::string::npos ? base : base.substr(0, pathStart)) + url;
    }
    if (pathStart == std::string::npos)
    {
        return base + "/" + url;
    }
    return base.substr(0, base.rfind('/') + 1) + url;
}

std::string withoutQuery(const std::string& url)
{
    const size_t mark = url.find_first_of("?#");
    return mark == std::string::npos ? url : url.substr(0, mark);
}

}  // namespace espmgr
