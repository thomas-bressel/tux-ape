// The languages of the windows: English by default, French from the
// translation built in, chosen from Settings > Language; and the TuxAPE
// that starts again in the new language, the machine as it was. Runs
// without a display (QT_QPA_PLATFORM=offscreen) and without a ROM.

#include <QAction>
#include <QApplication>
#include <QFile>
#include <QMenu>
#include <QMenuBar>
#include <QTemporaryDir>

#include "check.h"
#include "core/cpc.h"
#include "core/disc.h"
#include "core/setup.h"
#include "discmanager.h"
#include "emulator.h"
#include "language.h"
#include "mainwindow.h"
#include "settings.h"

namespace {

QMenu* menuNamed(QMenu* parent, const QString& text)
{
    for (QAction* action : parent->actions())
        if (action->menu() && action->text() == text)
            return action->menu();
    return nullptr;
}

QMenu* topMenu(MainWindow& window, int index)
{
    const QList<QAction*> menus = window.menuBar()->actions();
    return index < menus.size() ? menus[index]->menu() : nullptr;
}

uint8_t peek(Emulator& emulator, uint16_t address)
{
    return emulator.withMachine([&](tuxape::Cpc& cpc) { return cpc.memory().readRam(address); });
}

}  // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QTemporaryDir folder;
    Settings::setFile(folder.filePath("TuxAPE.ini"));

    // English, the language of the sources, comes first; French is built in
    // and says its own name.
    const QList<Language> languages = availableLanguages();
    CHECK(languages.size() >= 2);
    CHECK(languages.value(0).code == "en" && languages.value(0).name == "English");
    bool french = false;
    for (const Language& language : languages)
        french |= language.code == "fr" && language.name == QString::fromUtf8("Français");
    CHECK(french);

    // English by default, in the settings as in the texts.
    CHECK(Settings().language == "en");
    CHECK(currentLanguage() == "en");
    CHECK(QCoreApplication::translate("MainWindow", "&File") == "&File");
    // English once only, though it has a file of its own: its plurals,
    // "1 file", "3 files", where the sources say "file(s)".
    int englishes = 0;
    for (const Language& language : languages)
        englishes += language.code == "en";
    CHECK_EQ(englishes, 1);
    CHECK(useLanguage("en"));
    CHECK(QCoreApplication::translate("DiscEditorDialog", "Delete %n file(s)?", nullptr, 1) == "Delete 1 file?");
    CHECK(QCoreApplication::translate("DiscEditorDialog", "Delete %n file(s)?", nullptr, 3) == "Delete 3 files?");
    CHECK(QCoreApplication::translate("MainWindow", "&File") == "&File");

    // In French: TuxAPE's texts, and Qt's own as the system has them.
    CHECK(useLanguage("fr"));
    CHECK(currentLanguage() == "fr");
    CHECK(QCoreApplication::translate("MainWindow", "&File") == "&Fichier");
    CHECK(QCoreApplication::translate("MainWindow", "&Pause") == "&Pause");
    CHECK(QCoreApplication::translate("DiscEditorDialog", "Delete %n file(s)?", nullptr, 2)
          == QString::fromUtf8("Supprimer 2 fichiers ?"));
    {
        Emulator emulator;
        emulator.setupMachine(tuxape::stockMachine(tuxape::CpcModel::Cpc6128), true);
        Settings settings;
        settings.language = "fr";
        MainWindow window(&emulator);
        window.applySettings(settings);
        QMenu* file = topMenu(window, 0);
        CHECK(file && file->title() == "&Fichier");
        QMenu* settingsMenu = topMenu(window, 1);
        CHECK(settingsMenu && settingsMenu->title() == QString::fromUtf8("&Réglages"));
        QMenu* languageMenu = settingsMenu ? menuNamed(settingsMenu, "&Langue") : nullptr;
        CHECK(languageMenu != nullptr);
        if (languageMenu) {
            // Each language by its own name, the one in use ticked.
            QStringList names;
            for (QAction* entry : languageMenu->actions()) {
                names << entry->text();
                CHECK(entry->isCheckable() && entry->shortcut().isEmpty());
                CHECK(entry->isChecked() == (entry->text() == QString::fromUtf8("Français")));
            }
            CHECK(names.contains("English") && names.contains(QString::fromUtf8("Français")));
        }
    }

    // A language there is no translation for: the texts stay English.
    CHECK(!useLanguage("xx"));
    CHECK(currentLanguage() == "en");
    CHECK(QCoreApplication::translate("MainWindow", "&File") == "&File");

    // The language is a setting kept for the next start.
    {
        Settings settings;
        settings.language = "fr";
        CHECK(settings.save());
        Settings loaded;
        loaded.load();
        CHECK(loaded.language == "fr");
        loaded.language = "en";
        CHECK(loaded.save());
    }

    // Starting again: the next TuxAPE is given the machine's state, the
    // discs in the drives; it puts the discs back and takes the state.
    {
        Emulator emulator;
        emulator.setupMachine(tuxape::stockMachine(tuxape::CpcModel::Cpc6128), true);
        MainWindow window(&emulator);
        const QString disc = folder.filePath("blank.dsk");
        CHECK(window.discs()->createBlank(1, disc, tuxape::discFormats().front()).isEmpty());
        emulator.withMachine([](tuxape::Cpc& cpc) { cpc.memory().write(0x9000, 0x42); });

        const QStringList arguments = window.startAgainArguments();
        const qsizetype resume = arguments.indexOf("--resume");
        CHECK(resume >= 0 && resume + 1 < arguments.size());
        const QString state = arguments.value(resume + 1);
        CHECK(QFile::exists(state));
        CHECK(arguments.contains("--insert") && arguments.contains("B:" + disc));
        CHECK(!arguments.contains("--tape"));
        // Leaving was not asked for: nothing is waiting to start.
        CHECK(window.restartArguments().isEmpty());

        emulator.withMachine([](tuxape::Cpc& cpc) { cpc.memory().write(0x9000, 0x00); });
        CHECK_EQ(peek(emulator, 0x9000), 0x00);
        CHECK(window.resumeFrom(state));
        CHECK_EQ(peek(emulator, 0x9000), 0x42);
        // The state was TuxAPE's own, to carry over: it is gone.
        CHECK(!QFile::exists(state));
    }

    return checkSummary("gui_language");
}
