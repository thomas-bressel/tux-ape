#include "core/csl.h"


#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <sstream>

#include "core/autotype.h"
#include "core/cpc.h"
#include "core/files.h"
#include "core/setup.h"
#include "core/snapshot.h"

namespace tuxape {

namespace {

constexpr int kMaxNesting = 8;

std::string lower(std::string text)
{
    for (char& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

// Scripts written in a word processor come with typographic quotes.
std::string plainQuotes(std::string line)
{
    for (const char* fancy : {"\xE2\x80\x98", "\xE2\x80\x99"}) {
        for (size_t pos; (pos = line.find(fancy)) != std::string::npos;)
            line.replace(pos, 3, "'");
    }
    for (const char* fancy : {"\xE2\x80\x9C", "\xE2\x80\x9D"}) {
        for (size_t pos; (pos = line.find(fancy)) != std::string::npos;)
            line.replace(pos, 3, "\"");
    }
    return line;
}

// Splits a line into the instruction and its arguments. A semicolon starts
// a comment. A quoted argument runs from its opening quote to the last
// quote of the same kind on the line, so it may itself contain quotes of
// the other kind and semicolons.
void parseLine(const std::string& raw, std::string& command, std::vector<std::string>& args)
{
    command.clear();
    args.clear();
    const std::string line = plainQuotes(raw);
    size_t i = 0;
    const size_t n = line.size();
    auto skipBlanks = [&] {
        while (i < n && (std::isspace(static_cast<unsigned char>(line[i])) || line[i] == ','))
            ++i;
    };

    skipBlanks();
    while (i < n && !std::isspace(static_cast<unsigned char>(line[i])) && line[i] != ';')
        command += line[i++];
    command = lower(command);

    for (;;) {
        skipBlanks();
        if (i >= n || line[i] == ';')
            return;
        if (line[i] == '\'' || line[i] == '"') {
            const char quote = line[i];
            // The closing quote is the last one before any trailing comment.
            size_t end = line.find_last_of(quote);
            const size_t comment = line.find(';', i);
            if (comment != std::string::npos) {
                const size_t before = line.find_last_of(quote, comment);
                if (before != std::string::npos && before > i)
                    end = before;
            }
            if (end == std::string::npos || end <= i)
                end = n;
            args.push_back(line.substr(i + 1, end - i - 1));
            i = end < n ? end + 1 : n;
            continue;
        }
        std::string word;
        while (i < n && !std::isspace(static_cast<unsigned char>(line[i])) && line[i] != ',' && line[i] != ';')
            word += line[i++];
        args.push_back(word);
    }
}

bool parseNumber(const std::string& text, long& value)
{
    if (text.empty())
        return false;
    char* end = nullptr;
    value = std::strtol(text.c_str(), &end, 0);
    return *end == '\0';
}

struct NamedKey {
    const char* name;
    CpcKey key;
};

constexpr NamedKey kNamedKeys[] = {
    {"ESC", CpcKey::Escape},   {"TAB", CpcKey::Tab},        {"CAP", CpcKey::CapsLock},
    {"SHI", CpcKey::Shift},    {"CTR", CpcKey::Control},    {"COP", CpcKey::Copy},
    {"CLR", CpcKey::Clr},      {"DEL", CpcKey::Del},        {"RET", CpcKey::Return},
    {"ENT", CpcKey::Enter},    {"ARL", CpcKey::CursorLeft}, {"ARR", CpcKey::CursorRight},
    {"ARU", CpcKey::CursorUp}, {"ARD", CpcKey::CursorDown},
    {"FN0", CpcKey::F0}, {"FN1", CpcKey::F1}, {"FN2", CpcKey::F2}, {"FN3", CpcKey::F3}, {"FN4", CpcKey::F4},
    {"FN5", CpcKey::F5}, {"FN6", CpcKey::F6}, {"FN7", CpcKey::F7}, {"FN8", CpcKey::F8}, {"FN9", CpcKey::F9},
};

}  // namespace

CslRunner::CslRunner(Cpc& cpc)
    : cpc_(cpc)
{
}

CslRunner::~CslRunner()
{
    cpc_.setSsmHook(nullptr);
}

bool CslRunner::fail(const std::string& message)
{
    if (error_.empty())
        error_ = message;
    return false;
}

std::filesystem::path CslRunner::find(const std::filesystem::path& dir, const std::string& name) const
{
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path direct = dir / name;
    if (fs::is_regular_file(direct, ec))
        return direct;
    // Scripts written on Windows do not care about letter case.
    const std::string wanted = lower(fs::path(name).filename().string());
    for (const auto& entry : fs::directory_iterator(dir.empty() ? fs::path(".") : dir, ec)) {
        if (lower(entry.path().filename().string()) == wanted)
            return entry.path();
    }
    return {};
}

bool CslRunner::run(const std::filesystem::path& script)
{
    if (depth_ >= kMaxNesting)
        return fail("scripts nested too deeply at " + script.string());
    const auto file = readFile(script);
    if (!file)
        return fail("cannot read script " + script.string());

    const std::filesystem::path outerDir = scriptDir_;
    scriptDir_ = script.parent_path();
    if (depth_++ == 0)
        cpc_.setSsmHook([this](uint16_t code) { onSsm(code); });

    std::istringstream text(std::string(file->begin(), file->end()));
    std::string line, command;
    std::vector<std::string> args;
    int lineNumber = 0;
    bool ok = true;
    while (ok && std::getline(text, line)) {
        ++lineNumber;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        parseLine(line, command, args);
        if (command.empty())
            continue;
        if (!execute(command, args)) {
            // Say where, unless a nested script already has.
            if (error_.find(" line ") == std::string::npos)
                error_ = script.filename().string() + " line " + std::to_string(lineNumber) + " (" + command
                         + "): " + error_;
            ok = false;
        }
    }

    if (--depth_ == 0)
        cpc_.setSsmHook(nullptr);
    scriptDir_ = outerDir;
    return ok;
}

bool CslRunner::execute(const std::string& command, const std::vector<std::string>& args)
{
    auto arg = [&](size_t n) -> const std::string& {
        static const std::string empty;
        return n < args.size() ? args[n] : empty;
    };
    long number = 0;

    if (command == "csl_version" || command == "screenshot_dir" || command == "gate_array")
        return true;  // nothing to do: one format, one sink, one Gate Array

    if (command == "reset") {
        cpc_.keyboard().releaseAll();
        if (lower(arg(0)) == "soft")
            cpc_.reset();
        else
            cpc_.coldReset();
        return true;
    }
    if (command == "crtc_select") {
        const std::string type = lower(arg(0));
        if (type.empty() || type[0] < '0' || type[0] > '4')
            return fail("unknown CRTC type '" + arg(0) + "'");
        cpc_.crtc().setType(static_cast<CrtcType>(type[0] - '0'));
        return true;
    }
    if (command == "cpc_model") {
        if (!parseNumber(arg(0), number) || number < 0 || number > 2)
            return fail("machine model '" + arg(0) + "' is not supported");
        std::string error;
        const CpcModel model = number == 0 ? CpcModel::Cpc464 : number == 1 ? CpcModel::Cpc664 : CpcModel::Cpc6128;
        if (!setupStockMachine(cpc_, model, romDir_.empty() ? defaultRomDir() : romDir_, &error))
            return fail(error);
        return true;
    }
    if (command == "memory_exp") {
        if (!parseNumber(arg(0), number) || number < 0 || number > 4)
            return fail("unknown memory expansion '" + arg(0) + "'");
        Memory& memory = cpc_.memory();
        switch (number) {
        case 0: memory.setRam(RamExpansion::Internal, false); break;
        case 1: memory.setRam(RamExpansion::Dk256, false); break;
        case 2: memory.setRam(RamExpansion::None, true); break;
        case 3: memory.setRam(RamExpansion::Yarek4M, false); break;
        default: memory.setRam(RamExpansion::Dk256, true); break;
        }
        return true;
    }
    if (command == "rom_dir") {
        romDir_ = arg(0);
        return true;
    }
    if (command == "rom_config") {
        const std::string type = lower(arg(0));
        if (!parseNumber(arg(1), number) || (type != "u" && type != "l"))
            return fail("ROM type '" + arg(0) + "' is not supported");
        const auto path = find(romDir_.empty() ? defaultRomDir() : romDir_, arg(2));
        const auto image = path.empty() ? std::nullopt : readFile(path);
        if (!image)
            return fail("cannot read ROM image '" + arg(2) + "'");
        if (type == "l")
            cpc_.memory().setLowerRom(*image);
        else
            cpc_.memory().setUpperRom(static_cast<int>(number), *image);
        return true;
    }

    if (command == "disk_dir") {
        discDir_ = arg(0);
        return true;
    }
    if (command == "disk_insert") {
        if (args.size() >= 2)
            return insertDisc(lower(arg(0)) == "b" ? 1 : 0, arg(1));
        return insertDisc(0, arg(0));
    }

    if (command == "key_delay") {
        long press = 0, gap = 0, afterReturn = 0;
        if (!parseNumber(arg(0), press) || !parseNumber(arg(1), gap))
            return fail("key_delay needs two durations");
        if (!parseNumber(arg(2), afterReturn))
            afterReturn = gap;
        keyPress_ = static_cast<uint32_t>(press);
        keyGap_ = static_cast<uint32_t>(gap);
        keyGapAfterReturn_ = static_cast<uint32_t>(afterReturn);
        return true;
    }
    if (command == "key_output")
        return typeKeys(arg(0));
    if (command == "key_from_file") {
        const auto path = find(scriptDir_, arg(0));
        const auto file = path.empty() ? std::nullopt : readFile(path);
        if (!file)
            return fail("cannot read '" + arg(0) + "'");
        // The file is plain text: no escapes, line ends are RETURN.
        for (uint8_t byte : *file) {
            const char c = static_cast<char>(byte);
            if (c == '\r')
                continue;
            if (!typeKeys(c == '\n' ? std::string_view("\\(RET)") : std::string_view(&c, 1)))
                return false;
        }
        return true;
    }
    if (command == "keyboard_write") {
        if (args.size() != 10)
            return fail("keyboard_write needs ten values");
        for (int line = 0; line < 10; ++line) {
            if (!parseNumber(args[static_cast<size_t>(line)], number))
                return fail("bad keyboard value '" + args[static_cast<size_t>(line)] + "'");
            for (int bit = 0; bit < 8; ++bit)
                cpc_.keyboard().set(static_cast<CpcKey>(line * 8 + bit), !(number & (1 << bit)));
        }
        return true;
    }

    if (command == "wait") {
        if (!parseNumber(arg(0), number) || number < 0)
            return fail("bad duration '" + arg(0) + "'");
        cpc_.run(static_cast<uint32_t>(number));
        return true;
    }
    if (command == "wait_vsyncoffon") {
        const uint64_t limit = cpc_.microseconds() + static_cast<uint64_t>(eventTimeout_) * 1000000;
        // If VSYNC is on, let it finish first.
        for (const bool wanted : {false, true}) {
            while (cpc_.crtc().vsync() != wanted) {
                if (cpc_.microseconds() > limit)
                    return fail("no vertical sync");
                cpc_.run(1);
            }
        }
        return true;
    }
    if (command == "wait_driveonoff") {
        if (!parseNumber(arg(0), number))
            number = 1;
        const uint64_t limit = cpc_.microseconds() + static_cast<uint64_t>(eventTimeout_) * 1000000;
        for (long cycle = 0; cycle < number; ++cycle) {
            for (const bool wanted : {true, false}) {
                while (cpc_.fdc().motor() != wanted) {
                    if (cpc_.microseconds() > limit)
                        return fail("the drive motor did not go on and off");
                    cpc_.run(1000);
                }
            }
        }
        return true;
    }
    if (command == "wait_ssm0000")
        return waitForSsm(0x0000);
    if (command == "wait_ssm") {
        if (!parseNumber(arg(0), number))
            return fail("bad SSM code '" + arg(0) + "'");
        return waitForSsm(static_cast<int>(number));
    }

    if (command == "screenshot_name") {
        screenshotName_ = arg(0);
        return true;
    }
    if (command == "screenshot") {
        if (lower(arg(0)) == "vsync" && !execute("wait_vsyncoffon", {}))
            return false;
        return takeScreenshot(screenshotName_);
    }

    if (command == "csl_load") {
        std::string name = arg(0);
        std::filesystem::path path = find(scriptDir_, name);
        if (path.empty())
            path = find(scriptDir_, name + ".csl");
        if (path.empty())
            return fail("cannot find script '" + name + "'");
        return run(path);
    }

    if (command == "snapshot_dir") {
        snapshotDir_ = arg(0);
        return true;
    }
    if (command == "snapshot_load")
        return loadSnapshotFile(arg(0));
    if (command == "snapshot_name") {
        snapshotName_ = arg(0);
        return true;
    }
    if (command == "snapshot") {
        if (lower(arg(0)) == "vsync" && !execute("wait_vsyncoffon", {}))
            return false;
        return takeSnapshot();
    }

    if (command.rfind("tape_", 0) == 0)
        return fail("not supported yet");
    return fail("unknown instruction");
}

bool CslRunner::insertDisc(int drive, const std::string& name)
{
    std::filesystem::path path = find(discDir_.empty() ? scriptDir_ : discDir_, name);
    if (path.empty() && !discDir_.empty())
        path = find(scriptDir_, name);
    if (path.empty())
        path = fallbackDisc_;
    const auto file = path.empty() ? std::nullopt : readFile(path);
    auto disc = file ? Disc::fromDsk(*file) : std::nullopt;
    if (!disc)
        return fail("cannot read disc image '" + name + "'");
    cpc_.fdc().drive(drive).disc = std::make_unique<Disc>(std::move(*disc));
    return true;
}

// Types the characters of a key_output string. Besides plain characters it
// understands \(XXX) for the keys that have no character, {...} for keys
// pressed together, and \(KOF) to chain the next key without a pause.
bool CslRunner::typeKeys(std::string_view text)
{
    Keyboard& keyboard = cpc_.keyboard();
    size_t i = 0;

    // Reads one key at position i into `keys`. Returns false at a \(KOF).
    auto readKey = [&](std::vector<CpcKey>& keys, bool& isReturn) {
        if (text[i] == '\\' && i + 1 < text.size() && text[i + 1] == '(') {
            const size_t close = text.find(')', i + 3);
            const std::string_view name = text.substr(i + 2, close == std::string_view::npos ? 0 : close - i - 2);
            i = close == std::string_view::npos ? text.size() : close + 1;
            if (name == "KOF")
                return false;
            for (const NamedKey& named : kNamedKeys) {
                if (name == named.name) {
                    keys.push_back(named.key);
                    isReturn = isReturn || named.key == CpcKey::Return;
                    return true;
                }
            }
            // \({) and the like stand for the character itself.
            CpcKey key;
            bool shift;
            if (name.size() == 1 && cpcKeyForChar(name[0], key, shift)) {
                keys.push_back(key);
                if (shift)
                    keys.push_back(CpcKey::Shift);
            }
            return true;
        }
        CpcKey key;
        bool shift;
        if (cpcKeyForChar(text[i], key, shift)) {
            keys.push_back(key);
            if (shift)
                keys.push_back(CpcKey::Shift);
        }
        ++i;
        return true;
    };

    while (i < text.size()) {
        std::vector<CpcKey> keys;
        bool isReturn = false;
        if (text[i] == '{') {
            ++i;
            while (i < text.size() && text[i] != '}')
                readKey(keys, isReturn);
            if (i < text.size())
                ++i;
        } else if (!readKey(keys, isReturn)) {
            continue;  // a stray \(KOF)
        }
        if (keys.empty())
            continue;  // a character the keyboard does not have

        for (CpcKey key : keys)
            keyboard.set(key, true);
        cpc_.run(keyPress_);
        for (CpcKey key : keys)
            keyboard.set(key, false);

        const bool chained = text.substr(i, 6) == "\\(KOF)";
        if (chained)
            i += 6;
        else
            cpc_.run(isReturn ? keyGapAfterReturn_ : keyGap_);
    }
    return true;
}

bool CslRunner::waitForSsm(int code)
{
    const uint64_t seen = ssmCount_;
    const uint64_t limit = cpc_.microseconds() + static_cast<uint64_t>(eventTimeout_) * 1000000;
    for (;;) {
        cpc_.run(1000);
        if (ssmCount_ != seen && lastSsm_ == code)
            return true;
        if (cpc_.microseconds() > limit) {
            char text[64];
            std::snprintf(text, sizeof text, "the program never sent SSM code %04X", static_cast<unsigned>(code));
            return fail(text);
        }
    }
}

void CslRunner::onSsm(uint16_t code)
{
    lastSsm_ = code;
    ++ssmCount_;
    if (code == 0xFFFE) {
        takeScreenshot(screenshotName_);
    } else if (code == 0xFFFF) {
        takeSnapshot();
    } else if (code != 0x0000 && code < 0xFF00) {
        // Any ordinary code asks for a picture named after it.
        char name[96];
        std::snprintf(name, sizeof name, "%s_%d_%04X", emulatorName_.c_str(), static_cast<int>(cpc_.crtc().type()),
                      static_cast<unsigned>(code));
        takeScreenshot(name);
    }
}

bool CslRunner::takeSnapshot()
{
    char generated[96];
    std::snprintf(generated, sizeof generated, "%s_%d_FFFF", emulatorName_.c_str(),
                  static_cast<int>(cpc_.crtc().type()));
    const std::string name = snapshotName_.empty() ? generated : snapshotName_;
    ++snapshotCount_;
    if (snapshot_ && !snapshot_(name, saveSnapshot(cpc_)))
        return fail("cannot save snapshot '" + name + "'");
    return true;
}

bool CslRunner::loadSnapshotFile(const std::string& name)
{
    std::filesystem::path path;
    for (const std::string& candidate : {name, name + ".sna"}) {
        if (path.empty() && !snapshotDir_.empty())
            path = find(snapshotDir_, candidate);
        if (path.empty())
            path = find(scriptDir_, candidate);
    }
    const auto file = path.empty() ? std::nullopt : readFile(path);
    std::string why = "cannot read the file";
    if (!file || !loadSnapshot(cpc_, *file, &why))
        return fail("snapshot '" + name + "': " + why);
    return true;
}

bool CslRunner::takeScreenshot(const std::string& name)
{
    std::string actual = name;
    if (actual.empty()) {
        char generated[96];
        std::snprintf(generated, sizeof generated, "%s_%d_FFFE", emulatorName_.c_str(),
                      static_cast<int>(cpc_.crtc().type()));
        actual = generated;
    }
    ++screenshotCount_;
    if (screenshot_ && !screenshot_(actual))
        return fail("cannot save picture '" + actual + "'");
    return true;
}

}  // namespace tuxape
