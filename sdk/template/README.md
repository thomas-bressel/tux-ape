# Hello – le modèle d'un plugin pour TuxAPE

Un plugin minimal mais complet, à copier pour en commencer un : son moteur sans
Qt et son test, sa fenêtre, sa classe que TuxAPE charge, le test qui le charge
comme TuxAPE, son archive signée. Les conventions qu'il suit sont expliquées
dans `PLUGINS.md`, à la racine de TuxAPE.

```sh
cmake --preset dev        # ou : cmake -S . -B build -DTUXAPE_SDK=/chemin/TuxAPE/sdk
cmake --build build
ctest --preset dev
# l'archive : dist/hello-0.1.0-linux-x86_64.zip
```
