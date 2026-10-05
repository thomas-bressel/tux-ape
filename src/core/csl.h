#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "core/keyboard.h"

namespace tuxape {

class Cpc;

// Runs "CSL" scripts (Cpc Script Language, by Longshot / Logon System): one
// instruction per line that sets up the machine, inserts discs, types on the
// keyboard and waits, so that a test session can be replayed without anyone
// at the keyboard. The Shaker test disc comes with such scripts.
//
// Screenshots are asked for by the script, or by the emulated program
// itself through SSM codes (see Cpc::setSsmHook).
//
// Not handled yet: tapes, snapshots, cartridges and the Plus machines.
class CslRunner {
public:
    // Called with the name (no folder, no extension) each time a picture
    // is to be saved. Returns false if it could not be.
    using ScreenshotSink = std::function<bool(const std::string& name)>;

    explicit CslRunner(Cpc& cpc);
    ~CslRunner();

    void setScreenshotSink(ScreenshotSink sink) { screenshot_ = std::move(sink); }
    // Receives the snapshots a script asks for, as a name without extension
    // and the file's contents.
    using SnapshotSink = std::function<bool(const std::string& name, const std::vector<uint8_t>& data)>;
    void setSnapshotSink(SnapshotSink sink) { snapshot_ = std::move(sink); }
    // Prefix of the names given to pictures requested by SSM codes, which
    // come out as <prefix>_<crtc>_<code>.
    void setEmulatorName(std::string name) { emulatorName_ = std::move(name); }
    // Folders searched for the files a script names, after the script's own.
    void setRomDir(std::filesystem::path dir) { romDir_ = std::move(dir); }
    void setDiscDir(std::filesystem::path dir) { discDir_ = std::move(dir); }
    // Disc used when a script names one that cannot be found.
    void setFallbackDisc(std::filesystem::path file) { fallbackDisc_ = std::move(file); }
    // Upper bound on any single wait for an event, in emulated seconds.
    void setEventTimeout(int seconds) { eventTimeout_ = seconds; }

    // Runs a script to its end. Returns false on the first error, described
    // by error().
    bool run(const std::filesystem::path& script);
    const std::string& error() const { return error_; }
    int screenshotCount() const { return screenshotCount_; }
    int snapshotCount() const { return snapshotCount_; }

private:
    Cpc& cpc_;
    ScreenshotSink screenshot_;
    SnapshotSink snapshot_;
    std::filesystem::path snapshotDir_;
    std::string snapshotName_;
    int snapshotCount_ = 0;
    std::string emulatorName_ = "TUXAPE";
    std::filesystem::path romDir_;
    std::filesystem::path discDir_;
    std::filesystem::path fallbackDisc_;
    std::filesystem::path scriptDir_;
    int eventTimeout_ = 300;

    std::string error_;
    std::string screenshotName_;
    int screenshotCount_ = 0;
    int depth_ = 0;

    // Key timing, in microseconds: how long a key is held, the pause after
    // it, and the pause after RETURN.
    uint32_t keyPress_ = 19968;
    uint32_t keyGap_ = 19968;
    uint32_t keyGapAfterReturn_ = 19968;

    // Last SSM code seen and how many have been seen in all.
    uint16_t lastSsm_ = 0;
    uint64_t ssmCount_ = 0;

    bool fail(const std::string& message);
    bool execute(const std::string& command, const std::vector<std::string>& args);
    bool insertDisc(int drive, const std::string& name);
    bool typeKeys(std::string_view text);
    bool waitForSsm(int code);
    bool takeScreenshot(const std::string& name);
    bool takeSnapshot();
    bool loadSnapshotFile(const std::string& name);
    void onSsm(uint16_t code);
    std::filesystem::path find(const std::filesystem::path& dir, const std::string& name) const;
};

}  // namespace tuxape
