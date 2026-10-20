#include "core/autotype.h"

#include <cctype>
#include <cstdlib>
#include <string>

namespace tuxape {

namespace {

// The firmware scans the keyboard once per frame and debounces: a change
// must show in two successive scans, both for a key going down and for it
// coming back up between repeated letters.
constexpr int kDownFrames = 2;
constexpr int kUpFrames = 2;

struct CharKey {
    char c;
    CpcKey key;
    bool shift;
};

// Characters of the UK keyboard other than letters and digits.
constexpr CharKey kPunctuation[] = {
    {' ', CpcKey::Space, false},      {'\n', CpcKey::Return, false},   {'\t', CpcKey::Tab, false},
    {'!', CpcKey::Num1, true},        {'"', CpcKey::Num2, true},       {'#', CpcKey::Num3, true},
    {'$', CpcKey::Num4, true},        {'%', CpcKey::Num5, true},       {'&', CpcKey::Num6, true},
    {'\'', CpcKey::Num7, true},       {'(', CpcKey::Num8, true},       {')', CpcKey::Num9, true},
    {'_', CpcKey::Num0, true},        {'-', CpcKey::Minus, false},     {'=', CpcKey::Minus, true},
    {'^', CpcKey::Caret, false},      {'@', CpcKey::At, false},        {'|', CpcKey::At, true},
    {'[', CpcKey::LeftBracket, false}, {'{', CpcKey::LeftBracket, true},
    {']', CpcKey::RightBracket, false}, {'}', CpcKey::RightBracket, true},
    {';', CpcKey::Semicolon, false},  {'+', CpcKey::Semicolon, true},
    {':', CpcKey::Colon, false},      {'*', CpcKey::Colon, true},
    {',', CpcKey::Comma, false},      {'<', CpcKey::Comma, true},
    {'.', CpcKey::Period, false},     {'>', CpcKey::Period, true},
    {'/', CpcKey::Slash, false},      {'?', CpcKey::Slash, true},
    {'\\', CpcKey::Backslash, false}, {'`', CpcKey::Backslash, true},
};

constexpr CpcKey kLetters[26] = {
    CpcKey::A, CpcKey::B, CpcKey::C, CpcKey::D, CpcKey::E, CpcKey::F, CpcKey::G, CpcKey::H, CpcKey::I,
    CpcKey::J, CpcKey::K, CpcKey::L, CpcKey::M, CpcKey::N, CpcKey::O, CpcKey::P, CpcKey::Q, CpcKey::R,
    CpcKey::S, CpcKey::T, CpcKey::U, CpcKey::V, CpcKey::W, CpcKey::X, CpcKey::Y, CpcKey::Z,
};

constexpr CpcKey kDigits[10] = {
    CpcKey::Num0, CpcKey::Num1, CpcKey::Num2, CpcKey::Num3, CpcKey::Num4,
    CpcKey::Num5, CpcKey::Num6, CpcKey::Num7, CpcKey::Num8, CpcKey::Num9,
};

struct NamedKey {
    const char* name;
    CpcKey key;
};

constexpr NamedKey kNamed[] = {
    {"COPY", CpcKey::Copy},         {"CONTROL", CpcKey::Control},   {"CLR", CpcKey::Clr},
    {"CAPS LOCK", CpcKey::CapsLock}, {"DEL", CpcKey::Del},          {"DECIMAL", CpcKey::FDot},
    {"DOT", CpcKey::FDot},          {"DOWN", CpcKey::CursorDown},   {"ENTER", CpcKey::Enter},
    {"ESC", CpcKey::Escape},        {"FIRE", CpcKey::JoyFire1},     {"FIRE1", CpcKey::JoyFire1},
    {"FIRE2", CpcKey::JoyFire2},    {"FIRE3", CpcKey::JoyFire3},    {"JOY LEFT", CpcKey::JoyLeft},
    {"JOY RIGHT", CpcKey::JoyRight}, {"JOY UP", CpcKey::JoyUp},     {"JOY DOWN", CpcKey::JoyDown},
    {"LEFT", CpcKey::CursorLeft},   {"RIGHT", CpcKey::CursorRight}, {"RETURN", CpcKey::Return},
    {"SHIFT", CpcKey::Shift},       {"SPACE", CpcKey::Space},       {"TAB", CpcKey::Tab},
    {"UP", CpcKey::CursorUp},
    {"F0", CpcKey::F0}, {"F1", CpcKey::F1}, {"F2", CpcKey::F2}, {"F3", CpcKey::F3}, {"F4", CpcKey::F4},
    {"F5", CpcKey::F5}, {"F6", CpcKey::F6}, {"F7", CpcKey::F7}, {"F8", CpcKey::F8}, {"F9", CpcKey::F9},
};

}  // namespace

bool cpcKeyForChar(char c, CpcKey& key, bool& shift)
{
    const unsigned char u = static_cast<unsigned char>(c);
    if (std::isalpha(u) && u < 128) {
        key = kLetters[std::toupper(u) - 'A'];
        shift = std::isupper(u);
        return true;
    }
    if (std::isdigit(u)) {
        key = kDigits[u - '0'];
        shift = false;
        return true;
    }
    for (const CharKey& entry : kPunctuation) {
        if (entry.c == c) {
            key = entry.key;
            shift = entry.shift;
            return true;
        }
    }
    return false;
}

namespace {

// The text's characters: UTF-8 decoded, a byte that is not UTF-8 taken as
// Latin-1.
std::u32string decode(std::string_view text)
{
    std::u32string out;
    for (size_t i = 0; i < text.size();) {
        const auto byte = static_cast<unsigned char>(text[i]);
        const int length = byte < 0x80 ? 1 : (byte & 0xE0) == 0xC0 ? 2 : (byte & 0xF0) == 0xE0 ? 3 : (byte & 0xF8) == 0xF0 ? 4 : 0;
        char32_t c = length == 1 ? byte : length == 2 ? byte & 0x1F : length == 3 ? byte & 0x0F : byte & 0x07;
        bool good = length > 0 && i + static_cast<size_t>(length) <= text.size();
        for (int n = 1; good && n < length; ++n) {
            const auto next = static_cast<unsigned char>(text[i + static_cast<size_t>(n)]);
            good = (next & 0xC0) == 0x80;
            c = c << 6 | (next & 0x3F);
        }
        if (!good) {
            out.push_back(byte);
            ++i;
            continue;
        }
        out.push_back(c);
        i += static_cast<size_t>(length);
    }
    return out;
}

std::string upperAscii(std::u32string_view text)
{
    std::string out;
    for (const char32_t c : text)
        out.push_back(c < 0x80 ? static_cast<char>(std::toupper(static_cast<int>(c))) : '?');
    return out;
}

}  // namespace

void AutoType::setKeyTables(const KeyTables& tables)
{
    tables_ = tables;
    language_ = keyboardLanguage(tables);
}

bool AutoType::namedKey(std::u32string_view name, CpcKey& key) const
{
    const std::string upper = upperAscii(name);
    for (const NamedKey& entry : kNamed) {
        if (upper == entry.name) {
            key = entry.key;
            return true;
        }
    }
    // A single character names the key that carries it.
    KeyStroke stroke;
    const int code = name.size() == 1 ? cpcCharCode(name[0], language_) : -1;
    if (code < 0 || !keyForCode(tables_, static_cast<uint8_t>(code), stroke))
        return false;
    key = static_cast<CpcKey>(stroke.key);
    return true;
}

void AutoType::type(std::string_view utf8)
{
    const std::u32string text = decode(utf8);
    for (size_t i = 0; i < text.size(); ++i) {
        const char32_t c = text[i];
        if (c == '\r')
            continue;  // CR LF counts as one line break
        if (c != '~') {
            addChar(c);
            continue;
        }
        const size_t end = text.find(U'~', i + 1);
        if (end == std::u32string::npos)
            break;  // unterminated command: drop the rest
        if (end == i + 1)
            addChar('~');
        else
            addCommand(std::u32string_view(text).substr(i + 1, end - i - 1));
        i = end;
    }
}

void AutoType::addChar(char32_t c)
{
    // The code the machine's language shows it with, and the key that
    // gives that code.
    const int code = cpcCharCode(c, language_);
    KeyStroke stroke;
    if (code >= 0 && keyForCode(tables_, static_cast<uint8_t>(code), stroke))
        steps_.push_back({Step::Stroke, static_cast<CpcKey>(stroke.key), stroke.shift, 0, stroke.control});
}

void AutoType::addCommand(std::u32string_view command)
{
    const std::string upper = upperAscii(command);
    if (upper.size() > 6 && upper.compare(0, 6, "PAUSE ") == 0) {
        const int frames = std::atoi(upper.c_str() + 6);
        if (frames > 0)
            steps_.push_back({Step::Pause, CpcKey::Space, false, frames});
        return;
    }
    CpcKey key;
    if (command.size() > 1 && (command[0] == '+' || command[0] == '-')) {
        if (namedKey(command.substr(1), key))
            steps_.push_back({command[0] == '+' ? Step::Hold : Step::Release, key, false, 0});
        return;
    }
    if (namedKey(command, key))
        steps_.push_back({Step::Stroke, key, false, 0});
}

void AutoType::cancel()
{
    steps_.clear();
    for (CpcKey key : down_)
        keyboard_.set(key, false);
    down_.clear();
    wait_ = 0;
}

void AutoType::frame()
{
    if (wait_ > 0 && --wait_ > 0)
        return;

    if (!down_.empty()) {
        for (CpcKey key : down_)
            keyboard_.set(key, false);
        down_.clear();
        wait_ = kUpFrames;
        return;
    }

    while (!steps_.empty()) {
        const Step step = steps_.front();
        steps_.pop_front();
        switch (step.kind) {
        case Step::Stroke:
            keyboard_.set(step.key, true);
            down_.push_back(step.key);
            if (step.shift) {
                keyboard_.set(CpcKey::Shift, true);
                down_.push_back(CpcKey::Shift);
            }
            if (step.control) {
                keyboard_.set(CpcKey::Control, true);
                down_.push_back(CpcKey::Control);
            }
            wait_ = kDownFrames;
            return;
        case Step::Pause:
            wait_ = step.frames;
            return;
        case Step::Hold:
            keyboard_.set(step.key, true);
            break;
        case Step::Release:
            keyboard_.set(step.key, false);
            break;
        }
    }
}

}  // namespace tuxape
