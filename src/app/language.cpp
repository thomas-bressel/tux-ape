#include "language.h"

#include <algorithm>
#include <memory>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

namespace {

constexpr char kBuiltIn[] = ":/i18n";
constexpr char kPrefix[] = "tuxape_";

// Each translation says what its language is called, in that language: the
// text below, translated. The translator of a new language writes its
// name there.
struct Text {
    const char* source;
    const char* comment;
};
constexpr char kNameContext[] = "Language";
constexpr Text kName = QT_TRANSLATE_NOOP3("Language", "English", "the name of this language, written in it");

QString current = kSourceLanguage;
std::unique_ptr<QTranslator> appTranslator;
std::unique_ptr<QTranslator> qtTranslator;

// Beside the program: translations added without building TuxAPE again.
QString languagesFolder()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath("languages");
}

// The translation of a language, from the languages folder first, so that
// a newer one put there wins over the one built in.
QString translationFile(const QString& code)
{
    for (const QString& folder : {languagesFolder(), QString(kBuiltIn)}) {
        const QString file = QDir(folder).filePath(kPrefix + code + ".qm");
        if (QFileInfo::exists(file))
            return file;
    }
    return {};
}

QString languageName(const QString& code, const QString& file)
{
    QTranslator translator;
    if (translator.load(file)) {
        const QString name = translator.translate(kNameContext, kName.source, kName.comment);
        if (!name.isEmpty())
            return name;
    }
    // A translation that does not name its language: the system's name of it.
    const QString name = QLocale(code).nativeLanguageName();
    return name.isEmpty() ? code : name.left(1).toUpper() + name.mid(1);
}

}  // namespace

QList<Language> availableLanguages()
{
    QStringList codes;
    for (const QString& folder : {QString(kBuiltIn), languagesFolder()})
        for (const QString& file : QDir(folder).entryList({QString(kPrefix) + "*.qm"}, QDir::Files))
            codes << QFileInfo(file).completeBaseName().mid(int(sizeof kPrefix) - 1);
    codes.removeDuplicates();
    codes.removeAll(kSourceLanguage);

    QList<Language> languages;
    for (const QString& code : codes)
        languages.append({code, languageName(code, translationFile(code))});
    std::sort(languages.begin(), languages.end(),
              [](const Language& a, const Language& b) { return a.name.localeAwareCompare(b.name) < 0; });
    languages.prepend({kSourceLanguage, kName.source});
    return languages;
}

bool useLanguage(const QString& code)
{
    for (auto* translator : {&appTranslator, &qtTranslator}) {
        if (*translator)
            QCoreApplication::removeTranslator(translator->get());
        translator->reset();
    }
    current = kSourceLanguage;
    const QString file = translationFile(code);
    // The language of the sources: its plurals alone, if it has them.
    if (code == kSourceLanguage) {
        auto plurals = std::make_unique<QTranslator>();
        if (!file.isEmpty() && plurals->load(file)) {
            QCoreApplication::installTranslator(plurals.get());
            appTranslator = std::move(plurals);
        }
        return true;
    }
    auto app = std::make_unique<QTranslator>();
    if (file.isEmpty() || !app->load(file))
        return false;
    QCoreApplication::installTranslator(app.get());
    appTranslator = std::move(app);
    // Qt's own texts, as the system has them: OK, Cancel, the file windows.
    auto qt = std::make_unique<QTranslator>();
    if (qt->load(QLocale(code), "qtbase", "_", QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        QCoreApplication::installTranslator(qt.get());
        qtTranslator = std::move(qt);
    }
    current = code;
    return true;
}

QString currentLanguage()
{
    return current;
}
