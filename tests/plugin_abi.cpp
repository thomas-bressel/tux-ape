// The structures of the plugins' contract (sdk/plugin.h) go between TuxAPE
// and plugins built apart: their layout is part of the contract and never
// changes once a version has them. This test holds it to what the plugins
// already built were built with, here on Linux x86-64 with Qt 6: a field
// added, moved or taken away fails it. Elsewhere it only prints the layout.

#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>

#include "check.h"
#include "plugin.h"

namespace {

template <typename Whole, typename Part>
long offsetIn(const Whole& whole, const Part& part)
{
    return reinterpret_cast<const char*>(&part) - reinterpret_cast<const char*>(&whole);
}

struct Layout {
    const char* name;
    long size;
    long found;
    long wanted;
};

}  // namespace

int main()
{
    const tuxape::PluginScreen screen;
    const tuxape::PluginTiles tiles;
    const tuxape::PluginDisc disc;
    // Since version 1: PluginScreen. Since version 2: PluginTiles. Since
    // version 3: PluginDisc.
    const Layout layouts[] = {
        {"PluginScreen", sizeof screen, offsetIn(screen, screen.mode), 0},
        {"PluginScreen.inks", 0, offsetIn(screen, screen.inks), 8},
        {"PluginScreen.plusColours", 0, offsetIn(screen, screen.plusColours), 32},
        {"PluginScreen.address", 0, offsetIn(screen, screen.address), 56},
        {"PluginScreen.crtcStart", 0, offsetIn(screen, screen.crtcStart), 60},
        {"PluginScreen.columns", 0, offsetIn(screen, screen.columns), 64},
        {"PluginScreen.rows", 0, offsetIn(screen, screen.rows), 68},
        {"PluginScreen.rowHeight", 0, offsetIn(screen, screen.rowHeight), 72},
        {"PluginTiles", sizeof tiles, offsetIn(tiles, tiles.bytes), 0},
        {"PluginTiles.count", 0, offsetIn(tiles, tiles.count), 24},
        {"PluginTiles.width", 0, offsetIn(tiles, tiles.width), 28},
        {"PluginTiles.height", 0, offsetIn(tiles, tiles.height), 32},
        {"PluginTiles.address", 0, offsetIn(tiles, tiles.address), 36},
        {"PluginTiles.mode", 0, offsetIn(tiles, tiles.mode), 40},
        {"PluginDisc", sizeof disc, offsetIn(disc, disc.present), 0},
        {"PluginDisc.writable", 0, offsetIn(disc, disc.writable), 1},
        {"PluginDisc.name", 0, offsetIn(disc, disc.name), 8},
        {"PluginDisc.files", 0, offsetIn(disc, disc.files), 32},
        {"PluginDisc.freeBytes", 0, offsetIn(disc, disc.freeBytes), 56},
    };
    const struct {
        const char* name;
        long size;
    } sizes[] = {{"PluginScreen", 80}, {"PluginTiles", 48}, {"PluginDisc", 64}};
#if defined(__linux__) && defined(__x86_64__)
    for (const Layout& layout : layouts)
        CHECK_EQ(layout.found, layout.wanted);
    for (const auto& size : sizes)
        for (const Layout& layout : layouts)
            if (layout.size && std::string_view(layout.name) == size.name)
                CHECK_EQ(layout.size, size.size);
#else
    for (const Layout& layout : layouts)
        std::printf("%s: at %ld%s\n", layout.name, layout.found, layout.size ? (", size " + std::to_string(layout.size)).c_str() : "");
#endif
    return checkSummary("plugin_abi");
}
