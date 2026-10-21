# Écrire un plugin pour TuxAPE

Un plugin ajoute à TuxAPE une fenêtre, un outil, un convertisseur : ce qu'on
veut, sans toucher au code de TuxAPE ni lui être connu à l'avance. TuxAPE le
trouve dans son dossier `plugins`, vérifie qui l'a signé, le charge, et ne lui
montre de lui-même qu'une seule chose : l'interface décrite dans
[`plugin.h`](plugin.h).

Les conventions de nommage, de dossiers, de langues et de compatibilité sont
dans [`PLUGINS.md`](../PLUGINS.md), à la racine de TuxAPE ; `template/` est un
plugin modèle, complet, traduit et testé, à copier pour en commencer un ; et
`TuxAPEPlugin.cmake` est le module CMake qui fait respecter ces conventions à
la compilation. Un plugin est un projet à lui, dans son propre dépôt : il ne
prend de TuxAPE que ce dossier.

Ce dossier `sdk/` est tout ce dont un plugin a besoin de TuxAPE pour être
compilé. Il ne faut rien inclure d'autre de TuxAPE : le reste peut changer
d'une version à l'autre, `plugin.h` non.

## Ce que voit un plugin

`plugin.h` déclare deux classes :

- `tuxape::Plugin`, que le plugin implémente : `start(PluginHost*, QString*)`
  quand il est chargé, `stop()` avant d'être déchargé ;
- `tuxape::PluginHost`, que TuxAPE lui donne : le menu **Plugins** et des
  boutons dans le panneau du bas, les disquettes et les instantanés, le clavier
  du CPC, sa mémoire et son écran, l'assembleur, les tuiles choisies dans
  **Find Graphics**, les fichiers des disquettes des lecteurs, les autres
  plugins chargés, et la langue des fenêtres de TuxAPE.

Ce que TuxAPE a ajouté pour un plugin (entrées de menu, boutons) disparaît
quand il est arrêté.

## Les versions de l'interface

`kPluginApi` est la version de `plugin.h` ; le `plugin.json` d'un plugin dit
celle pour laquelle il a été compilé. TuxAPE charge un plugin fait pour sa
version ou une plus ancienne (jusqu'à `kOldestPluginApi`), jamais pour une plus
récente.

Une version **ajoute** à `PluginHost`, à la fin, des fonctions qui ont une
réponse par défaut, et n'enlève rien : un plugin compilé pour une version plus
ancienne fonctionne toujours. Les structures que TuxAPE et les plugins
s'échangent (`PluginScreen`, `PluginTiles`, `PluginDisc`) ne changent **jamais**
une fois qu'une version les a : une version qui a plus à dire ajoute sa propre
structure. Le test `plugin_abi` de TuxAPE veille à cette règle.

## Le dossier d'un plugin

```
monplugin/plugin.json
monplugin/linux-x86_64/libmonplugin.so
monplugin/bin/...                 ses programmes, s'il en a
monplugin/LICENSE
monplugin/signature.json          sa signature, voir plus bas
```

Le dossier porte l'identifiant du plugin : minuscules, chiffres, `-` et `_`.
`tuxape_add_plugin()` écrit le `plugin.json` :

```json
{
    "id": "monplugin",
    "name": "Mon Plugin – Sprite Editor",
    "version": "1.0.0",
    "api": 4,
    "author": "Prénom Nom",
    "licence": "MIT",
    "description": "Ce qu'il fait, en une ou deux phrases.",
    "libraries": {
        "linux-x86_64": "linux-x86_64/libmonplugin.so"
    }
}
```

Une bibliothèque par type d'ordinateur, dans le dossier du plugin :
`linux-x86_64`, `linux-arm64`, `windows-x86_64`, `macos-arm64`...

## Le code

```cpp
#include <QObject>
#include <QTranslator>
#include "plugin.h"

namespace monplugin {

class MonPluginPlugin : public QObject, public tuxape::Plugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.tuxape.Plugin/1")
    Q_INTERFACES(tuxape::Plugin)

public:
    bool start(tuxape::PluginHost* host, QString* error) override
    {
        host_ = host;
        // Ses textes dans la langue de TuxAPE, avant le premier tr().
        if (translator_.load(QStringLiteral(":/i18n/monplugin/monplugin_%1.qm").arg(host->language())))
            QCoreApplication::installTranslator(&translator_);
        QAction* entry = host->addAction(tr("Mon Plugin – &Sprite Editor..."));
        connect(entry, &QAction::triggered, this, [this] { /* ... */ });
        return true;
    }
    void stop() override
    {
        QCoreApplication::removeTranslator(&translator_);
        host_ = nullptr;
    }

private:
    tuxape::PluginHost* host_ = nullptr;
    QTranslator translator_;
};

}  // namespace monplugin
```

TuxAPE n'accepte une bibliothèque que si elle déclare l'interface
`org.tuxape.Plugin/1`.

Pour offrir quelque chose aux autres plugins, on le déclare `Q_INVOKABLE` dans
cet objet : un autre plugin le trouve par `host->plugin("monplugin")` et
l'appelle par son nom avec `QMetaObject::invokeMethod`. Il faut s'attendre à ce
que l'autre ne soit pas chargé, ou soit d'une version qui n'a pas encore la
fonction.

## La compilation

Avec CMake, contre le Qt 6 de la version de TuxAPE visée, et le module du SDK
qui fait tout le reste :

```cmake
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets)
include(${TUXAPE_SDK}/TuxAPEPlugin.cmake)

add_library(monplugin MODULE monpluginplugin.cpp monpluginplugin.h)
target_link_libraries(monplugin PRIVATE monplugin_ui)

tuxape_add_plugin(monplugin
    ID monplugin  NAME "Mon Plugin – Sprite Editor"  VERSION ${PROJECT_VERSION}
    AUTHOR "Prénom Nom"  LICENCE "MIT"  API 4
    DESCRIPTION "Ce qu'il fait."
    FILES LICENSE
    TRANSLATIONS translations/monplugin_fr.ts  TRANSLATE_TARGETS monplugin_ui)
```

`tuxape_add_plugin()` vérifie l'identifiant, la version de l'interface et
celle de Qt, nomme la bibliothèque, y met les traductions, écrit le
`plugin.json`, signe et fait l'archive `dist/monplugin-1.0.0-linux-x86_64.zip`.

Un plugin tourne **dans** TuxAPE : s'il plante, TuxAPE plante avec lui.

## L'archive

Un plugin se distribue en `.zip` : son dossier, ou ce que contient son dossier.
On l'installe par **Settings ▸ Plugins ▸ Install from ZIP...**, ou en le
déposant sur cette page.

## La signature

Un plugin peut tout faire sur l'ordinateur de celui qui l'installe, comme
TuxAPE lui-même. C'est pourquoi TuxAPE regarde qui l'a fait :

- signé par un auteur à qui l'utilisateur fait confiance, il est installé et
  chargé sans autre question ;
- signé par un auteur inconnu, TuxAPE montre son nom et l'empreinte de sa clé,
  et demande s'il faut lui faire confiance ; si oui, tous les plugins de cet
  auteur le sont ensuite ;
- non signé, TuxAPE prévient et ne l'installe que si l'utilisateur insiste ;
  chaque nouvelle version lui est redemandée ;
- modifié après avoir été signé, il n'est jamais chargé.

À chaque démarrage, TuxAPE vérifie de nouveau les fichiers de chaque plugin.

La signature se fait avec `tuxape-plugin-sign`, que la compilation de TuxAPE
produit (`build/src/sign/tuxape-plugin-sign`) :

```sh
# Une fois : la paire de clés de l'auteur.
tuxape-plugin-sign key ~/.config/tuxape-signing/moi.key "Prénom Nom"

# À chaque version : tuxape_add_plugin() signe l'archive, si la clé est nommée.
cmake -S . -B build -DTUXAPE_SIGN_KEY=$HOME/.config/tuxape-signing/moi.key
cmake --build build

# Pour vérifier un dossier ou une archive :
tuxape-plugin-sign check dist/monplugin-1.0.0-linux-x86_64.zip
```

La clé ne sert que si elle est nommée (`TUXAPE_SIGN_KEY`, en option CMake ou en
variable d'environnement) : compiler le plugin de quelqu'un d'autre ne le signe
jamais à votre nom par mégarde.

La signature (`signature.json`) porte sur tous les autres fichiers du dossier :
l'ajout, le retrait ou la modification d'un seul octet la rend fausse. C'est une
signature Ed25519 ; les détails sont dans `src/sign/pluginsignature.h`, dans
les sources de TuxAPE.

Le fichier de clé secrète ne doit jamais être publié ni perdu : copié, il
permet à n'importe qui de signer au nom de l'auteur ; perdu, l'auteur doit
changer de clé et ses utilisateurs lui refaire confiance. Donnez à vos
utilisateurs l'empreinte de votre clé (affichée par `key` et par `check`), pour
qu'ils puissent vérifier que c'est bien la vôtre quand TuxAPE la leur montre.
