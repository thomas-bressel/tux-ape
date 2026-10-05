#include "core/inifile.h"

#include <cctype>
#include <charconv>

#include "core/files.h"

namespace tuxape {

namespace {

bool sameName(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

std::string_view trimmed(std::string_view s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    return s;
}

}  // namespace

IniFile IniFile::parse(std::string_view text)
{
    IniFile ini;
    std::string section;
    while (!text.empty()) {
        const size_t end = text.find('\n');
        const std::string_view line = trimmed(text.substr(0, end));
        text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);

        if (line.empty() || line.front() == ';' || line.front() == '#')
            continue;
        if (line.front() == '[') {
            const size_t close = line.find(']');
            if (close != std::string_view::npos)
                section = std::string(trimmed(line.substr(1, close - 1)));
            continue;
        }
        const size_t equals = line.find('=');
        if (equals == std::string_view::npos || equals == 0)
            continue;
        // As on Windows, the first of two keys of the same name is the one
        // that counts.
        const std::string_view key = trimmed(line.substr(0, equals));
        if (!ini.has(section, key))
            ini.set(section, key, trimmed(line.substr(equals + 1)));
    }
    return ini;
}

std::string IniFile::text() const
{
    std::string out;
    for (const Section& section : sections_) {
        if (!out.empty())
            out += "\r\n";
        out += '[' + section.name + "]\r\n";
        for (const Entry& entry : section.entries)
            out += entry.key + '=' + entry.value + "\r\n";
    }
    return out;
}

bool IniFile::load(const std::filesystem::path& path)
{
    sections_.clear();
    const auto data = readFile(path);
    if (!data)
        return false;
    *this = parse(std::string_view(reinterpret_cast<const char*>(data->data()), data->size()));
    return true;
}

bool IniFile::save(const std::filesystem::path& path) const
{
    const std::string out = text();
    return writeFile(path, {reinterpret_cast<const uint8_t*>(out.data()), out.size()});
}

const IniFile::Section* IniFile::find(std::string_view section) const
{
    for (const Section& candidate : sections_)
        if (sameName(candidate.name, section))
            return &candidate;
    return nullptr;
}

const IniFile::Entry* IniFile::find(std::string_view section, std::string_view key) const
{
    if (const Section* found = find(section))
        for (const Entry& entry : found->entries)
            if (sameName(entry.key, key))
                return &entry;
    return nullptr;
}

bool IniFile::has(std::string_view section, std::string_view key) const
{
    return find(section, key) != nullptr;
}

std::string IniFile::get(std::string_view section, std::string_view key, std::string_view fallback) const
{
    const Entry* entry = find(section, key);
    return std::string(entry ? std::string_view(entry->value) : fallback);
}

int IniFile::getInt(std::string_view section, std::string_view key, int fallback) const
{
    const Entry* entry = find(section, key);
    if (!entry)
        return fallback;
    int value = 0;
    const char* first = entry->value.data();
    const char* last = first + entry->value.size();
    const auto result = std::from_chars(first, last, value);
    return result.ec == std::errc() && result.ptr == last ? value : fallback;
}

bool IniFile::getBool(std::string_view section, std::string_view key, bool fallback) const
{
    const Entry* entry = find(section, key);
    if (!entry)
        return fallback;
    if (sameName(entry->value, "true") || entry->value == "1")
        return true;
    if (sameName(entry->value, "false") || entry->value == "0")
        return false;
    return fallback;
}

void IniFile::set(std::string_view section, std::string_view key, std::string_view value)
{
    Section* target = nullptr;
    for (Section& candidate : sections_)
        if (sameName(candidate.name, section))
            target = &candidate;
    if (!target) {
        sections_.push_back({std::string(section), {}});
        target = &sections_.back();
    }
    for (Entry& entry : target->entries)
        if (sameName(entry.key, key)) {
            entry.value = std::string(value);
            return;
        }
    target->entries.push_back({std::string(key), std::string(value)});
}

void IniFile::setInt(std::string_view section, std::string_view key, int value)
{
    set(section, key, std::to_string(value));
}

void IniFile::setBool(std::string_view section, std::string_view key, bool value)
{
    set(section, key, value ? "true" : "false");
}

void IniFile::remove(std::string_view section, std::string_view key)
{
    for (Section& candidate : sections_)
        if (sameName(candidate.name, section))
            std::erase_if(candidate.entries, [&](const Entry& entry) { return sameName(entry.key, key); });
}

std::vector<std::string> IniFile::sections() const
{
    std::vector<std::string> names;
    for (const Section& section : sections_)
        names.push_back(section.name);
    return names;
}

std::vector<std::string> IniFile::keys(std::string_view section) const
{
    std::vector<std::string> names;
    if (const Section* found = find(section))
        for (const Entry& entry : found->entries)
            names.push_back(entry.key);
    return names;
}

}  // namespace tuxape
