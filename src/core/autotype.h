#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

#include "core/keyboard.h"
#include "core/keytables.h"

namespace tuxape {

// Finds the key that carries a character on the CPC's UK keyboard, and
// whether SHIFT is needed with it. Returns false for characters the
// keyboard cannot type.
bool cpcKeyForChar(char c, CpcKey& key, bool& shift);

// Types text into the emulated machine by pressing keys on its keyboard,
// the keys the machine's own firmware gives each character with (its key
// tables: English, French, Spanish, Danish, German...), using WinAPE's
// Auto-Type syntax:
//
//   plain text      typed as is; a line break is the RETURN key
//   ~NAME~          a named key: ~RETURN~, ~ESC~, ~COPY~, ~JOY LEFT~, ~FIRE~...
//   ~+KEY~ ~-KEY~   hold a key down, release it
//   ~PAUSE n~       wait n frames (50 per second)
//   ~~              a tilde
//
// The text is UTF-8. An accented letter is typed as the code the
// machine's language shows it with ("é" is &7B on a French CPC); one the
// machine has not got is left out.
class AutoType {
public:
    explicit AutoType(Keyboard& keyboard)
        : keyboard_(keyboard)
    {
    }

    // The machine's key tables, found in its firmware or its RAM; the
    // English ones until then.
    void setKeyTables(const KeyTables& tables);
    KeyboardLanguage language() const { return language_; }

    // Queues text after whatever is still waiting to be typed.
    void type(std::string_view text);
    void cancel();
    bool active() const { return !steps_.empty() || !down_.empty(); }

    // Call once per emulated frame.
    void frame();

private:
    struct Step {
        enum Kind : uint8_t { Stroke, Hold, Release, Pause } kind;
        CpcKey key;
        bool shift;
        int frames;
        bool control = false;
    };

    Keyboard& keyboard_;
    KeyTables tables_ = englishKeyTables();
    KeyboardLanguage language_ = KeyboardLanguage::English;
    std::deque<Step> steps_;
    std::vector<CpcKey> down_;  // keys of the stroke in progress
    int wait_ = 0;

    void addChar(char32_t c);
    void addCommand(std::u32string_view command);
    bool namedKey(std::u32string_view name, CpcKey& key) const;
};

}  // namespace tuxape
