#pragma once

#include <filesystem>
#include <string>

namespace tuxape {

class Cpc;

enum class CpcModel { Cpc464, Cpc664, Cpc6128 };

// Folder holding the ROM images: $TUXAPE_ROM_DIR if set, otherwise the ROM
// folder of the WinAPE distribution next to the sources.
std::filesystem::path defaultRomDir();

// Fits the RAM and the ROM images of a stock machine, as WinAPE's profiles
// of the same name do, then cold-resets it. ROM files use WinAPE's names
// (OS6128.ROM, BASIC1-1.ROM, AMSDOS.ROM...).
// Returns false and fills `error` if a ROM image cannot be read.
bool setupStockMachine(Cpc& cpc, CpcModel model, const std::filesystem::path& romDir, std::string* error);

}  // namespace tuxape
