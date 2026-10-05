// The Plus machines in the application: the settings that make one, WinAPE's
// profiles for them, the Setup window's switches, a cartridge loaded from
// the menu, and one taken from the Library. Runs without a display
// (QT_QPA_PLATFORM=offscreen).
//
// Needs WinAPE's system cartridge; exits with code 77 (skipped) without it.

#include <functional>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>

#include "check.h"
#include "core/cpc.h"
#include "core/screen_text.h"
#include "core/setup.h"
#include "emulator.h"
#include "librarydialog.h"
#include "mainwindow.h"
#include "settings.h"
#include "setupdialog.h"

namespace {

// Deals with the window a click opens: `handle` is given it as it comes
// up, and has to close it.
class Modals {
public:
    explicit Modals(std::function<void(QWidget*)> handle)
    {
        QObject::connect(&timer_, &QTimer::timeout, [this, handle] {
            QWidget* modal = QApplication::activeModalWidget();
            if (modal && modal != last_) {
                last_ = modal;
                ++seen_;
                handle(modal);
            }
        });
        timer_.start(5);
    }
    int seen() const { return seen_; }

private:
    QTimer timer_;
    QWidget* last_ = nullptr;
    int seen_ = 0;
};

QAction* actionNamed(MainWindow& window, const char* text)
{
    for (QAction* action : window.findChildren<QAction*>())
        if (action->text().remove(QLatin1Char('&')) == QLatin1String(text))
            return action;
    return nullptr;
}

}  // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QTemporaryDir folder;
    Settings::setFile(folder.filePath("TuxAPE.ini"));
    using tuxape::CpcModel;

    const QString systemCartridge =
        QString::fromStdString(tuxape::findRom("CPC_PLUS.CPR", tuxape::defaultRomDir()).string());
    if (systemCartridge.isEmpty()) {
        std::printf("the system cartridge was not found; skipping\n");
        return 77;
    }

    // The settings: WinAPE's keys, and its profiles.
    {
        Settings settings;
        CHECK(!settings.machine.isPlus());
        settings.machine = tuxape::stockMachine(CpcModel::Plus6128);
        settings.crtcType = 3;
        CHECK(settings.save());
        QFile file(Settings::file());
        CHECK(file.open(QIODevice::ReadOnly));
        const QByteArray text = file.readAll();
        CHECK(text.contains("\r\nCartridge=CPC_PLUS.CPR\r\n"));
        CHECK(text.contains("\r\nCartridge Enabled=true\r\n"));
        CHECK(text.contains("\r\nEnable Plus=true\r\n"));
        Settings loaded;
        loaded.load();
        CHECK(loaded.machine == settings.machine);
        CHECK(loaded.machine.isPlus());
        CHECK(tuxape::modelOf(loaded.machine) == CpcModel::Plus6128);

        Settings profile;
        CHECK(profile.loadProfile(Settings::profileFolder() + "/464 Plus.wpf"));
        CHECK(profile.machine.isPlus());
        CHECK(tuxape::modelOf(profile.machine) == CpcModel::Plus464);
        CHECK_EQ(profile.crtcType, 3);
        CHECK(profile.loadProfile(Settings::profileFolder() + "/CPC6128.wpf"));
        CHECK(!profile.machine.isPlus());
        CHECK(profile.machine.lowerRom == "OS6128");
        CHECK_EQ(profile.crtcType, 0);
        QFile::remove(Settings::file());
    }

    // The Setup window's two switches and the cartridge's file.
    {
        Settings settings;
        SetupDialog dialog(settings);
        auto* plus = dialog.findChild<QCheckBox*>("ckEnablePlus");
        auto* cartridge = dialog.findChild<QCheckBox*>("ckEnableCart");
        auto* file = dialog.findChild<QLabel*>("lCartridge");
        auto* browse = dialog.findChild<QToolButton*>("sbCartridge");
        CHECK(plus && plus->isEnabled() && !plus->isChecked());
        CHECK(cartridge && cartridge->isEnabled() && !cartridge->isChecked());
        CHECK(file && file->text().isEmpty());
        CHECK(browse && browse->isEnabled());
        settings.machine = tuxape::stockMachine(CpcModel::Plus6128);
        dialog.setSettings(settings);
        CHECK(plus->isChecked() && cartridge->isChecked());
        CHECK(file->text() == "CPC_PLUS.CPR");
        CHECK(dialog.settings().machine == settings.machine);
        plus->setChecked(false);
        CHECK(!dialog.settings().machine.isPlus());
        CHECK(dialog.settings().machine.cartridge == "CPC_PLUS.CPR");
        // A profile for a Plus sets all three.
        auto* profiles = dialog.findChild<QComboBox*>("cbProfile");
        const int index = profiles->findText("6128 Plus");
        CHECK(index > 0);
        profiles->setCurrentIndex(index);
        emit profiles->activated(index);
        CHECK(dialog.settings().machine.isPlus());
        CHECK_EQ(dialog.settings().crtcType, 3);
    }

    Emulator emulator;
    CHECK(emulator.setupMachine(CpcModel::Cpc6128).isEmpty());
    MainWindow window(&emulator);
    window.show();
    emulator.setSpeedPercent(1000);
    emulator.start();
    auto screenText = [&] { return emulator.withMachine([](tuxape::Cpc& cpc) { return tuxape::readScreenText(cpc); }); };
    auto shows = [&](const char* text) {
        return QTest::qWaitFor([&] { return screenText().find(text) != std::string::npos; }, 20000);
    };
    CHECK(shows("BASIC 1.1"));
    CHECK(!emulator.machine().isPlus());

    QAction* load = actionNamed(window, "Load Cartridge...");
    CHECK(load && load->isEnabled());
    CHECK(load && load->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_F3));
    QToolButton* toolButton = nullptr;
    for (QToolButton* candidate : window.findChildren<QToolButton*>())
        if (candidate->toolTip() == "Change Cartridge (CTRL+F3)")
            toolButton = candidate;
    CHECK(toolButton && toolButton->isEnabled());

    // Not a cartridge: a word to the user, and the machine goes on.
    const QString notOne = folder.filePath("notes.cpr");
    {
        QFile file(notOne);
        CHECK(file.open(QIODevice::WriteOnly));
        file.write("RIFF....WAVE");
    }
    {
        Modals modals([](QWidget* modal) {
            CHECK(qobject_cast<QMessageBox*>(modal) != nullptr);
            modal->close();
        });
        CHECK(!window.insertCartridgeFile(notOne));
        CHECK_EQ(modals.seen(), 1);
        CHECK(!emulator.machine().isPlus());
    }

    // A cartridge makes the machine a Plus and starts it: here the system
    // cartridge's menu.
    CHECK(window.insertCartridgeFile(systemCartridge));
    CHECK(emulator.machine().isPlus());
    CHECK(emulator.crtcType() == tuxape::CrtcType::AsicPlus);
    CHECK(emulator.withMachine([](tuxape::Cpc& cpc) { return cpc.plus(); }));
    CHECK(shows("f1 Amstrad BASIC"));
    // It is in the settings for the next start.
    {
        Settings loaded;
        loaded.load();
        CHECK(loaded.machine.isPlus());
        CHECK(QDir::fromNativeSeparators(QString::fromStdString(loaded.machine.cartridge)) == systemCartridge);
        CHECK_EQ(loaded.crtcType, 3);
    }

    // Back to a CPC through the settings: the machine starts afresh.
    {
        Settings settings = window.settings();
        CHECK(settings.loadProfile(Settings::profileFolder() + "/CPC6128.wpf"));
        window.applySettings(settings);
        CHECK(!emulator.machine().isPlus());
        CHECK(!emulator.withMachine([](tuxape::Cpc& cpc) { return cpc.plus(); }));
        CHECK(shows("Amstrad 128K Microcomputer"));
    }

    // From the Library, as a file and inside an archive.
    const QString games = folder.filePath("carts");
    QDir().mkpath(games);
    const QString copy = games + "/Burnin' Rubber (1990)(Ocean).cpr";
    CHECK(QFile::copy(systemCartridge, copy));
    const QList<LibraryEntry> entries = scanLibrary({games});
    CHECK_EQ(entries.size(), 1);
    CHECK(entries.size() == 1 && entries[0].kind == LibraryEntry::Cartridge && entries[0].title == "Burnin' Rubber");
    Settings settings = window.settings();
    settings.libraryFolders = QStringList{games};
    window.applySettings(settings);
    {
        Modals modals([&](QWidget* modal) {
            auto* library = qobject_cast<LibraryDialog*>(modal);
            CHECK(library && library->listedTitles() == QStringList{"Burnin' Rubber"});
            if (!library)
                return modal->close(), void();
            CHECK(library->findChild<QPushButton*>("bInsertA")->text() == "&Insert");
            CHECK(!library->findChild<QPushButton*>("bInsertB")->isEnabled());
            CHECK(library->choose());
        });
        actionNamed(window, "Library...")->trigger();
        CHECK_EQ(modals.seen(), 1);
    }
    CHECK(emulator.machine().isPlus());
    CHECK(QDir::fromNativeSeparators(QString::fromStdString(emulator.machine().cartridge)) == copy);
    CHECK(shows("f2 Burnin' Rubber"));

    emulator.stop();
    return checkSummary("gui_plus");
}
