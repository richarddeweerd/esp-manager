#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace espmgr
{

// Writes JSON for the web pages' requests, without a JSON library. Calls are chained:
//
//   JsonWriter json;
//   json.beginObject().key("ssid").string(ssid).key("rssi").number(-60).endObject();
//
// It trusts the caller to nest correctly; keys and values in an object must alternate.
class JsonWriter
{
public:
    JsonWriter& beginObject();
    JsonWriter& endObject();
    JsonWriter& beginArray();
    JsonWriter& endArray();

    JsonWriter& key(std::string_view name);
    // Escapes quotes, backslashes and control characters; other bytes, such as UTF-8 in an SSID,
    // are copied as they are.
    JsonWriter& string(std::string_view value);
    JsonWriter& number(int64_t value);
    JsonWriter& boolean(bool value);

    const std::string& str() const
    {
        return out_;
    }

private:
    // Writes the comma before a value or key that follows another in the same object or array.
    void beforeValue();
    void quoted(std::string_view text);

    std::string out_;
    // Per open object or array: whether something has been written into it yet.
    std::vector<bool> hasItems_;
    bool afterKey_ = false;
};

}  // namespace espmgr
