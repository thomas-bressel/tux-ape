#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace tuxape {

// Whole-file helpers. They report failure through the return value; the
// caller knows the file name and can word the message.
std::optional<std::vector<uint8_t>> readFile(const std::filesystem::path& path);
bool writeFile(const std::filesystem::path& path, std::span<const uint8_t> data);

// Writes a picture as an uncompressed 24-bit PNG. Pixels are 0xAARRGGBB;
// alpha is dropped.
bool writePng(const std::filesystem::path& path, const uint32_t* pixels, int width, int height);

}  // namespace tuxape
