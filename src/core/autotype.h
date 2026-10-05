#pragma once

#include <cstdint>
#include <deque>
#include <string_view>
#include <vector>

#include "core/keyboard.h"

namespace tuxape {

// Finds the key that carries a character on the CPC's UK keyboard, and
// whether SHIFT is needed with it. Returns false for characters the
// keyboard cannot type.
bool cpcKeyForChar(char c, CpcKey& key, bool& shift);

// Types text into the emulated machine by pressing keys on its keyboard,
// using WinAPE's Auto-Type syntax:
//
//   plain text      typed as is; a line break is the RETURN key
//   ~NAME~          a named key: ~RETURN~, ~ESC~, ~COPY~, ~JOY LEFT~, ~FIRE~...
//   ~+KEY~ ~-KEY~   hold a key down, release it
//   ~PAUSE n~       wait n frames (50 per second)
//   ~~              a tilde
class AutoType {
public:
    explicit AutoType(Keyboard& keyboard)
        : keyboard_(keyboard)
    {
    }

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
    };

    Keyboard& keyboard_;
    std::deque<Step> steps_;
    std::vector<CpcKey> down_;  // keys of the stroke in progress
    int wait_ = 0;

    void addChar(char c);
    void addCommand(std::string_view command);
};

}  // namespace tuxape
