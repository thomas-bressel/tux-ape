// The plugins folder: a plugin's description read, the archive it comes in
// looked into and installed from the Setup window's Plugins page, the
// plugin loaded with its entry in the Plugins menu and its button, turned
// off and on again, put in the folder by hand, and removed. Runs without a
// display (QT_QPA_PLATFORM=offscreen).
//
//   gui_plugins [prefix]   also saves pictures of the windows as
//                          <prefix>setup_plugins.png, <prefix>setup_no_plugin.png
//                          and <prefix>window_plugins.png
//   gui_plugins <prefix> <archive>
//                          also tries a real plugin's .zip archive, with
//                          pictures of TuxAPE's window and of the plugin's

#include <array>
#include <utility>
#include <vector>

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QGroupBox>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>

#include "check.h"
#include "core/discfiles.h"
#include "core/screen_text.h"
#include "emulator.h"
#include "discmanager.h"
#include "graphicsdialog.h"
#include "mainwindow.h"
#include "plugin.h"
#include "pluginmanager.h"
#include "settings.h"
#include "setupdialog.h"

namespace {

uint32_t crc32(const QByteArray& data)
{
    static const auto table = [] {
        std::array<uint32_t, 256> made{};
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int bit = 0; bit < 8; ++bit)
                c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            made[n] = c;
        }
        return made;
    }();
    uint32_t crc = 0xFFFFFFFFu;
    for (const char byte : data)
        crc = table[(crc ^ static_cast<uint8_t>(byte)) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

void put(QByteArray& out, uint32_t value, int bytes)
{
    for (int i = 0; i < bytes; ++i)
        out.append(static_cast<char>(value >> (8 * i)));
}

// A .zip archive of files stored as they are.
bool writeZip(const QString& path, const std::vector<std::pair<QString, QByteArray>>& files)
{
    QByteArray body, directory;
    for (const auto& [name, data] : files) {
        const QByteArray label = name.toUtf8();
        const auto offset = static_cast<uint32_t>(body.size());
        const auto size = static_cast<uint32_t>(data.size());
        const auto fields = [&](QByteArray& out) {
            put(out, 20, 2);  // version needed
            put(out, 0, 2);   // flags
            put(out, 0, 2);   // stored
            put(out, 0, 4);   // time and date
            put(out, crc32(data), 4);
            put(out, size, 4);
            put(out, size, 4);
            put(out, static_cast<uint32_t>(label.size()), 2);
            put(out, 0, 2);   // no extra field
        };
        put(body, 0x04034B50, 4);
        fields(body);
        body += label + data;
        put(directory, 0x02014B50, 4);
        put(directory, 20, 2);  // made by
        fields(directory);
        put(directory, 0, 2);   // no comment
        put(directory, 0, 2);   // disc
        put(directory, 0, 2);   // attributes
        put(directory, 0, 4);
        put(directory, offset, 4);
        directory += label;
    }
    QByteArray end;
    put(end, 0x06054B50, 4);
    put(end, 0, 4);
    put(end, static_cast<uint32_t>(files.size()), 2);
    put(end, static_cast<uint32_t>(files.size()), 2);
    put(end, static_cast<uint32_t>(directory.size()), 4);
    put(end, static_cast<uint32_t>(body.size()), 4);
    put(end, 0, 2);
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(body + directory + end) == body.size() + directory.size() + end.size();
}

QByteArray manifest(const QString& id, int api, const QString& platform, const QString& library)
{
    return QString("{\"id\": \"%1\", \"name\": \"Sample Plugin\", \"version\": \"1.2\", \"author\": \"TuxAPE's tests\","
                   " \"licence\": \"GPL-2.0\", \"description\": \"Counts how often it is chosen.\", \"api\": %2,"
                   " \"libraries\": {\"%3\": \"%4\"}}")
        .arg(id)
        .arg(api)
        .arg(platform, library)
        .toUtf8();
}

QAction* entry(const QMenu* menu, const QString& text)
{
    for (QAction* action : menu->actions())
        if (action->text() == text)
            return action;
    return nullptr;
}

}  // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QTemporaryDir folder;
    Settings::setFile(folder.filePath("TuxAPE.ini"));
    qputenv("TUXAPE_LIBRARY_DIR", folder.filePath("no library").toLocal8Bit());
    const QString plugins = folder.filePath("plugins");
    qputenv("TUXAPE_PLUGIN_DIR", plugins.toLocal8Bit());
    const QString prefix = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();

    CHECK(PluginManager::folder() == plugins);
    const QString platform = PluginManager::platform();
    CHECK(platform.contains('-') && platform == platform.toLower());

    // ---- A plugin's description ----
    {
        const auto info = PluginManager::parse(manifest("sample", tuxape::kPluginApi, platform, "lib/libsample.so"));
        CHECK(info && info->id == "sample" && info->name == "Sample Plugin" && info->version == "1.2");
        CHECK(info && info->licence == "GPL-2.0" && info->api == tuxape::kPluginApi && info->problem.isEmpty());
        CHECK(info && info->library == "lib/libsample.so" && info->platforms == QStringList{platform});
        // Not a description: no text, no id, an id that could not name a folder.
        QString error;
        CHECK(!PluginManager::parse("not json", &error) && !error.isEmpty());
        CHECK(!PluginManager::parse("{\"name\": \"Nameless\"}"));
        CHECK(!PluginManager::parse(manifest("../sample", tuxape::kPluginApi, platform, "a.so")));
        CHECK(!PluginManager::parse(manifest("Sample", tuxape::kPluginApi, platform, "a.so")));
        // A description that reads, of a plugin that cannot be used here.
        const auto other = PluginManager::parse(manifest("sample", tuxape::kPluginApi + 1, platform, "a.so"));
        CHECK(other && other->problem.contains("version"));
        // One made for an earlier version still finds all it was built
        // against: it can be used, back to the oldest version there is.
        const auto earlier = PluginManager::parse(manifest("sample", tuxape::kOldestPluginApi, platform, "a.so"));
        CHECK(tuxape::kOldestPluginApi < tuxape::kPluginApi && earlier && earlier->problem.isEmpty());
        const auto none = PluginManager::parse(manifest("sample", tuxape::kOldestPluginApi - 1, platform, "a.so"));
        CHECK(none && none->problem.contains("version"));
        const auto elsewhere = PluginManager::parse(manifest("sample", tuxape::kPluginApi, "cpc-z80", "a.so"));
        CHECK(elsewhere && elsewhere->library.isEmpty() && elsewhere->problem.contains(platform));
        const auto outside = PluginManager::parse(manifest("sample", tuxape::kPluginApi, platform, "../a.so"));
        CHECK(outside && !outside->problem.isEmpty());
    }

    // ---- The archives ----
    QFile built(QStringLiteral(TUXAPE_TEST_PLUGIN));
    CHECK(built.open(QIODevice::ReadOnly));
    const QByteArray library = built.readAll();
    const QString libraryPath = platform + "/libsample.so";
    const QByteArray good = manifest("sample", tuxape::kPluginApi, platform, libraryPath);
    const QString archive = folder.filePath("sample-1.2.zip");
    // The plugin in a folder of the archive, as an archive of a folder is made.
    CHECK(writeZip(archive, {{"sample/plugin.json", good},
                             {"sample/" + libraryPath, library},
                             {"sample/bin/tool", "#!/bin/sh\n"},
                             {"sample/README.txt", "A plugin to try TuxAPE with.\n"}}));
    // And at the archive's top.
    const QString flat = folder.filePath("flat.zip");
    CHECK(writeZip(flat, {{"plugin.json", good}, {libraryPath, library}}));
    const QString stale = folder.filePath("stale.zip");
    CHECK(writeZip(stale, {{"sample/plugin.json", manifest("sample", tuxape::kPluginApi + 1, platform, libraryPath)},
                           {"sample/" + libraryPath, library}}));
    const QString hollow = folder.filePath("hollow.zip");
    CHECK(writeZip(hollow, {{"sample/plugin.json", good}}));
    const QString escaping = folder.filePath("escaping.zip");
    CHECK(writeZip(escaping, {{"sample/plugin.json", good}, {"sample/" + libraryPath, library}, {"sample/../../out.txt", "x"}}));
    const QString two = folder.filePath("two.zip");
    CHECK(writeZip(two, {{"a/plugin.json", good}, {"b/plugin.json", good}}));
    const QString none = folder.filePath("none.zip");
    CHECK(writeZip(none, {{"sample/readme.txt", "nothing here"}}));
    {
        QString error;
        const auto info = PluginManager::inspect(archive, &error);
        CHECK(info && info->id == "sample" && info->problem.isEmpty() && info->folder.isEmpty());
        CHECK(PluginManager::inspect(flat).has_value());
        CHECK(!PluginManager::inspect(two, &error) && error.contains("more than one"));
        CHECK(!PluginManager::inspect(none, &error) && error.contains("plugin.json"));
        CHECK(!PluginManager::inspect(folder.filePath("TuxAPE.ini"), &error) && error.contains(".zip"));
        const auto old = PluginManager::inspect(stale);
        CHECK(old && !old->problem.isEmpty());
    }

    // ---- Installing, with no window for the plugin to talk to ----
    {
        PluginManager manager;
        CHECK(manager.plugins().empty());
        int changes = 0;
        QObject::connect(&manager, &PluginManager::changed, [&changes] { ++changes; });
        QString error;
        // What cannot be used is not installed, and nothing is left of it.
        CHECK(!manager.install(stale, &error) && error.contains("version"));
        CHECK(!manager.install(hollow, &error) && error.contains("missing"));
        CHECK(!manager.install(escaping, &error) && error.contains("unpacked"));
        CHECK(!QFile::exists(folder.filePath("out.txt")) && !QFile::exists(plugins + "/../out.txt"));
        CHECK(!manager.install(none, &error) && manager.plugins().empty());
        CHECK(QDir(plugins).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty());
        CHECK_EQ(changes, 0);

        CHECK(manager.install(archive, &error));
        CHECK_EQ(changes, 1);
        CHECK_EQ(manager.plugins().size(), 1);
        const PluginInfo* info = manager.plugin("sample");
        CHECK(info && info->name == "Sample Plugin" && info->folder == plugins + "/sample" && info->problem.isEmpty());
        // Without a window nothing is loaded.
        manager.start();
        CHECK(info && info->enabled && !manager.plugin("sample")->loaded);
        CHECK(QFile::exists(plugins + "/sample/plugin.json") && QFile::exists(plugins + "/sample/" + libraryPath));
        CHECK(QFile::exists(plugins + "/sample/README.txt"));
        // A program it brings can be run.
        CHECK(QFile::permissions(plugins + "/sample/bin/tool") & QFile::ExeOwner);
        CHECK(!(QFile::permissions(plugins + "/sample/README.txt") & QFile::ExeOwner));

        // Installed again, from the archive without a folder: it takes the
        // other's place whole.
        CHECK(manager.install(flat, &error));
        CHECK_EQ(manager.plugins().size(), 1);
        CHECK(!QFile::exists(plugins + "/sample/README.txt") && QFile::exists(plugins + "/sample/" + libraryPath));

        CHECK(!manager.remove("nothing"));
        CHECK(manager.remove("sample", &error));
        CHECK(manager.plugins().empty() && !QDir(plugins + "/sample").exists());
    }

    // ---- The Setup window's page by itself ----
    {
        Settings settings;
        SetupDialog setup(settings);
        auto* tabs = setup.findChild<QTabWidget*>("PageControl");
        CHECK(tabs && tabs->count() == 8 && tabs->tabText(SetupDialog::Plugins) == "Plugins");
        setup.showPage(SetupDialog::Plugins);
        CHECK(tabs && tabs->currentIndex() == SetupDialog::Plugins);
        auto* list = setup.findChild<QTreeWidget*>("lvPlugins");
        auto* install = setup.findChild<QPushButton*>("bInstallPlugin");
        auto* remove = setup.findChild<QPushButton*>("bRemovePlugin");
        auto* box = setup.findChild<QGroupBox*>("gbPlugin");
        auto* text = setup.findChild<QLabel*>("lPluginInfo");
        CHECK(list && install && remove && box && text && setup.findChild<QPushButton*>("bPluginFolder"));
        // Told of no plugins, the page has nothing to install them with.
        CHECK(install && !install->isEnabled() && remove && !remove->isEnabled());
        CHECK(!setup.installPlugin(archive));

        PluginManager manager;
        setup.setPlugins(&manager);
        CHECK(install && install->isEnabled() && remove && !remove->isEnabled());
        CHECK(list && list->topLevelItemCount() == 0 && setup.selectedPlugin().isEmpty());
        CHECK(box && box->title() == "No Plugin" && text && text->text().contains(QDir::toNativeSeparators(plugins)));
        if (!prefix.isEmpty()) {
            setup.show();
            QTest::qWait(50);
            setup.grab().save(prefix + "setup_no_plugin.png");
        }

        QString error;
        CHECK(!setup.installPlugin(stale, &error) && error.contains("version"));
        CHECK(list && list->topLevelItemCount() == 0);
        CHECK(setup.installPlugin(archive, &error));
        CHECK(list && list->topLevelItemCount() == 1 && setup.selectedPlugin() == "sample");
        QTreeWidgetItem* item = list ? list->topLevelItem(0) : nullptr;
        CHECK(item && item->text(1) == "Sample Plugin" && item->text(2) == "1.2" && item->text(3) == "TuxAPE's tests");
        CHECK(item && item->checkState(0) == Qt::Checked && remove && remove->isEnabled());
        CHECK(box && box->title() == "Sample Plugin");
        CHECK(text && text->text().contains("Counts how often it is chosen.") && text->text().contains("GPL-2.0"));
        CHECK(text && text->text().contains("Loaded when TuxAPE starts")
              && text->text().contains(QDir::toNativeSeparators(plugins + "/sample")));

        // Its box is a setting: cleared, the plugin is among those not to load.
        CHECK(setup.settings().disabledPlugins.isEmpty());
        if (item)
            item->setCheckState(0, Qt::Unchecked);
        CHECK(setup.settings().disabledPlugins == QStringList{"sample"});
        CHECK(text && text->text().contains("its box is not ticked"));
        settings.disabledPlugins.clear();
        setup.setSettings(settings);
        CHECK(list && list->topLevelItem(0)->checkState(0) == Qt::Checked);
        settings.disabledPlugins = QStringList{"sample", "gone"};
        setup.setSettings(settings);
        CHECK(list && list->topLevelItem(0)->checkState(0) == Qt::Unchecked);
        // The setting is kept from one run to the next.
        CHECK(settings.save());
        Settings kept;
        kept.load();
        CHECK(kept.disabledPlugins == settings.disabledPlugins);

        // Removed, it is gone from the list and from those turned off.
        CHECK(setup.removePlugin("sample", &error));
        CHECK(list && list->topLevelItemCount() == 0 && !QDir(plugins + "/sample").exists());
        CHECK(setup.settings().disabledPlugins == QStringList{"gone"});
        setup.setPlugins(nullptr);
    }

    // ---- In the application ----
    Emulator emulator;
    if (!emulator.setupMachine(tuxape::CpcModel::Cpc6128).isEmpty()) {
        std::printf("ROM images not found; the application's part is skipped\n");
        return g_failures ? checkSummary("gui_plugins") : 77;
    }
    {
        // A plugin put in the folder by hand is found when the program starts.
        CHECK(QDir().mkpath(plugins + "/sample/" + platform));
        CHECK(QFile::copy(QStringLiteral(TUXAPE_TEST_PLUGIN), plugins + "/sample/" + libraryPath));
        QFile byHand(plugins + "/sample/plugin.json");
        CHECK(byHand.open(QIODevice::WriteOnly) && byHand.write(good) == good.size());
        byHand.close();
        // And one that is not what it says: no library.
        CHECK(QDir().mkpath(plugins + "/broken"));
        QFile broken(plugins + "/broken/plugin.json");
        CHECK(broken.open(QIODevice::WriteOnly)
              && broken.write(manifest("broken", tuxape::kPluginApi, platform, "nowhere.so")) > 0);
        broken.close();

        MainWindow window(&emulator);
        window.show();
        // The machine as it is once started: mode 1, its picture at &C000.
        emulator.setSpeedPercent(1000);
        emulator.start();
        CHECK(QTest::qWaitFor(
            [&] {
                return emulator.withMachine([](tuxape::Cpc& cpc) { return tuxape::readScreenText(cpc); }).find("Ready")
                       != std::string::npos;
            },
            10000));
        QMenu* menu = window.pluginsMenu();
        CHECK(menu && menu->title() == "&Plugins" && !menu->menuAction()->isVisible());
        CHECK(entry(menu, "&Manage Plugins...") != nullptr);
        CHECK_EQ(window.plugins()->plugins().size(), 2);
        CHECK(!window.plugins()->started() && !window.plugins()->plugin("sample")->loaded);
        CHECK(!window.property("sampleStarted").toBool());

        window.loadPlugins();
        const PluginInfo* sample = window.plugins()->plugin("sample");
        CHECK(sample && sample->loaded && sample->problem.isEmpty());
        if (sample && !sample->problem.isEmpty())
            std::printf("  %s\n", qPrintable(sample->problem));
        const PluginInfo* bad = window.plugins()->plugin("broken");
        CHECK(bad && !bad->loaded && bad->problem.contains("missing"));
        CHECK(window.property("sampleStarted").toBool());
        // Its entry is in the menu, above the line, with its key; the menu shows.
        CHECK(menu && menu->menuAction()->isVisible());
        QAction* chosen = entry(menu, "&Sample Plugin...");
        CHECK(chosen && menu->actions().indexOf(chosen) == 0 && menu->actions().at(1)->isSeparator());
        CHECK(chosen && chosen->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_M));
        // And its button on the control panel.
        QToolButton* button = nullptr;
        for (QToolButton* candidate : window.findChildren<QToolButton*>())
            if (candidate->toolTip().startsWith("Sample Plugin"))
                button = candidate;
        CHECK(button && button->isVisible() && button->toolTip() == "Sample Plugin (Ctrl+M)");

        // Chosen, by the menu or the button, the plugin reads the machine.
        if (chosen)
            chosen->trigger();
        if (button)
            button->click();
        CHECK_EQ(window.property("sampleChosen").toInt(), 2);
        CHECK_EQ(window.property("sampleMode").toInt(), 1);
        CHECK_EQ(window.property("sampleInks").toInt(), 17);
        CHECK_EQ(window.property("sampleAddress").toInt(), 0xC000);
        CHECK_EQ(window.property("sampleStart").toInt(), 0x3000);
        CHECK_EQ(window.property("sampleBytes").toInt(), 0x4000);
        CHECK(window.property("sampleFolder").toString() == folder.filePath("Plugins/sample"));
        CHECK(QDir(folder.filePath("Plugins/sample")).exists());
        if (!prefix.isEmpty()) {
            QTest::qWait(50);
            window.grab().save(prefix + "window_plugins.png");
        }

        // Its entry of Find Graphics' menu: there before that window is,
        // and in the window's menu once it is; chosen, the plugin is given
        // the tiles selected.
        QAction* toSample = nullptr;
        QAction* findGraphics = nullptr;
        for (QAction* action : window.findChildren<QAction*>()) {
            if (action->text() == "To the &Sample...")
                toSample = action;
            else if (action->text() == "&Find Graphics")
                findGraphics = action;
        }
        CHECK(toSample && !toSample->isEnabled() && findGraphics && window.graphics() == nullptr);
        if (toSample && findGraphics) {
            // Chosen with no such window, by a key say, it is given nothing.
            toSample->trigger();
            CHECK(window.property("sampleTiles").toInt() == 0 && window.property("sampleTileBytes").toByteArray().isEmpty());
            findGraphics->trigger();
            GraphicsDialog* graphics = window.graphics();
            CHECK(graphics && graphics->extraActions() == QList<QAction*>{toSample} && !toSample->isEnabled());
            if (graphics) {
                graphics->setView(1, 3, 0xC000, 2, 4, GraphicsDialog::Cpc);
                graphics->select(1, 2);
                CHECK(toSample->isEnabled());
                toSample->trigger();
                CHECK_EQ(window.property("sampleTiles").toInt(), 2);
                CHECK(window.property("sampleTileBytes").toByteArray() == graphics->selectionBytes());
                CHECK_EQ(window.property("sampleTileBytes").toByteArray().size(), 16);
                CHECK(window.property("sampleTileShape").toString() == "2x4 at c008 in mode 1");
            }
        }

        // A source the plugin wrote is assembled into the machine and run.
        // Written again, it is the new text that is; one that does not
        // assemble is refused.
        if (chosen) {
            const QString source = folder.filePath("sample.asm");
            const auto write = [&](const char* text) {
                QFile file(source);
                CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(text) > 0);
            };
            const auto poked = [&](int value) {
                return QTest::qWaitFor(
                    [&] {
                        return emulator.withMachine([](tuxape::Cpc& cpc) { return cpc.memory().readRam(0x9000); })
                               == value;
                    },
                    5000);
            };
            // The assembler says what it did in a box of its own, as when
            // its Run is chosen in its window: the box is closed for it.
            int boxes = 0;
            const auto run = [&] {
                QTimer::singleShot(0, [&boxes] {
                    QWidget* box = QApplication::activeModalWidget();
                    if (box && box->objectName() == "Assembling") {
                        ++boxes;
                        box->close();
                    }
                });
                chosen->trigger();
            };
            window.setProperty("sampleSource", source);
            write("org &8000\nrun &8000\nld a,&42\nld (&9000),a\nloop: jr loop\n");
            run();
            CHECK(window.property("sampleRan").toBool());
            CHECK(poked(0x42));
            write("org &8000\nrun &8000\nld a,&17\nld (&9000),a\nloop: jr loop\n");
            run();
            CHECK(window.property("sampleRan").toBool());
            CHECK(poked(0x17));
            write("org &8000\nrun &8000\nld a,(\n");
            run();
            CHECK(!window.property("sampleRan").toBool());
            CHECK_EQ(boxes, 3);
            window.setProperty("sampleSource", QString());
            // The machine is left to itself again for what follows.
            emulator.reset(true);
            for (QWidget* top : QApplication::topLevelWidgets())
                if (top != &window && top->inherits("AssemblerDialog"))
                    top->close();
        }

        // A file the plugin puts on the disc in drive A: is there, and the
        // disc has changed; and the plugin finds itself among those loaded.
        if (chosen) {
            window.setProperty("sampleFile", "nodisc.bin");
            chosen->trigger();
            CHECK(window.property("samplePut").toString().contains("no disc in drive A:"));
            CHECK(window.property("sampleDisc").toString() == "0 0  ");
            CHECK(window.discs()->createBlank(0, folder.filePath("plugin.dsk"), tuxape::defaultDiscFormat()).isEmpty());
            const QByteArray content(1000, 'x');
            window.setProperty("sampleFile", "made.bin");
            window.setProperty("sampleContent", content);
            chosen->trigger();
            CHECK(window.property("samplePut").toString().isEmpty());
            CHECK(window.property("sampleDisc").toString() == "1 1 plugin.dsk MADE.BIN");
            CHECK_EQ(window.property("sampleFree").toInt(), 178 * 1024 - 1024);
            CHECK_EQ(window.property("sampleDrives").toInt(), 2);
            CHECK(!window.property("sampleNoDrive").toBool());
            CHECK(window.property("sampleItself").toBool() && window.property("sampleNobody").toBool());
            CHECK(window.discs()->info(0).modified);
            const auto read = emulator.withMachine([](tuxape::Cpc& cpc) {
                tuxape::DiscFiles files(*cpc.fdc().drive(0).disc);
                const std::vector<tuxape::DiscFile> list = files.list();
                return list.empty() ? std::nullopt : files.read(list[0]);
            });
            CHECK(read && read->size() == 1024 && std::equal(content.begin(), content.end(), read->begin()));
            // Put again under the same name, it takes the other's place; a
            // name a disc cannot have is refused.
            window.setProperty("sampleContent", QByteArray(10, 'y'));
            chosen->trigger();
            CHECK(window.property("sampleDisc").toString() == "1 1 plugin.dsk MADE.BIN");
            window.setProperty("sampleFile", "much.too.long");
            chosen->trigger();
            CHECK(window.property("samplePut").toString().contains("not a name"));
            window.setProperty("sampleFile", QString());
            window.discs()->remove(0);
        }

        // The Setup window's page, as the Settings menu opens it.
        {
            SetupDialog setup(window.settings(), &window);
            setup.setPlugins(window.plugins());
            setup.showPage(SetupDialog::Plugins);
            auto* text = setup.findChild<QLabel*>("lPluginInfo");
            auto* list = setup.findChild<QTreeWidget*>("lvPlugins");
            CHECK(list && list->topLevelItemCount() == 2);
            setup.selectPlugin("sample");
            CHECK(text && text->text().contains("Loaded. It is in the Plugins menu."));
            if (!prefix.isEmpty()) {
                setup.show();
                QTest::qWait(50);
                setup.grab().save(prefix + "setup_plugins.png");
            }
            setup.selectPlugin("broken");
            CHECK(text && text->text().contains("Cannot be used") && text->text().contains("nowhere.so"));
        }

        // Turned off, it lets go of the window: no entry, no button, no menu.
        window.plugins()->setDisabled({"sample"});
        CHECK(!window.plugins()->plugin("sample")->loaded && !window.plugins()->plugin("sample")->enabled);
        CHECK(!window.property("sampleStarted").toBool());
        CHECK(menu && !menu->menuAction()->isVisible() && entry(menu, "&Sample Plugin...") == nullptr);
        bool buttonLeft = false;
        for (const QToolButton* candidate : window.findChildren<QToolButton*>())
            buttonLeft = buttonLeft || candidate->toolTip().startsWith("Sample Plugin");
        CHECK(!buttonLeft);
        // Nor any entry in Find Graphics' menu.
        CHECK(window.graphics() && window.graphics()->extraActions().isEmpty());
        // And on again.
        window.plugins()->setDisabled({});
        CHECK(window.plugins()->plugin("sample")->loaded && window.property("sampleStarted").toBool());
        CHECK(menu && menu->menuAction()->isVisible() && entry(menu, "&Sample Plugin...") != nullptr);
        CHECK(window.graphics() && window.graphics()->extraActions().size() == 1);

        // A newer one installed over it is the one loaded from then on.
        QString error;
        CHECK(window.plugins()->install(archive, &error));
        CHECK(window.plugins()->plugin("sample")->loaded && window.property("sampleStarted").toBool());
        CHECK(QFile::exists(plugins + "/sample/README.txt"));
        QAction* again = entry(menu, "&Sample Plugin...");
        if (again)
            again->trigger();
        CHECK_EQ(window.property("sampleChosen").toInt(), 3);

        // One that will not start says why, and is not loaded.
        window.plugins()->setDisabled({"sample"});
        window.setProperty("sampleRefuses", true);
        window.plugins()->setDisabled({});
        sample = window.plugins()->plugin("sample");
        CHECK(sample && !sample->loaded && sample->problem == "it was told not to");
        CHECK(menu && !menu->menuAction()->isVisible());
        window.setProperty("sampleRefuses", false);
        window.plugins()->scan();
        window.plugins()->start();
        CHECK(window.plugins()->plugin("sample")->loaded);

        // Removed while loaded: stopped first, then its folder goes.
        CHECK(window.plugins()->remove("sample", &error));
        CHECK(!window.property("sampleStarted").toBool() && !QDir(plugins + "/sample").exists());
        CHECK(menu && !menu->menuAction()->isVisible());
        CHECK_EQ(window.plugins()->plugins().size(), 1);

        // Given a real plugin's archive, that one is tried as well: installed,
        // loaded, and the first entry it put in the menu chosen.
        if (argc > 2) {
            const QString real = QString::fromLocal8Bit(argv[2]);
            const auto described = PluginManager::inspect(real, &error);
            CHECK(described.has_value());
            CHECK(window.plugins()->install(real, &error));
            const PluginInfo* info = described ? window.plugins()->plugin(described->id) : nullptr;
            CHECK(info && info->loaded);
            if (!info || !info->loaded)
                std::printf("  %s\n", qPrintable(info ? info->problem : error));
            CHECK(menu && menu->menuAction()->isVisible());
            QTest::qWait(50);
            window.grab().save(prefix + "window_real_plugin.png");
            if (menu && !menu->actions().constFirst()->isSeparator())
                menu->actions().constFirst()->trigger();
            QTest::qWait(100);
            int shown = 0;
            for (QWidget* widget : QApplication::topLevelWidgets())
                if (widget != &window && widget->isVisible() && widget->parentWidget() == &window)
                    widget->grab().save(prefix + QString("real_plugin_window%1.png").arg(++shown));
            CHECK(shown > 0);
            CHECK(described && window.plugins()->remove(described->id, &error));
        }

        // A window that goes with a plugin loaded stops it first.
        CHECK(window.plugins()->install(archive, &error));
        CHECK(window.property("sampleStarted").toBool());
    }
    return checkSummary("gui_plugins");
}
