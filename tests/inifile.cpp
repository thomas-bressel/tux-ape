// The INI file reader and writer, on made-up text and on the files that come
// with WinAPE: its own WinAPE.ini and one of its profiles.

#include <filesystem>
#include <string>

#include "check.h"
#include "core/inifile.h"

namespace {

using namespace tuxape;

void testParsing()
{
    const IniFile ini = IniFile::parse("; a comment\r\n"
                                       "[ROMS]\r\n"
                                       "Lower=OS6128\r\n"
                                       "Upper(0)=BASIC1-1\r\n"
                                       "Upper(1)=\r\n"
                                       "\r\n"
                                       "[Configuration]\r\n"
                                       "CRTC Type=2\r\n"
                                       "Full Screen=false\r\n"
                                       "Sound Stereo=true\r\n"
                                       "  Sound Frame Delay = 0.0  \n"
                                       "Keyboard File=C:\\My Files\\a=b.kbd\n"
                                       "CRTC Type=4\n"
                                       "not a key\n");
    CHECK(ini.get("ROMS", "Lower") == "OS6128");
    CHECK(ini.get("ROMS", "Upper(0)") == "BASIC1-1");
    // Present but empty is not the same as missing.
    CHECK(ini.has("ROMS", "Upper(1)"));
    CHECK(ini.get("ROMS", "Upper(1)", "x").empty());
    CHECK(!ini.has("ROMS", "Upper(2)"));
    CHECK(ini.get("ROMS", "Upper(2)", "x") == "x");

    // Names are matched without regard to case; of two keys of the same
    // name the first counts.
    CHECK_EQ(ini.getInt("configuration", "crtc type", -1), 2);
    CHECK_EQ(ini.getBool("Configuration", "Full Screen", true), false);
    CHECK_EQ(ini.getBool("Configuration", "Sound Stereo", false), true);
    CHECK(ini.get("Configuration", "Sound Frame Delay") == "0.0");
    // Only the first "=" splits the line.
    CHECK(ini.get("Configuration", "Keyboard File") == "C:\\My Files\\a=b.kbd");

    // Values of the wrong kind give the fallback.
    CHECK_EQ(ini.getInt("ROMS", "Lower", 7), 7);
    CHECK_EQ(ini.getInt("Configuration", "Sound Frame Delay", 7), 7);
    CHECK_EQ(ini.getBool("ROMS", "Lower", true), true);
    CHECK_EQ(ini.getInt("Nowhere", "Nothing", 9), 9);

    CHECK_EQ(ini.sections().size(), 2);
    CHECK_EQ(ini.keys("ROMS").size(), 3);
    CHECK(ini.keys("Configuration").front() == "CRTC Type");
}

void testWriting()
{
    IniFile ini;
    ini.setInt("Configuration", "CRTC Type", 1);
    ini.setBool("Drives", "Fast Disc", true);
    ini.set("Configuration", "Machine", "CPC 6128");
    ini.setInt("configuration", "crtc type", 3);  // the same key
    CHECK(ini.text() == "[Configuration]\r\nCRTC Type=3\r\nMachine=CPC 6128\r\n\r\n[Drives]\r\nFast Disc=true\r\n");

    const IniFile again = IniFile::parse(ini.text());
    CHECK(again.text() == ini.text());
    CHECK_EQ(again.getInt("Configuration", "CRTC Type", -1), 3);
    CHECK_EQ(again.getBool("Drives", "Fast Disc", false), true);

    ini.remove("Configuration", "MACHINE");
    CHECK(!ini.has("Configuration", "Machine"));

    const std::filesystem::path path = std::filesystem::temp_directory_path() / "tuxape_inifile_test.ini";
    CHECK(ini.save(path));
    IniFile loaded;
    CHECK(loaded.load(path));
    CHECK(loaded.text() == ini.text());
    std::filesystem::remove(path);
    CHECK(!loaded.load(path));
    CHECK(loaded.sections().empty());
}

void testWinApeFiles()
{
    const std::filesystem::path folder = TUXAPE_WINAPE_DIR;
    IniFile ini;
    if (!ini.load(folder / "WinAPE.ini")) {
        std::printf("WinAPE.ini not found; that part is skipped\n");
        return;
    }
    CHECK_EQ(ini.getInt("Configuration", "CRTC Type", -1), 0);
    CHECK_EQ(ini.getInt("Configuration", "Sound Volume", -1), 15);
    CHECK_EQ(ini.getBool("Configuration", "Sound Stereo", false), true);
    CHECK(ini.get("ROMS", "Upper(7)") == "ParaDOS 1-2+");
    CHECK(ini.has("ROMS", "Lower"));

    IniFile profile;
    CHECK(profile.load(folder / "Profile" / "CPC6128.wpf"));
    CHECK(profile.get("ROMS", "Lower") == "OS6128");
    CHECK(profile.get("ROMS", "Upper(0)") == "BASIC1-1");
    CHECK(profile.get("ROMS", "Upper(7)") == "AMSDOS");
    CHECK_EQ(profile.getBool("ROMS", "Cartridge Enabled", true), false);
    CHECK_EQ(profile.getInt("Configuration", "Extended RAM", -1), 1);
    CHECK_EQ(profile.getBool("Configuration", "Silicon Disc", true), false);
}

}  // namespace

int main()
{
    testParsing();
    testWriting();
    testWinApeFiles();
    return checkSummary("inifile");
}
