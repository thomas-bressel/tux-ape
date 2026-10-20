# Écrire un plugin pour TuxAPE

Un plugin ajoute à TuxAPE une fenêtre, un outil, un convertisseur : ce qu'on
veut, sans toucher au code de TuxAPE ni lui être connu à l'avance. TuxAPE le
trouve dans son dossier `plugins`, vérifie qui l'a signé, le charge, et ne lui
montre de lui-même qu'une seule chose : l'interface décrite dans
[`plugin.h`](plugin.h).

Les conventions de nommage, de dossiers et de compatibilité sont dans
[`PLUGINS.md`](../PLUGINS.md), à la racine de TuxAPE ; `template/` est un plugin
modèle, complet et testé, à copier pour en commencer un, et `TuxAPEPlugin.cmake`
le module CMake qui fait respecter ces conventions à la compilation.

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
  **Find Graphics**, les fichiers des disquettes des lecteurs, et les autres
  plugins chargés.

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
monplugin/signature.json          (sa signature, voir plus bas)
monplugin/...                     ce qu'il emporte d'autre ; bin/ pour des programmes
```

Le dossier porte l'identifiant du plugin : minuscules, chiffres, `-` et `_`.
Son `plugin.json` :

```json
{
    "id": "monplugin",
    "name": "Mon Plugin",
    "version": "1.0",
    "author": "Prénom Nom",
    "licence": "MIT",
    "description": "Ce qu'il fait, en une ou deux phrases.",
    "api": 3,
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
#include "plugin.h"

class MonPlugin : public QObject, public tuxape::Plugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.tuxape.Plugin/1")
    Q_INTERFACES(tuxape::Plugin)

public:
    bool start(tuxape::PluginHost* host, QString* error) override
    {
        host_ = host;
        QAction* entry = host->addAction(tr("&Mon Plugin..."));
        connect(entry, &QAction::triggered, this, [this] { /* ... */ });
        return true;
    }
    void stop() override { host_ = nullptr; }

private:
    tuxape::PluginHost* host_ = nullptr;
};
```

TuxAPE n'accepte une bibliothèque que si elle déclare l'interface
`org.tuxape.Plugin/1`.

Pour offrir quelque chose aux autres plugins, on le déclare `Q_INVOKABLE` dans
cet objet : un autre plugin le trouve par `host->plugin("monplugin")` et
l'appelle par son nom avec `QMetaObject::invokeMethod`. Il faut s'attendre à ce
que l'autre ne soit pas chargé, ou soit d'une version qui n'a pas encore la
fonction.

## La compilation

Avec CMake, contre le Qt 6 de la version de TuxAPE visée :

```cmake
find_package(Qt6 REQUIRED COMPONENTS Widgets)
add_library(monplugin MODULE monplugin.cpp)
set_target_properties(monplugin PROPERTIES AUTOMOC ON)
target_include_directories(monplugin PRIVATE /chemin/vers/TuxAPE/sdk)
target_link_libraries(monplugin PRIVATE Qt6::Widgets)
```

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

# À chaque version, en dernier, juste avant de mettre le dossier dans le .zip :
tuxape-plugin-sign sign build/stage/monplugin ~/.config/tuxape-signing/moi.key

# Pour vérifier un dossier ou une archive :
tuxape-plugin-sign check dist/monplugin-1.0-linux-x86_64.zip
```

La signature (`signature.json`) porte sur tous les autres fichiers du dossier :
l'ajout, le retrait ou la modification d'un seul octet la rend fausse. C'est une
signature Ed25519 ; les détails sont dans `src/sign/pluginsignature.h`.

Le fichier de clé secrète ne doit jamais être publié ni perdu : copié, il
permet à n'importe qui de signer au nom de l'auteur ; perdu, l'auteur doit
changer de clé et ses utilisateurs lui refaire confiance. Donnez à vos
utilisateurs l'empreinte de votre clé (affichée par `key` et par `check`), pour
qu'ils puissent vérifier que c'est bien la vôtre quand TuxAPE la leur montre.
