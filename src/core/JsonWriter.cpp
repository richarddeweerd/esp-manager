#include "core/JsonWriter.h"

#include <cstdio>

namespace espmgr
{

void JsonWriter::beforeValue()
{
    if (afterKey_)
    {
        afterKey_ = false;
        return;
    }
    if (!hasItems_.empty())
    {
        if (hasItems_.back())
        {
            out_ += ',';
        }
        hasItems_.back() = true;
    }
}

JsonWriter& JsonWriter::beginObject()
{
    beforeValue();
    out_ += '{';
    hasItems_.push_back(false);
    return *this;
}

JsonWriter& JsonWriter::endObject()
{
    out_ += '}';
    hasItems_.pop_back();
    return *this;
}

JsonWriter& JsonWriter::beginArray()
{
    beforeValue();
    out_ += '[';
    hasItems_.push_back(false);
    return *this;
}

JsonWriter& JsonWriter::endArray()
{
    out_ += ']';
    hasItems_.pop_back();
    return *this;
}

JsonWriter& JsonWriter::key(std::string_view name)
{
    beforeValue();
    quoted(name);
    out_ += ':';
    afterKey_ = true;
    return *this;
}

JsonWriter& JsonWriter::string(std::string_view value)
{
    beforeValue();
    quoted(value);
    return *this;
}

JsonWriter& JsonWriter::number(int64_t value)
{
    beforeValue();
    out_ += std::to_string(value);
    return *this;
}

JsonWriter& JsonWriter::boolean(bool value)
{
    beforeValue();
    out_ += value ? "true" : "false";
    return *this;
}

void JsonWriter::quoted(std::string_view text)
{
    out_ += '"';
    for (const char c : text)
    {
        switch (c)
        {
        case '"':
            out_ += "\\\"";
            break;
        case '\\':
            out_ += "\\\\";
            break;
        case '\n':
            out_ += "\\n";
            break;
        case '\r':
            out_ += "\\r";
            break;
        case '\t':
            out_ += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20)
            {
                char escaped[7];
                std::snprintf(escaped, sizeof(escaped), "\\u%04x",
                              static_cast<unsigned>(static_cast<unsigned char>(c)));
                out_ += escaped;
            }
            else
            {
                out_ += c;
            }
        }
    }
    out_ += '"';
}

}  // namespace espmgr
