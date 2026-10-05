# TuxAPE

Émulateur Amstrad CPC / Plus multiplateforme, conçu comme une réplique de
[WinAPE](http://www.winape.net) 2.0 Beta 2 : mêmes fonctions, mêmes formats de
fichiers, même organisation de l'interface, avec une émulation plus précise.

Le projet est écrit en C++20. L'interface utilise Qt 6, le son et les manettes
passent par SDL2. Le cœur d'émulation (`src/core`) ne dépend ni de Qt ni du
système : il se compile et se teste seul.

## Compiler

Dépendances sous Debian / Ubuntu :

```sh
sudo apt install build-essential cmake qt6-base-dev libsdl2-dev
```

Puis :

```sh
cmake --preset dev
cmake --build build -j
./build/src/app/tuxape
```

Sans droits administrateur, `tools/setup-qt-local.sh` récupère les fichiers de
développement Qt dans `.deps/`, où le preset `dev` les trouve.

## ROMs

TuxAPE ne contient aucune ROM. Il lit celles de WinAPE : placez le dossier
`WinAPE20B2` de la distribution d'origine à la racine du dépôt, ou indiquez un
autre dossier avec la variable d'environnement `TUXAPE_ROM_DIR`.

## Tests

```sh
ctest --preset dev
```

## Organisation

| Dossier | Contenu |
|---|---|
| `src/core` | émulation : Z80, Gate Array, CRTC, PPI, AY, FDC, formats de fichiers |
| `src/app` | application Qt : fenêtres, affichage, son, clavier |
| `tests` | programmes de test du cœur |
| `tools` | scripts d'aide au développement |
