#include "core/setup.h"

#include <algorithm>
#include <cctype>
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

std::filesystem::path defaultProfileDir()
{
    if (const char* dir = std::getenv("TUXAPE_PROFILE_DIR"); dir && *dir)
        return dir;
    return defaultRomDir().parent_path() / "Profile";
}

namespace {

std::string lowered(std::string_view text)
{
    std::string out(text);
    for (char& c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool isRomFile(const std::filesystem::path& path)
{
    return lowered(path.extension().string()) == ".rom";
}

}  // namespace

MachineConfig stockMachine(CpcModel model)
{
    MachineConfig config;
    switch (model) {
    case CpcModel::Cpc464:
        config.ram = RamExpansion::None;
        config.lowerRom = "OS464";
        config.upperRoms[0] = "BASIC1-0";
        break;
    case CpcModel::Cpc664:
        config.ram = RamExpansion::None;
        config.lowerRom = "OS664";
        config.upperRoms[0] = "BASIC664";
        config.upperRoms[7] = "AMSDOS";
        break;
    case CpcModel::Cpc6128:
        config.ram = RamExpansion::Internal;
        config.lowerRom = "OS6128";
        config.upperRoms[0] = "BASIC1-1";
        config.upperRoms[7] = "AMSDOS";
        break;
    }
    return config;
}

CpcModel modelOf(const MachineConfig& config)
{
    const std::string firmware = lowered(std::filesystem::path(config.lowerRom).stem().string());
    if (firmware.find("464") != std::string::npos)
        return CpcModel::Cpc464;
    if (firmware.find("664") != std::string::npos)
        return CpcModel::Cpc664;
    return CpcModel::Cpc6128;
}

std::vector<std::string> romNames(const std::filesystem::path& romDir)
{
    std::vector<std::string> names;
    std::error_code ignored;
    for (const auto& entry : std::filesystem::directory_iterator(romDir, ignored))
        if (entry.is_regular_file(ignored) && isRomFile(entry.path()))
            names.push_back(entry.path().stem().string());
    std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
        const std::string la = lowered(a), lb = lowered(b);
        return la != lb ? la < lb : a < b;
    });
    return names;
}

std::filesystem::path findRom(std::string_view name, const std::filesystem::path& romDir)
{
    if (name.empty())
        return {};
    std::error_code ignored;
    const std::filesystem::path given(name);
    if (given.has_parent_path() || name.find('\\') != std::string_view::npos)
        return std::filesystem::is_regular_file(given, ignored) ? given : std::filesystem::path();

    // WinAPE.ini comes from a file system that does not tell upper case
    // from lower: "cpc_plus" there is CPC_PLUS.ROM here.
    const std::string wanted = lowered(name);
    std::filesystem::path found;
    for (const auto& entry : std::filesystem::directory_iterator(romDir, ignored)) {
        if (!entry.is_regular_file(ignored))
            continue;
        const std::filesystem::path& file = entry.path();
        if (lowered(file.filename().string()) == wanted)
            return file;  // the name as given, extension and all
        if (isRomFile(file) && lowered(file.stem().string()) == wanted)
            found = file;
    }
    return found;
}

bool applyMachine(Cpc& cpc, const MachineConfig& config, const std::filesystem::path& romDir, std::string* error)
{
    std::string missing;
    auto load = [&](const std::string& name) -> std::vector<uint8_t> {
        if (name.empty())
            return {};
        const std::filesystem::path file = findRom(name, romDir);
        auto image = file.empty() ? std::nullopt : readFile(file);
        if (!image) {
            if (!missing.empty())
                missing += ", ";
            missing += file.empty() ? name : file.string();
            return {};
        }
        return std::move(*image);
    };

    Memory& memory = cpc.memory();
    if (memory.ramExpansion() != config.ram || memory.siliconDisc() != config.siliconDisc)
        memory.setRam(config.ram, config.siliconDisc);
    memory.setRomSlotLimit(config.rom32 ? 32 : 16);
    memory.setLowerRom(config.disableAllRoms ? std::vector<uint8_t>() : load(config.lowerRom));
    for (int slot = 0; slot < Memory::kRomSlots; ++slot) {
        const bool fitted = !config.disableAllRoms && (config.rom32 || slot < 16)
                            && (!config.onlyLower0And7 || slot == 0 || slot == 7);
        memory.setUpperRom(slot, fitted ? load(config.upperRoms[static_cast<size_t>(slot)]) : std::vector<uint8_t>());
    }

    if (missing.empty())
        return true;
    if (error)
        *error = "cannot read ROM image " + missing;
    return false;
}

bool setupStockMachine(Cpc& cpc, CpcModel model, const std::filesystem::path& romDir, std::string* error)
{
    if (!applyMachine(cpc, stockMachine(model), romDir, error))
        return false;
    cpc.coldReset();
    return true;
}

}  // namespace tuxape
