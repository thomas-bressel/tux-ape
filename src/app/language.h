#pragma once

#include <QList>
#include <QString>

// The languages TuxAPE's windows can be in: English, the language of its
// sources, and each translation there is of them. Translations are built
// in (src/app/translations/tuxape_<code>.ts), or a tuxape_<code>.qm put in
// the languages folder beside the program: a language is added without
// touching the code.
struct Language {
    QString code;  // "en", "fr"
    QString name;  // written in the language itself: "English", "Français"
};

// The language of the sources: no translation is needed for it.
inline constexpr char kSourceLanguage[] = "en";

// English first, then the others by name.
QList<Language> availableLanguages();

// Puts the texts of the application, and Qt's own (buttons, file
// windows), in that language. Returns false if there is no translation for
// it: the texts are then English. Windows made before keep their texts.
bool useLanguage(const QString& code);

// The language useLanguage() last put the texts in.
QString currentLanguage();
