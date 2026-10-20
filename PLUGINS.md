# Plugins pour TuxAPE : conventions

Ce guide fixe la façon d'écrire, de nommer, de ranger, de compiler et de
publier un plugin pour TuxAPE. Le suivre garantit qu'un plugin se charge dans
TuxAPE, qu'il ne gêne ni TuxAPE ni les autres plugins, et qu'il reste
compatible quand TuxAPE évolue.

Chaque règle ci-dessous correspond à ce que TuxAPE vérifie ou suppose.
Plusieurs sont contrôlées dès la compilation par `sdk/TuxAPEPlugin.cmake` ;
les autres sont signalées comme telles.

## Démarrer : copier le modèle

`sdk/template/` est un plugin complet et testé, nommé « hello » : un moteur
sans Qt et son test, une fenêtre, la classe que TuxAPE charge, un test qui le
charge comme TuxAPE, son archive signée. Pour commencer un plugin :

1. copier `sdk/template/` à côté du dossier de TuxAPE, sous le nom du projet
   (`MonPlugin/`) ;
2. remplacer partout `hello` par l'identifiant du plugin, `Hello` par son nom,
   y compris dans les noms de fichiers (`helloplugin.cpp` → `monpluginplugin.cpp`) ;
3. remplir l'appel `tuxape_add_plugin(...)` du `CMakeLists.txt` (auteur,
   licence, description, version d'API) et le fichier `LICENSE` ;
4. compiler et lancer les tests :

```sh
cmake --preset dev          # ou : cmake -S . -B build -DTUXAPE_SDK=/chemin/TuxAPE/sdk
cmake --build build
ctest --preset dev
```

L'archive à distribuer est alors dans `dist/`.

## Ce qui décide si TuxAPE charge un plugin

| Contrôle | Règle | Vérifié |
|---|---|---|
| Identifiant | `^[a-z0-9][a-z0-9_-]{0,31}$` : minuscules, chiffres, `-`, `_`, 32 au plus | à la compilation et au chargement |
| Dossier | porte exactement l'identifiant | au chargement |
| `api` du `plugin.json` | entre `kOldestPluginApi` et `kPluginApi` de `sdk/plugin.h` (aujourd'hui 1 à 3) | à la compilation et au chargement |
| Bibliothèque | une pour l'ordinateur de l'utilisateur, dans le dossier du plugin | au chargement |
| Interface | la bibliothèque déclare `org.tuxape.Plugin/1` | au chargement |
| Qt | celui de TuxAPE (6.10) ou un Qt 6 plus ancien, jamais plus récent | à la compilation (Qt refuse sinon) |
| Signature | auteur de confiance, ou plugin accepté par l'utilisateur ; jamais modifié depuis la signature | au chargement, à chaque démarrage |
| `start()` | renvoie `true` | au chargement |

Un plugin refusé apparaît dans **Settings ▸ Plugins**, avec la raison.

## Nommage

Dans les exemples, l'identifiant est `monplugin` et le nom « Mon Plugin ».

| Élément | Convention | Exemple |
|---|---|---|
| Identifiant | unique, définitif : il désigne le plugin partout | `monplugin` |
| Dossier installé | l'identifiant | `plugins/monplugin/` |
| Bibliothèque | l'identifiant, avec le préfixe et le suffixe du système (fait par `tuxape_add_plugin`) | `libmonplugin.so`, `monplugin.dll`, `libmonplugin.dylib` |
| Archive | `<id>-<version>-<plateforme>.zip` (fait par `tuxape_add_plugin`) | `monplugin-1.2.0-linux-x86_64.zip` |
| Version | `MAJEUR.MINEUR.CORRECTIF`, celle du `project()` CMake | `1.2.0` |
| Projet CMake | le nom du plugin, sans espace | `project(MonPlugin ...)` |
| Cibles CMake | `<id>` le module, `<id>_ui` les fenêtres, `<id>_engine` (ou `<id>_<partie>`) le reste, `<id>_cli` un programme | `monplugin`, `monplugin_ui` |
| Variables et options CMake | préfixe `<ID>_` en majuscules | `MONPLUGIN_BUILD_TESTS` |
| Classe chargée | `<Nom>Plugin` | `MonPluginPlugin` |
| Espace de noms C++ | l'identifiant, ou un nom à soi : jamais `tuxape` | `namespace monplugin` |
| Nom affiché (`name`) | « Nom – ce qu'il fait », avec un tiret demi-cadratin | `Mon Plugin – Sprite Editor` |
| Entrée du menu Plugins | le nom affiché, suivi de `...` s'il ouvre une fenêtre | `Mon Plugin – Sprite Editor...` |
| Titre des fenêtres | commence par le nom du plugin | `Mon Plugin – Export` |
| Noms d'objets Qt | uniques dans le plugin, préfixés par leur genre (`b` bouton, `l` étiquette, `ed` champ, `ck` case, `lv` liste) | `bExport`, `lStatus` |
| Plateforme | `<système>-<processeur>`, comme TuxAPE la nomme | `linux-x86_64`, `linux-arm64`, `windows-x86_64`, `macos-arm64` |

Identifiants déjà pris : `tuxasc`, `tuxdsk`, `tuxscr`, `tuxwav`, `tuxcmp`. Ne
publiez pas un plugin sous l'identifiant du modèle, `hello`.

## Le dossier des sources

```
MonPlugin/
├── CMakeLists.txt        le projet, et l'appel tuxape_add_plugin()
├── CMakePresets.json     la compilation « dev », avec le Qt de TuxAPE
├── LICENSE               la licence, qui va dans l'archive
├── README.md             ce que fait le plugin, comment le compiler
├── src/
│   ├── engine/           ce que fait le plugin, en C++ sans Qt ni TuxAPE : testable seul
│   ├── plugin/           les fenêtres (bibliothèque <id>_ui) et la classe chargée (module <id>)
│   └── cli/              facultatif : un programme en ligne de commande, qui va dans bin/
├── tests/
│   ├── check.h           les vérifications des tests
│   ├── host.h            un TuxAPE de test : toutes les fonctions de PluginHost
│   ├── engine_*.cpp      les tests du moteur
│   └── plugin_window.cpp le plugin chargé comme TuxAPE le charge
├── tools/                facultatif : les outils du développement
├── build/                la compilation (pas dans le dépôt)
└── dist/                 les archives (pas dans le dépôt)
```

Trois règles d'architecture :

- **Le moteur ne connaît ni Qt ni TuxAPE.** Il se teste seul, et sert aussi au
  programme en ligne de commande.
- **Les fenêtres sont une bibliothèque statique** (`<id>_ui`), que les tests
  pilotent sans TuxAPE.
- **Le module chargé ne contient que la classe du plugin.** Elle ajoute les
  entrées et les boutons, ouvre les fenêtres et range tout dans `stop()`.

## Le dossier installé (le contenu de l'archive)

```
monplugin/
├── plugin.json                    sa description (écrit par tuxape_add_plugin)
├── linux-x86_64/libmonplugin.so   une bibliothèque par plateforme
├── bin/monplugin                  facultatif : ses programmes, rendus exécutables à l'installation
├── LICENSE
└── signature.json                 sa signature, ajoutée en dernier
```

Une archive peut contenir ce dossier, ou ce qu'il contient : TuxAPE accepte
les deux, mais un seul plugin par archive. Ses chemins restent dans le dossier
(pas de `..`, pas de chemin absolu). Une nouvelle version installée remplace
entièrement l'ancienne.

**Un plugin n'écrit jamais dans son propre dossier.** Un fichier ajouté ou
modifié rend sa signature fausse, et TuxAPE ne le charge plus au démarrage
suivant.

## `plugin.json`

`tuxape_add_plugin()` l'écrit à partir de ses arguments :

| Champ | Contenu |
|---|---|
| `id` | l'identifiant |
| `name` | le nom affiché |
| `version` | la version du plugin |
| `api` | la version de l'interface pour laquelle il est compilé (voir plus bas) |
| `author` | l'auteur |
| `licence` | la licence (identifiant SPDX de préférence : `MIT`, `GPL-2.0`, `BSD-3-Clause`...) |
| `description` | ce qu'il fait, en une ou deux phrases |
| `libraries` | pour chaque plateforme, le chemin de sa bibliothèque dans le dossier |

## Compatibilité avec TuxAPE

### La version de l'interface, pas celle de TuxAPE

Le seul numéro qui compte pour la compatibilité est la version de l'interface
des plugins, `kPluginApi` dans `sdk/plugin.h` ; le numéro de version de
TuxAPE n'en dit rien. Un TuxAPE charge les plugins faits pour sa version de
l'interface ou une plus ancienne, jusqu'à `kOldestPluginApi`, jamais ceux
faits pour une plus récente.

| API | Ajoute |
|---|---|
| 1 | le menu Plugins, les boutons, les disquettes et les instantanés, le clavier, la mémoire et l'écran du CPC, l'assembleur, un dossier de données |
| 2 | une entrée dans le menu des tuiles de Find Graphics, et les tuiles choisies |
| 3 | les fichiers des disquettes des lecteurs, et ceux qu'on y pose ; les autres plugins chargés |

Les règles :

- **Déclarez la plus petite version dont vous utilisez une fonction**, pas la
  dernière : votre plugin fonctionnera aussi dans les TuxAPE plus anciens.
- **N'appelez jamais une fonction d'une version plus récente que celle
  déclarée** : un TuxAPE plus ancien ne l'a pas, et le plugin planterait. Le
  numéro « Since version N » de `plugin.h` indique la version de chaque
  fonction.
- Compilez toujours contre le `sdk/` d'un TuxAPE récent : il connaît toutes les
  versions précédentes.

### Ce qui ne change jamais

- `sdk/plugin.h` n'enlève et ne déplace jamais rien : une version ajoute des
  fonctions à la fin de `PluginHost`.
- Les structures échangées par valeur (`PluginScreen`, `PluginTiles`,
  `PluginDisc`) ne changent plus une fois publiées. Le test `plugin_abi` de
  TuxAPE le vérifie.
- L'interface déclarée est toujours exactement `org.tuxape.Plugin/1`.

### Ce que le plugin doit respecter

- **N'incluez de TuxAPE que `sdk/plugin.h`.** Le reste de TuxAPE peut changer
  à tout moment. Ne donnez pas à vos propres fichiers le nom d'un fichier de
  TuxAPE.
- **Qt :** le Qt 6 de TuxAPE (aujourd'hui 6.10) ou un plus ancien, jamais un
  plus récent, que Qt refuserait de charger. `TuxAPEPlugin.cmake` l'interdit
  à la compilation.
- **Compilation en Release**, avec le compilateur habituel du système : GCC et
  libstdc++ sous Linux, MSVC sous Windows, Clang sous macOS.
- **C++20**, en n'échangeant avec TuxAPE que les types de `plugin.h` : types de
  Qt et structures du SDK, jamais de types de la bibliothèque standard.

## Le cycle de vie

- `start(host, error)` est appelé une fois, au chargement. Le plugin y ajoute
  ses entrées (`addAction`) et ses boutons (`addButton`). S'il ne peut pas
  fonctionner, il écrit la raison dans `*error` et renvoie `false` : il est
  alors déchargé.
- `stop()` est appelé avant le déchargement, à la fermeture de TuxAPE, quand
  l'utilisateur décoche le plugin ou en installe une nouvelle version. Le
  plugin y ferme et supprime ses fenêtres, arrête ses minuteries et ses fils
  d'exécution, et retire ses filtres d'événements. **Rien ne doit lui survivre** :
  la bibliothèque est déchargée juste après.
- Les entrées et les boutons appartiennent à TuxAPE, qui les retire. Ne les
  supprimez pas, et ne gardez pas de pointeur vers eux après `stop()`.
- N'utilisez pas de variables globales ou statiques qui gardent un état d'un
  chargement à l'autre.
- Un plugin tourne dans TuxAPE : s'il plante, TuxAPE plante avec lui.
  Vérifiez ce que vous lisez (fichiers, mémoire du CPC) avant de vous en
  servir.

## Le clavier

Toutes les touches qui ne sont pas des touches de fonction appartiennent au
CPC. Un raccourci comme Ctrl+lettre ne marcherait pas quand l'écran du CPC a
le clavier, et priverait un jeu de cette touche quand il ne l'a pas.

- **Par défaut, pas de raccourci** : l'entrée du menu Plugins et le bouton
  suffisent.
- S'il en faut un, uniquement **Ctrl+Maj+F4 à F8, ou Ctrl+Maj+F10 à F12** : ce
  sont les touches que TuxAPE laisse libres. Vérifiez qu'un autre plugin
  installé ne les a pas prises.
- Dans vos propres fenêtres, les touches sont à vous.

## L'apparence

- **Suivez le thème, clair ou sombre.** Dessinez l'image du bouton avec les
  couleurs de la palette de l'application, et redonnez-la par
  `addButton(action, icône)` quand la palette change
  (`QEvent::ApplicationPaletteChange`), comme le fait le modèle.
- **Contraste :** TuxAPE vise le niveau AAA des WCAG, 7:1 pour le texte et
  3:1 pour le reste. Un état ne se montre jamais par la seule couleur.
- **Textes de l'interface en anglais**, comme ceux de TuxAPE, avec `&` pour la
  lettre de navigation. Utilisez `tr()`.

## Les données du plugin

- Réglages, historique, fichiers de travail : dans `host->dataFolder()`, un
  dossier propre au plugin que TuxAPE crée (`~/.config/TuxAPE/Plugins/<id>/`
  sous Linux).
- Jamais dans le dossier du plugin (il fait partie de la signature), jamais
  dans les réglages de TuxAPE, et pas de `QSettings` aux noms de TuxAPE.
- Les fichiers de l'utilisateur (images, disquettes produites) vont là où il
  les choisit.

## Communiquer avec un autre plugin

Depuis l'API 3, `host->plugin("autreplugin")` donne l'objet chargé d'un autre
plugin, ou `nullptr` s'il n'est pas là.

- Ce qu'un plugin offre aux autres, ce sont des fonctions `Q_INVOKABLE` de sa
  classe chargée, avec des types Qt simples (`QString`, `QByteArray`, `int`,
  `bool`...). Elles s'appellent par leur nom avec `QMetaObject::invokeMethod`.
- **Une fonction offerte ne change jamais** : ni son nom, ni ses paramètres,
  ni son sens. Pour faire autrement, ajoutez-en une autre.
- Celui qui appelle vérifie le retour de `invokeMethod`. L'autre plugin peut
  être absent, désactivé, ou d'une version qui n'a pas la fonction : dites-le
  à l'utilisateur au lieu de planter.
- Ne gardez pas l'objet obtenu : redemandez-le à chaque fois, car il disparaît
  quand l'autre plugin s'arrête.

## Signer et publier

Un plugin peut tout faire sur l'ordinateur de celui qui l'installe. TuxAPE
regarde donc qui l'a signé :

| Le plugin est | TuxAPE |
|---|---|
| signé par un auteur de confiance | l'installe et le charge sans question |
| signé par un auteur inconnu | montre son nom et l'empreinte de sa clé, et demande s'il faut lui faire confiance |
| non signé | prévient, avec Annuler par défaut, et redemande à chaque nouvelle version |
| modifié après la signature | ne le charge jamais |

`tuxape-plugin-sign` est produit par la compilation de TuxAPE
(`build/src/sign/tuxape-plugin-sign`) :

```sh
# Une fois : votre paire de clés, à garder pour vous et à sauvegarder.
tuxape-plugin-sign key ~/.config/tuxape-signing/moi.key "Prénom Nom"

# À chaque version : tuxape_add_plugin() signe l'archive si la clé est désignée.
cmake -S . -B build -DTUXAPE_SIGN_KEY=$HOME/.config/tuxape-signing/moi.key
cmake --build build

# Vérifier une archive avant de la publier.
tuxape-plugin-sign check dist/monplugin-1.2.0-linux-x86_64.zip
```

- La clé n'est utilisée que si elle est désignée (`TUXAPE_SIGN_KEY`, en option
  CMake ou en variable d'environnement). Compiler le plugin de quelqu'un
  d'autre ne le signe donc jamais à votre nom par mégarde.
- Publiez l'empreinte de votre clé (affichée par `key` et par `check`) là où
  vous publiez le plugin : vos utilisateurs la comparent à celle que TuxAPE
  leur montre.
- Ne publiez jamais le fichier de clé secrète. Copié, il permet de signer à
  votre nom ; perdu, il vous oblige à changer de clé, et vos utilisateurs
  doivent vous refaire confiance.

## Tester

- **Le moteur**, seul (`tests/engine_*.cpp`).
- **Le plugin chargé comme TuxAPE le charge** (`tests/plugin_window.cpp`) :
  `QPluginLoader`, l'interface `org.tuxape.Plugin/1`, `start()` avec l'hôte de
  test (`tests/host.h`), les entrées, la fenêtre, `stop()`, puis le
  déchargement.
- **Dans un vrai TuxAPE**, sans toucher à vos plugins installés : décompressez
  l'archive dans un dossier vide et lancez TuxAPE avec
  `TUXAPE_PLUGIN_DIR=/ce/dossier` ; ou installez-la par **Settings ▸ Plugins
  ▸ Install from ZIP...**.
- Les tests ne doivent jamais écrire dans les réglages de l'utilisateur.

## Liste de contrôle avant de publier

- [ ] L'identifiant est unique, en minuscules, et n'est pas `hello`.
- [ ] `api` est la plus petite version dont le plugin utilise une fonction.
- [ ] Compilé en Release, avec le Qt de TuxAPE ou un plus ancien.
- [ ] Rien d'autre de TuxAPE que `sdk/plugin.h` n'est inclus.
- [ ] `stop()` laisse tout propre : fenêtres, minuteries, fils d'exécution, filtres.
- [ ] Pas de raccourci, ou seulement Ctrl+Maj+F4 à F8 ou F10 à F12.
- [ ] Le bouton suit le thème clair et sombre ; les contrastes sont AAA.
- [ ] Les données vont dans `dataFolder()`, jamais dans le dossier du plugin.
- [ ] Les fonctions offertes aux autres plugins n'ont pas changé.
- [ ] `ctest` passe.
- [ ] L'archive est signée, et `tuxape-plugin-sign check` le confirme.
- [ ] L'archive s'installe et se charge dans un vrai TuxAPE.
- [ ] Le `README.md` et le `LICENSE` sont dans le dépôt ; l'empreinte de la clé est publiée.

Pour aller plus loin : `sdk/plugin.h` (le contrat, fonction par fonction),
`sdk/README.md` (pas à pas), `sdk/template/` (le modèle).
