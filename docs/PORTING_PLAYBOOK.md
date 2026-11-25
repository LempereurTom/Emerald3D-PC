# Guide de portage : d'un port 3DS / décompilation GBA à un exe PC natif

Ce guide résume comment Emerald PC a été fait (novembre 2026, jalons J0 → J10) pour refaire le
même travail sur un autre jeu, en premier lieu **Pokémon Rouge Feu / Vert Feuille**. Il dit ce
qui a marché, dans quel ordre, ce qui a coûté du temps, et ce qui se transpose ou non.
L'historique détaillé est dans [PC_PORT_STATUS.md](PC_PORT_STATUS.md), l'architecture du code
dans [../pc_port/README.md](../pc_port/README.md).

## 1. Le résultat et ses principes

Emerald PC est un **port source** (pas un émulateur, pas une recompilation) : l'arbre du jeu
d'[Emerald 3DS](https://github.com/ZallaxDev/emerald-3ds) (la décompilation pret/pokeemerald +
la couche 3DS de Zallax) compilé pour PC, avec une couche qui imite ce que la 3DS fournit.

Principes tenus du début à la fin :
- **Aucun contenu du jeu** dans le dépôt ni dans les releases (ni ROM, ni pack, ni données
  extraites). Le jeu reconstruit ses données chez le joueur depuis **sa** ROM.
- **Rester rebasable** sur l'amont : tout le PC est dans `pc_port/` ; les fichiers partagés ne
  changent que sous `#ifdef PORT_PC` (ou par une variante qui laisse la branche d'origine
  identique). Les correctifs de CI pris à l'amont sont repris **à l'identique**.
- **Ne pas réécrire ce qui marche** : le moteur 3DS (`3ds_video.c`, `ctr_voxel.c`, l'interface
  de l'écran du bas) tourne tel quel ; c'est la couche du dessous (libctru, citro3d, citro2d)
  qui est imitée (décision D1 : « shim citro sur OpenGL »).
- **Sauvegardes identiques à la console** : le même `emerald3ds.sav` marche sur 3DS et sur PC
  (test automatique des tailles et décalages de structures, 152/152).
- **Licence** CC BY-NC-SA 4.0 : crédit à l'amont dans le README et dans un écran À propos.

## 2. L'architecture

| Couche | Ce qui a été fait |
|---|---|
| Construction | `pc_port/pc.mk`, surcouche du Makefile 3DS (variables écrasées par `override`), arbre séparé (`tools/bootstrap.py --dir build/upstream-pc`). Windows i686, MinGW (MSYS2 portable). Pas de CMake. |
| libctru | « libctru-lite » (`pc_port/compat/3ds.h`, `src/pc_ctru.c`) : APT, HID, threads, fichiers, horloge. |
| Chemins | `pc_paths.h` forcé partout : `sdmc:/3ds/...` → dossier utilisateur, `romfs:/` → `engine/` à côté de l'exe. Appels larges UTF-8 sous Windows. |
| GPU | Shim citro3d/citro2d sur OpenGL 3.3 (`src/gl/pc_c3d*.c`) : surfaces en mémoire CPU au format PICA, texture GL miroir, exécution différée comme la liste de commandes de la console, shaders PICA réécrits en GLSL (`pc_shaders.c`), combineurs émulés dans un seul fragment shader. |
| Fenêtre | SDL3 (`src/gl/pc_display.c`) : dispositions des deux écrans, FUSION (un seul écran + interface), plein écran, filtrage net, couleurs, particules, menu Windows. |
| Son | NDSP sur SDL3 (`src/pc_ndsp.c`) à la cadence de la console (59,8261 Hz), filtre des aigus. |
| Entrées | Clavier remappable, souris = stylet, manettes SDL (`src/pc_input.c`). |
| Jeu | Les unités du jeu compilées telles quelles ; options PC ajoutées par les points d'extension de l'amont (`pc_extras*.inc`, `pc_options.inc`). |
| Release | Données du jeu sorties de l'exe (`tools/pc_release.py`), recette (`tools/gen_recipe.py` + `pc_nm.py`), builder (`tools/pc_builder.py`, gelé), premier lancement (`src/pc_setup.c`), zip audité (`tools/pc_build_release.py`). |

## 3. La méthode, jalon par jalon

Un jalon = un résultat vérifiable, vérifié par capture ou par test avant de passer au suivant.

| Jalon | Contenu | Critère de fin |
|---|---|---|
| J0 | Inventaire : ce que l'amont utilise de la 3DS (fonctions libctru/citro, fichiers natifs, formats). | `PC_PORT_INVENTORY.md` |
| J1 | Compile, linke, démarre (stubs partout). | l'exe arrive à `AgbMain` |
| J2 | Écran titre + 2D : le shim GPU. | capture de l'écran titre identique |
| J3 | Son + cadence. | musique sans craquement, 59,83 images/s |
| J4 | Écran du bas + tactile, dispositions. | clics et touches F9/Tab/F11 |
| J5 | Monde voxel. | Route 101 → Rosyère sans trou |
| J6 | Sauvegardes, horloge, réglages. | save 3DS ↔ PC, horloge après 2038 |
| J7 | Langues. | une ROM par langue, un exe par langue |
| J8 | Manettes, options PC, crédits. | écrans d'options, À propos |
| J9 | Release sans contenu du jeu. | zip audité contre la ROM, essai de bout en bout |
| J10 | RecompLauncher, fork, CI. | manifeste validé, CI verte, release publiée |

Outils de vérification qui ont tout rendu possible (à recréer d'abord) :
- **Scénarios** rejoués (`pc_port/tools/make_scenario.py` → `scenario.txt`) : touches, attentes,
  toucher, sauvegarde de départ, options (`voxel=1`, `audio_clock=0`).
- **Captures** (`EMERALD_PC_DUMP`, `_EVERY`, `_FROM`, `_EFFECTS`, `_LAYOUTS`) et exécution sans
  fenêtre ni cadence (`EMERALD_PC_HIDDEN`, `EMERALD_PC_NOPACE`, `EMERALD_PC_FRAMES`).
- **Rapport de plantage** (`crash.txt`, adresses pour `addr2line`).
- **Export texte** de ce qu'on ne voit pas sur les captures (`EMERALD_PC_MENU_DUMP` pour le menu).

## 4. La release sans contenu du jeu (le plus délicat)

1. **Regrouper** : `pc_group_sections.sh` renomme (objcopy) les sections en lecture seule des
   objets du jeu (`.rdata`, `.rodata`, `script_data`) en `.rdata$zzzgd1…`, entre deux marqueurs
   (`pc_game_group.c`). Le lieur PE trie les `name$suffixe` : tout tombe dans une seule plage.
2. **Sortir** (`pc_release.py`) : la plage devient `gamedata/gamedata.bin`, ses pointeurs (lus
   dans les relocations de base du PE, `.reloc` gardée au lien) vont dans `gamedata.rel` ; la
   plage est mise à zéro, le marqueur réécrit « EXTERNAL », les infos de débogage retirées.
   Adresses fixes (pas d'ASLR) : rien à reloger au démarrage, on recopie juste les octets.
3. **Recette** : `tools/gen_recipe.py` de l'amont, inchangé, avec `pc_nm.py` comme `nm`
   (enlève le `_` des noms Windows, donne les tailles depuis le map, lit les symboles de
   référence de la langue sans assembleur ARM). Il faut `-malign-data=abi` (voir pièges).
4. **Builder** : celui de l'amont, appelé par `pc_builder.py` qui écrit des fichiers lâches
   (`<home>/devdata` + marqueur d'ABI) ; gelé avec PyInstaller.
5. **Premier lancement** : `pc_setup.c` compare l'ABI des données et de l'exe ; sinon boîte de
   dialogue, choix de la ROM, builder avec barre de progression.
6. **Audit** : `tools/release_audit.py --zip … --rom <ROM>` doit dire *clean* ; liste
   d'autorisation PC à part (`pc_port/release_audit_allow.toml`), fusionnée à la volée.

Résultat Emerald (français) : 93 % des données du jeu reconstruites depuis la ROM, 1 519 octets
de tables du jeu en littéral (plafond 4 096), graphismes 99,98 %, cartes 100 %.

## 5. Les pièges (dans l'ordre où ils ont coûté du temps)

| Symptôme | Cause | Remède |
|---|---|---|
| Jeu non bridé (1 700–6 000 images/s) | rien n'attend la VBlank | cadence dans `gspWaitForVBlank` et `C3D_FrameBegin` |
| Écrans noirs après un lien | index des assets à adresses absolues d'un ancien lien | la cible `pc` refait toujours `pc-data` |
| Monde 3D resté en 2D | pas de file GX liée | lier la file à `C3D_Init`, reprendre les `--wrap` GX |
| Fenêtre figée pendant les menus | rien n'est présenté sans nouvelle image | représenter la dernière image à chaque VBlank |
| Assembleur Windows refuse `.type`/`.size`, `.align N` faux | syntaxe ELF/ARM | `pc_asm.py`, variante COFF sous `#ifdef __ELF__` |
| Symboles introuvables entre C et assembleur | `_` devant les noms C en Windows 32 bits | `objcopy --redefine-syms` |
| Horloge bloquée en 2038 | `time_t` 32 bits de msvcrt | `-D_TIME_BITS=64` partout (reconstruire tout) |
| Champs de bits décalés dans les sauvegardes | disposition MSVC par défaut | `-mno-ms-bitfields` + test des structures |
| La recette ne reconnaît pas les tables | GCC x86 aligne tout tableau ≥ 32 octets sur 32 : tailles arrondies dans le map | `-malign-data=abi` |
| `gen_recipe` plante (division par zéro) | symboles de référence de taille 0 | les écarter dans `pc_nm.py` |
| Octets de la ROM dans l'exe publié | infos de débogage DWARF | `strip --strip-debug` à la release |
| Audit : dossier `romfs` refusé | réservé au RomFS 3DS | dossier `engine/` |
| Accès refusé en écrivant la plage de données | `.rdata` en lecture seule | `VirtualProtect` / `mprotect` le temps de la copie |
| CI Windows : patchs refusés | conversion CRLF au checkout | `core.autocrlf false` **avant** le checkout |
| CI : outils de la décompilation sans libpng | mauvais compilateur en tête du PATH | UCRT64 en premier pour `make tools` |
| CI : `jsonproc` introuvable | course `tools generated` en parallèle | deux `make` séparés |
| CI 3DS bloquée 1 h, log de 1,28 Go | ASan + ASLR forte des runners Linux récents (`DEADLYSIGNAL` en boucle) | `-fno-pie -no-pie` (correctif de l'amont) |
| Test accéléré interminable | le son suit le temps réel | `audio_clock=0` avec `EMERALD_PC_NOPACE` |
| Python 3.13 exigé par `voxel_precompile.py` | pin de l'amont pour ses releases | pour le PC, vérifier avec le Python qui a fait les fichiers |
| Texte du jeu illisible dans un menu natif | codes de contrôle `0xFC` + arguments | `GetExtCtrlCodeLength` |

## 6. Commandes de référence

```
# arbre (UCRT64), une langue : --french-rom <ROM> (Python UCRT64 dans le PATH)
python tools/bootstrap.py --dir build/upstream-pc
make -C build/upstream-pc tools && make -C build/upstream-pc generated
# exe de développement (MINGW32) — contient du contenu du jeu, jamais distribué
make -C build/upstream-pc/3ds_port -f $PWD/pc_port/pc.mk pc -j16
# release (une langue), audit compris
PC_NM=/mingw32/bin/nm.exe python pc_port/tools/pc_build_release.py --version X.Y.Z \
    --tree build/upstream-pc-fr --rom <ROM> --language fr --pyinstaller --nm /mingw32/bin/nm.exe
```

Une modification de `3ds_port/` n'atteint les arbres de build qu'en relançant `bootstrap.py`.
Toute modification du code change les adresses : la recette est alors à refaire (~15 min).

## 7. Transposer à Rouge Feu / Vert Feuille

La différence décisive : **il n'existe pas d'« Rouge Feu 3DS »**. Emerald PC a hérité
d'Emerald 3DS le moteur voxel, l'écran du bas, les options, le builder et la recette ; pour
Rouge Feu, il faut d'abord décider d'une base.

Ce qui se reprend presque tel quel :
- la méthode (jalons, scénarios, captures, critères) et les outils de vérification ;
- le shim citro/libctru sur OpenGL et SDL3, le son NDSP, les entrées, les chemins, le rapport
  de plantage, le menu natif, l'interface FUSION (si l'écran du bas est repris) ;
- toute la chaîne de release (regroupement, `pc_release.py`, `pc_nm.py`, builder, premier
  lancement, audit, manifeste RecompLauncher, CI) ;
- les pièges de la section 5 (presque tous liés à MinGW, au format PE ou à la CI).

Ce qui est propre à Émeraude et demande du travail :
- la couche 3DS de l'amont (`3ds_port/`) est écrite pour pokeemerald : la porter sur
  pret/pokefirered (moteur très proche, mais menus, cartes, scripts, île Sevii, différents) ;
- le monde voxel : générateurs écrits pour les tuiles et bâtiments de Hoenn ;
- les localisateurs par langue, les symboles de référence, les correctifs `patches/pokeemerald/` ;
- le builder et la recette attendent l'arbre et la ROM d'Émeraude (profils de ROM, `vtree`).

Deux bases possibles, à trancher au J0 de Rouge Feu :
1. **Porter la couche Emerald 3DS sur pokefirered**, puis le PC comme ici : tout le confort
   (écran du bas, voxel) mais un gros travail de portage du côté jeu.
2. **Partir de pokefirered seul** avec une couche GBA → PC minimale (affichage des fonds et
   sprites GBA, son MP2K), puis ajouter les améliorations PC une à une : plus rapide pour un
   premier exe jouable, sans le voxel au début.

À reprendre au démarrage : lire ce guide, `PC_PORT_STATUS.md`, et refaire un inventaire (J0)
avant tout choix.
