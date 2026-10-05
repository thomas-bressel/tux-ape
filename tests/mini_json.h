#pragma once

#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Just enough JSON to read test vectors: no escapes beyond \" and \\, and
// numbers are integers.
struct Json {
    enum Kind { Null, Number, String, Array, Object } kind = Null;
    int64_t number = 0;
    std::string string;
    std::vector<Json> items;
    std::vector<std::pair<std::string, Json>> members;

    bool isNull() const { return kind == Null; }
    size_t size() const { return items.size(); }
    const Json& operator[](size_t i) const { return items[i]; }

    const Json* find(std::string_view key) const
    {
        for (const auto& [k, v] : members)
            if (k == key)
                return &v;
        return nullptr;
    }

    const Json& at(std::string_view key) const
    {
        if (const Json* v = find(key))
            return *v;
        throw std::runtime_error("missing key " + std::string(key));
    }

    static Json parse(std::string_view text)
    {
        size_t pos = 0;
        return parseValue(text, pos);
    }

private:
    static void skipSpace(std::string_view t, size_t& p)
    {
        while (p < t.size() && (t[p] == ' ' || t[p] == '\n' || t[p] == '\r' || t[p] == '\t'))
            ++p;
    }

    static std::string parseString(std::string_view t, size_t& p)
    {
        std::string out;
        ++p;  // opening quote
        while (t[p] != '"') {
            if (t[p] == '\\')
                ++p;
            out += t[p++];
        }
        ++p;
        return out;
    }

    static Json parseValue(std::string_view t, size_t& p)
    {
        skipSpace(t, p);
        Json v;
        const char c = t[p];
        if (c == '{') {
            v.kind = Object;
            ++p;
            skipSpace(t, p);
            while (t[p] != '}') {
                skipSpace(t, p);
                std::string key = parseString(t, p);
                skipSpace(t, p);
                ++p;  // colon
                v.members.emplace_back(std::move(key), parseValue(t, p));
                skipSpace(t, p);
                if (t[p] == ',')
                    ++p;
            }
            ++p;
        } else if (c == '[') {
            v.kind = Array;
            ++p;
            skipSpace(t, p);
            while (t[p] != ']') {
                v.items.push_back(parseValue(t, p));
                skipSpace(t, p);
                if (t[p] == ',')
                    ++p;
                skipSpace(t, p);
            }
            ++p;
        } else if (c == '"') {
            v.kind = String;
            v.string = parseString(t, p);
        } else if (c == 'n') {
            p += 4;
        } else {
            v.kind = Number;
            char* end = nullptr;
            v.number = std::strtoll(t.data() + p, &end, 10);
            p = static_cast<size_t>(end - t.data());
        }
        return v;
    }
};
