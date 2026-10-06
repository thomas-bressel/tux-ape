// The built-in keyboard layout must be WinAPE's, and .kbd files must
// survive a load/save round trip unchanged.

#include "check.h"
#include "core/files.h"
#include "core/keymap.h"

using namespace tuxape;

int main()
{
    const KeyMap defaults;

    // Spot checks of the default layout.
    CHECK_EQ(defaults.pcKey(false, CpcKey::A, 0), PcA);
    // COPY is not on Alt, which is left to the interface.
    CHECK_EQ(defaults.pcKey(true, CpcKey::Copy, 0), PcInsert);
    for (int key = 0; key < kCpcKeyCount; ++key)
        for (int slot = 0; slot < KeyMap::kAlternatives; ++slot)
            CHECK(defaults.pcKey(true, static_cast<CpcKey>(key), slot) != PcLeftAlt);
    CHECK_EQ(defaults.pcKey(true, CpcKey::Shift, 1), PcRightShift);
    CHECK_EQ(defaults.pcKey(true, CpcKey::F1, 0), PcNum1);
    CHECK_EQ(defaults.pcKey(false, CpcKey::F1, 0), PcNone);
    CHECK_EQ(defaults.pcKey(false, CpcKey::JoyUp, 0), PcNum8);

    // The keypad's 7 is "up and left" on the joystick, or f7.
    const auto diagonal = defaults.cpcKeys(PcNum7, false);
    CHECK_EQ(diagonal.size(), 2);
    const auto function = defaults.cpcKeys(PcNum7, true);
    CHECK_EQ(function.size(), 1);
    if (function.size() == 1)
        CHECK(function[0] == CpcKey::F7);

    // Round trip.
    const std::vector<uint8_t> saved = defaults.save();
    KeyMap reloaded;
    reloaded.setPcKey(false, CpcKey::A, 0, PcZ);
    CHECK(reloaded.load(saved));
    CHECK(reloaded.save() == saved);
    CHECK(!reloaded.load(std::vector<uint8_t>(10)));

    // Byte for byte the same as the file WinAPE ships, when it is around,
    // but for COPY, which WinAPE has on Alt.
    if (const auto shipped = readFile(TUXAPE_WINAPE_DIR "/default.kbd")) {
        KeyMap winape;
        CHECK(winape.load(*shipped));
        CHECK_EQ(winape.pcKey(true, CpcKey::Copy, 0), PcLeftAlt);
        for (bool numLock : {false, true})
            winape.setPcKey(numLock, CpcKey::Copy, 0, PcInsert);
        CHECK(winape.save() == saved);
    } else
        std::printf("WinAPE's default.kbd not found; comparison skipped\n");

    return checkSummary("keymap");
}
