#pragma once

#include <filesystem>
#include <string>

#include <array>
#include <string_view>
#include <vector>

#include "core/memory.h"

namespace tuxape {

class Cpc;

enum class CpcModel { Cpc464, Cpc664, Cpc6128, Plus464, Plus6128 };

// What is fitted to the machine: the choices of WinAPE's Memory settings.
struct MachineConfig {
    RamExpansion ram = RamExpansion::Internal;
    bool siliconDisc = false;
    // ROM images, by name as in WinAPE.ini. A name without a folder is
    // looked for in the ROM folder, with or without its ".ROM" and whatever
    // the case of its letters; anything else is the path of a file. Empty:
    // nothing in that place.
    std::string lowerRom;
    std::array<std::string, Memory::kRomSlots> upperRoms;
    bool rom32 = false;           // a ROM board of 32 slots, not 16
    bool disableAllRoms = false;  // fit none of the ROMs named above
    bool onlyLower0And7 = false;  // fit only the firmware, slot 0 and slot 7
    // A Plus machine, which takes both of WinAPE's switches, "Enable Plus
    // Features" and "Enable Cartridge": the cartridge (a CPR file, looked
    // for like a ROM image, or a path) is its firmware, and the ASIC's
    // features are there. The ROMs named above are then those of an
    // expansion board.
    std::string cartridge;
    bool cartridgeEnabled = false;
    bool plus = false;
    bool isPlus() const { return plus && cartridgeEnabled && !cartridge.empty(); }

    bool operator==(const MachineConfig&) const = default;
};

// The machines of WinAPE's profiles "CPC464", "CPC664" (as its help
// describes it) and "CPC6128".
MachineConfig stockMachine(CpcModel model);
// The model a configuration comes closest to, going by its firmware ROM.
CpcModel modelOf(const MachineConfig& config);

// Folder holding the ROM images: $TUXAPE_ROM_DIR if set, otherwise the ROM
// folder of the WinAPE distribution next to the sources.
std::filesystem::path defaultRomDir();
// Folder holding the profiles (.wpf): $TUXAPE_PROFILE_DIR if set, otherwise
// the "Profile" folder beside the ROM folder, as in WinAPE.
std::filesystem::path defaultProfileDir();

// The ROM images of a folder, as WinAPE lists them: file names without
// their ".ROM", in alphabetical order.
std::vector<std::string> romNames(const std::filesystem::path& romDir);
// The file a ROM name stands for; an empty path if there is none.
std::filesystem::path findRom(std::string_view name, const std::filesystem::path& romDir);

// Fits the RAM and the ROM images. The machine is not reset: as in WinAPE,
// the firmware only finds out about new ROMs at the next reset. A ROM image
// that cannot be read leaves its place empty; the function then returns
// false, with the names of all such images in `error`.
bool applyMachine(Cpc& cpc, const MachineConfig& config, const std::filesystem::path& romDir, std::string* error);

// Fits a stock machine, then cold-resets it. ROM files use WinAPE's names
// (OS6128.ROM, BASIC1-1.ROM, AMSDOS.ROM...).
// Returns false and fills `error` if a ROM image cannot be read.
bool setupStockMachine(Cpc& cpc, CpcModel model, const std::filesystem::path& romDir, std::string* error);

}  // namespace tuxape
