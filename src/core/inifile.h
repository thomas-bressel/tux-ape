#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace tuxape {

// A Windows-style INI file: "[Section]" lines followed by "Key=Value" lines.
// This is the format of WinAPE.ini and of WinAPE's profiles (.wpf), and the
// one TuxAPE keeps its own settings in.
//
// As on Windows, section and key names are matched without regard to case,
// keys may hold spaces, and booleans are the words "true" and "false".
// Sections and keys stay in the order they were first seen, so that a file
// read and written back keeps its shape. Comment lines (";" or "#") and
// anything else that is not a section or a key are dropped.
class IniFile {
public:
    static IniFile parse(std::string_view text);
    // Lines end with CR LF, as WinAPE writes them.
    std::string text() const;

    // False if the file cannot be read; the object is then left empty.
    bool load(const std::filesystem::path& path);
    bool save(const std::filesystem::path& path) const;

    bool has(std::string_view section, std::string_view key) const;
    std::string get(std::string_view section, std::string_view key, std::string_view fallback = {}) const;
    // The fallback is returned when the key is missing or does not hold a
    // value of that kind.
    int getInt(std::string_view section, std::string_view key, int fallback) const;
    bool getBool(std::string_view section, std::string_view key, bool fallback) const;

    void set(std::string_view section, std::string_view key, std::string_view value);
    void setInt(std::string_view section, std::string_view key, int value);
    void setBool(std::string_view section, std::string_view key, bool value);
    void remove(std::string_view section, std::string_view key);

    std::vector<std::string> sections() const;
    std::vector<std::string> keys(std::string_view section) const;

private:
    struct Entry {
        std::string key;
        std::string value;
    };
    struct Section {
        std::string name;
        std::vector<Entry> entries;
    };
    std::vector<Section> sections_;

    const Section* find(std::string_view section) const;
    const Entry* find(std::string_view section, std::string_view key) const;
};

}  // namespace tuxape
