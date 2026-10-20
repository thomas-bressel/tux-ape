# Plugins

C'est ici que TuxAPE range ses plugins, un dossier par plugin :

```
plugins/tuxasc/plugin.json
plugins/tuxasc/linux-x86_64/libtuxasc.so
```

Deux façons d'en installer un :

- dans TuxAPE, **Settings ▸ Plugins ▸ Install from ZIP...**, en choisissant le
  fichier `.zip` téléchargé : il est décompressé ici et chargé aussitôt ;
- à la main, en décompressant le `.zip` dans ce dossier : le plugin est trouvé
  au prochain démarrage de TuxAPE.

Un plugin chargé ajoute ses entrées au menu **Plugins**. Sa case, dans
Settings ▸ Plugins, permet de ne plus le charger sans le supprimer.

Un autre dossier peut être indiqué avec la variable d'environnement
`TUXAPE_PLUGIN_DIR`.
