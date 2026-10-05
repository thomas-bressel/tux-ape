#include "core/setup.h"

#include <cstdlib>

#include "core/cpc.h"
#include "core/files.h"

namespace tuxape {

std::filesystem::path defaultRomDir()
{
    if (const char* dir = std::getenv("TUXAPE_ROM_DIR"); dir && *dir)
        return dir;
    return TUXAPE_DEV_ROM_DIR;
}

bool setupStockMachine(Cpc& cpc, CpcModel model, const std::filesystem::path& romDir, std::string* error)
{
    struct Stock {
        const char* os;
        const char* basic;
        const char* dos;  // null on a machine without a disc drive
        RamExpansion ram;
    };
    Stock stock{};
    switch (model) {
    case CpcModel::Cpc464: stock = {"OS464.ROM", "BASIC1-0.ROM", nullptr, RamExpansion::None}; break;
    case CpcModel::Cpc664: stock = {"OS664.ROM", "BASIC664.ROM", "AMSDOS.ROM", RamExpansion::None}; break;
    case CpcModel::Cpc6128: stock = {"OS6128.ROM", "BASIC1-1.ROM", "AMSDOS.ROM", RamExpansion::Internal}; break;
    }

    auto load = [&](const char* name) -> std::optional<std::vector<uint8_t>> {
        auto image = readFile(romDir / name);
        if (!image && error)
            *error = "cannot read ROM image " + (romDir / name).string();
        return image;
    };

    const auto os = load(stock.os);
    const auto basic = load(stock.basic);
    if (!os || !basic)
        return false;
    std::optional<std::vector<uint8_t>> dos;
    if (stock.dos && !(dos = load(stock.dos)))
        return false;

    Memory& memory = cpc.memory();
    memory.setRam(stock.ram, false);
    memory.setLowerRom(*os);
    for (int slot = 0; slot < Memory::kRomSlots; ++slot)
        memory.setUpperRom(slot, {});
    memory.setUpperRom(0, *basic);
    if (dos)
        memory.setUpperRom(7, *dos);
    cpc.coldReset();
    return true;
}

}  // namespace tuxape
