# Hello – le modèle d'un plugin pour TuxAPE

Un plugin minimal mais complet, à copier pour en commencer un :

- son moteur sans Qt (`src/engine/`) et son test, qui ne rend que des
  données ;
- sa fenêtre et sa classe que TuxAPE charge (`src/plugin/`), toutes dans
  `namespace hello` ;
- sa traduction française (`translations/hello_fr.ts`), chargée dans la langue
  de TuxAPE ;
- le test qui le charge comme TuxAPE, en anglais puis en français ;
- son archive, signée quand la clé de l'auteur est nommée.

Les conventions qu'il suit sont expliquées dans `PLUGINS.md`, à la racine de
TuxAPE. TuxAPE le compile et le teste lui-même (test `sdk_template`).

```sh
cmake --preset dev        # copié à côté du dossier de TuxAPE
# ou :  cmake -S . -B build -DTUXAPE_SDK=/chemin/TuxAPE/sdk
cmake --build build
ctest --preset dev
# l'archive : dist/hello-0.1.0-linux-x86_64.zip
```

Le preset `dev` cherche Qt là où le système l'a, ou dans le `.deps/` de
TuxAPE s'il y en a un (`tools/setup-qt-local.sh`).

Après un changement des textes :

```sh
cmake --build build --target hello_update_translations   # lupdate
linguist translations/hello_fr.ts                         # traduire ce qui est nouveau
cmake --build build
```
