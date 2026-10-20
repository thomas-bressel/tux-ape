#pragma once

// A .zip archive written for the tests: its files stored as they are.

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

#include <QByteArray>
#include <QFile>
#include <QString>

namespace testzip {

inline uint32_t crc32(const QByteArray& data)
{
    static const auto table = [] {
        std::array<uint32_t, 256> made{};
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int bit = 0; bit < 8; ++bit)
                c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            made[n] = c;
        }
        return made;
    }();
    uint32_t crc = 0xFFFFFFFFu;
    for (const char byte : data)
        crc = table[(crc ^ static_cast<uint8_t>(byte)) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

inline void put(QByteArray& out, uint32_t value, int bytes)
{
    for (int i = 0; i < bytes; ++i)
        out.append(static_cast<char>(value >> (8 * i)));
}

// A .zip archive of files stored as they are.
inline bool writeZip(const QString& path, const std::vector<std::pair<QString, QByteArray>>& files)
{
    QByteArray body, directory;
    for (const auto& [name, data] : files) {
        const QByteArray label = name.toUtf8();
        const auto offset = static_cast<uint32_t>(body.size());
        const auto size = static_cast<uint32_t>(data.size());
        const auto fields = [&](QByteArray& out) {
            put(out, 20, 2);  // version needed
            put(out, 0, 2);   // flags
            put(out, 0, 2);   // stored
            put(out, 0, 4);   // time and date
            put(out, crc32(data), 4);
            put(out, size, 4);
            put(out, size, 4);
            put(out, static_cast<uint32_t>(label.size()), 2);
            put(out, 0, 2);   // no extra field
        };
        put(body, 0x04034B50, 4);
        fields(body);
        body += label + data;
        put(directory, 0x02014B50, 4);
        put(directory, 20, 2);  // made by
        fields(directory);
        put(directory, 0, 2);   // no comment
        put(directory, 0, 2);   // disc
        put(directory, 0, 2);   // attributes
        put(directory, 0, 4);
        put(directory, offset, 4);
        directory += label;
    }
    QByteArray end;
    put(end, 0x06054B50, 4);
    put(end, 0, 4);
    put(end, static_cast<uint32_t>(files.size()), 2);
    put(end, static_cast<uint32_t>(files.size()), 2);
    put(end, static_cast<uint32_t>(directory.size()), 4);
    put(end, static_cast<uint32_t>(body.size()), 4);
    put(end, 0, 2);
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(body + directory + end) == body.size() + directory.size() + end.size();
}

}  // namespace testzip
