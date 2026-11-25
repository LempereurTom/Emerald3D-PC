# Inventaire du portage PC (J0)

État : J0, sans code. Base analysée : emerald-3ds `bcae4b7` (v0.5.0+, sync du
2026-10-30), pokeemerald `76463dac` + les 83 patchs (`tools/bootstrap.py`
appliqué avec succès dans `build/upstream`).

Ce document répertorie ce qu'il faut réimplémenter, réutiliser ou adapter pour
un backend `pc_port/` à côté de `3ds_port/`. Les chiffres viennent de greps
sur l'arbre ; ils servent à dimensionner, pas de preuve.

---

## 1. Constats qui modifient le plan

### 1.1 Le `.pak` est lié à l'exécutable, pas seulement à la ROM ⚠️

Ce n'est pas un simple conteneur d'assets. Trois de ses fichiers sont des
**images de sections tirées du link ARM**, avec des relocations :

| Fichier du pak | Origine | Lié à |
|---|---|---|
| `gamedata/gamedata.bin` | section `.gamedata` (tout le `.rodata` des unités du jeu), double link image/NOLOAD (`emerald3ds.ld.in`, `scripts/gen_gamedata_bundle.py`) | adresses de link de l'ELF ARM, disposition des structs de GCC ARM |
| `scripts/scripts.bin`, `sound/songs.bin` | bundles + relocations (`scripts/ctr_bundle.py`, lecture par `arm-none-eabi-readelf`) | idem |
| `asset_index.bin` (fichier moteur) | adresse de chaque stub INCBIN | idem |

`romfs:/engine/abi.bin` porte un CRC de tout cela, et le pak refuse de se
charger si ce CRC ne correspond pas.

**Conséquences :**
- Un pak 3DS **ne peut pas** servir à l'exécutable PC. Le PC a besoin de **ses
  propres recettes** (`emerald3ds.recipe` par langue *et* par cible), produites
  à la release depuis le build PC et les ROM de référence de Tom, exactement
  comme fait l'amont pour la 3DS.
- Le builder Python doit savoir faire `--target pc` (même code, autre recette,
  autre ABI). L'étape `prepare` de RecompLauncher appelle donc le builder avec
  la recette PC.
- Les scripts de bundle appellent `arm-none-eabi-readelf` sur un ELF.
  **Linux i686 (ELF)** : il suffit de changer d'outil (`readelf`).
  **Windows (PE/COFF, MinGW)** : il faut un autre extracteur (sections PE + table
  `.reloc`, link avec `--dynamicbase`). C'est le **plus gros risque technique
  du portage Windows**. Il confirme l'ordre du plan : Linux d'abord.
- Les sauvegardes, elles, ne dépendent que de la disposition des structs (§1.5).

### 1.2 La frontière à réimplémenter n'est pas `port_platform.h`

Les fonctions de `3ds_port/compat/port_platform.h` (assets, scripts, textes,
cartes, sauvegarde) sont **déjà implémentées en C portable** par des unités du
jeu : `3ds_assets.c`, `3ds_map_loader.c`, `3ds_script_loader.c` et
`3ds_compat.c`. Elles se réutilisent telles quelles.

La vraie frontière, ce sont les **≈230 fonctions exportées par les 16 unités
natives** (déclarées dans `3ds_port/include/3ds_*.h`), listées au §3.

### 1.3 `3ds_video.c` est du compositeur, pas du code GPU

| Fichier | Lignes | Lignes avec appel SDK | Fonctions |
|---|---|---|---|
| `3ds_video.c` | 13 153 | 830 (6 %) | 356 |
| `voxel/ctr_voxel.c` | 9 010 | 496 (5,5 %) | 239 |
| `3ds_dex_top.c` | 318 | 50 | 10 |
| `3ds_gpumem.c` | 366 | 20 | 17 |

À eux quatre, ces fichiers n'utilisent qu'**environ 215 symboles citro2d,
citro3d, GPU, GX et Mtx distincts** (≈70 fonctions). Les plus fréquents :
`C2D_Flush`, `C2D_DrawImageAt`, `C2D_DrawRectSolid`, `C3D_TexEnv*` (109
occurrences dans `ctr_voxel.c`), `C3D_RenderTarget*`, `C3D_AlphaBlend`,
`C3D_DepthTest`, `C3D_TexInit`/`C3D_SyncTextureCopy`, `Mtx_PerspStereo`/`Mtx_LookAt`.

Réécrire `pc_video.c` et `pc_voxel_gl.c` « from scratch », comme le prévoit le
plan, reviendrait à dupliquer ~20 000 lignes de logique (couches, écrans mis
en scène, combats stéréo, météo, flou…) qui évoluent sans cesse en amont.
→ **Décision à prendre, voir §6.**

Détail qui compte pour un shim : les textures sont écrites en **tuiles Morton
(format PICA)** côté CPU (`3ds_video_decode.c:15`, `voxel_atlas.c`, mesh
builder). Il faut donc les détuiler à l'upload GL. C'est peu coûteux : une
fonction par format (RGBA5551, RGBA8, RGB565, A8, L8).

### 1.4 Un « libctru-lite » rend six fichiers natifs réutilisables tels quels

Dans les fichiers natifs « légers », les appels SDK se limitent à des primitives :

| Fichier | Appels SDK |
|---|---|
| `3ds_settings.c` | `LightLock`, `LightEvent`, `threadCreate/Join`, chemin `sdmc:` |
| `3ds_log.c` | idem + `svcGetSystemTick`, `svcOutputDebugString`, `gfx*` (écran fatal) |
| `3ds_data.c` | `LightLock`, `svcSleepThread`, `sdmc:` |
| `3ds_prof.c` | `svcGetSystemTick`, `osGet*Version`, `CFGU_GetSystemModel` |
| `3ds_scenario.c` | `sdmc:` uniquement |
| `3ds_fs.c` | `romfsInit/Exit`, `sdmc:` |

Un en-tête `pc_port/compat/3ds.h` (≈300 lignes au-dessus de SDL3 : mutex,
conditions, threads, ticks à 268 MHz simulés, `sdmc:`/`romfs:` → répertoires)
permet de les compiler **sans modification**. On ne réécrit alors que
`main`, la plateforme, l'entrée, l'audio et le local sans fil.

### 1.5 Disposition des structs : options de compilation obligatoires

La sauvegarde (image flash de 128 Kio) et `gamedata.bin` reflètent la
disposition des structs de GCC ARM EABI. Or l'arbre du jeu contient **469
champs de bitfield** dans `include/` et 7 types 64 bits/double.
- **MinGW** applique par défaut la disposition MS des bitfields
  (`-mms-bitfields`) → **`-mno-ms-bitfields` obligatoire**, sinon les saves
  3DS ⇄ PC sont incompatibles.
- **Linux i386** aligne `double`/`u64` sur 4 octets (ARM EABI : 8) →
  `-malign-double`, ou vérifier qu'aucune struct sauvegardée n'en contient.
- **Test J1 à ajouter** : générer `sizeof`/`offsetof` de `SaveBlock1`,
  `SaveBlock2`, `Pokemon`, `BoxPokemon`, `PokemonStorage` et des structs de
  `.gamedata` sur ARM et sur i686, puis comparer (échec CI si différent).

### 1.6 `PLATFORM_3DS` désigne le jeu de fonctionnalités, pas l'ARM

`PLATFORM_3DS` apparaît 527 fois dans 112 fichiers du jeu, et `PORT_BRIDGE`
622 fois. Ces gardes protègent les **fonctionnalités du port** (écran du bas,
menus de combat tactiles, Pokédex, champ de vision élargi) : le PC doit en
hériter. L'assembleur inline est déjà protégé par `#ifndef PORTABLE`
(`m4a.c`, `script.c`, `librfu_intr.c`).
→ Compiler les unités du jeu PC avec
`-DPORT_BRIDGE -DPLATFORM_3DS -DPORTABLE -DMODERN=1 -DPORT_PC`, et n'ajouter
`&& !defined(PORT_PC)` que là où c'est vraiment de l'ARM (les fonctions
callback avec le bit 0, selon `ARCHITECTURE.md` : à localiser en J1).

### 1.7 Chemins `romfs:/` codés en dur côté jeu

Les unités du jeu n'ont aucun appel SDK, mais elles font
`fopen("romfs:/…")` : `3ds_assets.c` (3 chemins), `3ds_script_loader.c` (3),
`3ds_battle_hud.c`, `3ds_bottom_ui.c`, `3ds_bottom_shell.inc`,
`voxel/voxel_tree.h`. Sur PC, il faut soit un `fopen` enveloppé
(`-Wl,--wrap=fopen` sous Linux, macro sous MinGW) qui réécrit le préfixe, soit
un `#define CTR_ROMFS_PREFIX`. Le wrap ne touche à aucun fichier partagé :
c'est la solution la plus rebasable.

### 1.8 Environnement Windows

Avec `core.autocrlf=true` (réglage global de cette machine), les patchs passent
en CRLF et `bootstrap.py` échoue (`patch does not apply`). La correction a été
appliquée **dans ce dépôt seulement** (`git config core.autocrlf false` +
nouveau checkout). À documenter dans `DEVELOPMENT.md` du fork, ou à
corriger en amont avec un `.gitattributes` (`*.patch -text`).

---

## 2. Classement des fichiers de `3ds_port/src`

Groupes : **jeu** = unité compilée avec `PORT_BRIDGE` (`full.mk`,
`BACKEND_SRCS`/`VOXEL_GAME_SRCS`) ; **natif** = `SOURCES` du `Makefile`, inclut
`<3ds.h>` ; **inc** = inclus par un autre fichier.

### 2.1 PORTABLE : à compiler tel quel dans `pc_port`

| Fichier | Lignes | Groupe | Note |
|---|---|---|---|
| 3ds_assets.c | 1 180 | jeu | implémente `Port_*Asset*` ; chemins `romfs:` (§1.7) |
| 3ds_map_loader.c | 115 | jeu | `Port_GetMapLayoutById`… |
| 3ds_script_loader.c | 239 | jeu | relocations des bundles ; chemins `romfs:` |
| 3ds_compat.c | 550 | jeu | saves (`Port_WriteFlash`), `Port_Log`, debug |
| 3ds_game_full.c | 1 442 | jeu | AgbMain, VBlank, intégration |
| 3ds_game_bridge.c | 200 | jeu | |
| 3ds_bottom_ui.c (+ 6 `.inc`, `3ds_bottom_art.h`) | 7 798 (+6 150) | jeu | UI de l'écran du bas : dessine dans un buffer CPU |
| 3ds_battle_hud.c | 1 275 | jeu | `romfs:/battle/hud.bin` |
| 3ds_dex_top_game.c | 259 | jeu | décode les images du Pokédex dans des buffers |
| 3ds_extras.c | 302 | jeu | |
| 3ds_nuzlocke_game.c, _core.c, _store.c (+ `.inc`) | 977 | jeu/natif | `_core`/`_store` testés sur l'hôte |
| 3ds_randomizer.c, _core.c | 235 | jeu | |
| 3ds_encounter_tables.c, 3ds_encounters.c | 96 | jeu/natif | |
| 3ds_rfu_adapter.c | 1 297 | jeu | adaptateur sans fil GBA → `LocalLink_*` (§2.3) |
| 3ds_sound.c | 177 | jeu | verrou du son vers `CtrAudio_LockSound` |
| 3ds_map_events.c, 3ds_sprite_ids.c, 3ds_prof_wrap.c | 252 | jeu | |
| 3ds_cycle_test.c | 234 | jeu | test de cycle ; déclare `vramAlloc` → exclure sur PC |
| 3ds_daynight.c, 3ds_clock_store.c | 449 | natif | sans SDK, testés sur l'hôte |
| 3ds_field_view.c, 3ds_corner_art.c, 3ds_pak.c | 458 | natif | sans SDK |
| 3ds_icons_ptbr.c | 1 123 | natif | sans SDK (PT-BR) |
| 3ds_input_decode.c, 3ds_video_decode.c | 96 | natif | décodage, détuilage Morton |
| voxel/*.c sauf ctr_voxel.c (15 fichiers) | 14 522 | jeu | world, camera, atlas, mesh builder, lighting, entities, relief, building, tree, sign, battle, swoop… |

### 2.2 PORTABLE avec le shim libctru-lite (§1.4)

| Fichier | Lignes |
|---|---|
| 3ds_settings.c | 458 |
| 3ds_log.c | 391 (l'écran fatal `gfx*` → fenêtre SDL) |
| 3ds_data.c | 691 |
| 3ds_prof.c | 464 |
| 3ds_scenario.c | 555 (pilote les tests PNG du J2) |
| 3ds_fs.c | 89 |

### 2.3 SPÉCIFIQUE 3DS : à remplacer

| Fichier | Lignes | Remplacement PC |
|---|---|---|
| main_3ds.c | 101 | `pc_main.c` (tas, `svcControlMemory` → `malloc`) |
| 3ds_platform.c | 583 | `pc_platform.c` : boucle, APT/HOME → événements SDL, timing 59,7275 Hz, threads |
| 3ds_input.c | 52 | `pc_input.c` + `pc_touch.c` (`hid*` → clavier, manette, souris) |
| 3ds_audio.c | 436 | `pc_audio.c` : NDSP → `SDL_AudioStream` ; garder `CtrAudio_*` |
| 3ds_bottom_screen.c | 114 | blit du framebuffer de l'écran du bas → texture GL |
| 3ds_local.c | 1 343 | local sans fil (UDS) : **stub** `LocalLink_Enabled()=false` en v1 |
| 3ds_gpumem.c | 366 | wrappers `__wrap_*` d'allocation VRAM/linear : sans objet sur PC (comptage seul) |
| voxel/voxel.v.pica, voxel_weather.v.pica | 203 | GLSL 330 (uniformes : `projection`, `modelView`, `origin`, `shadeTint`, `tintDiff`, `grade`, `fog`, `dappleU/V` ; météo : + `boxOffset/Size/Base`, `across`, `along`) |

### 2.4 MIXTE (logique + GPU), selon la décision du §6

| Fichier | Lignes | Partie SDK |
|---|---|---|
| 3ds_video.c | 13 153 | C2D/C3D (830 lignes), `__wrap_GX_*`/`GSPGPU` (6 wrappers), cibles de rendu (81), `gfxSet3D` |
| voxel/ctr_voxel.c (+ .h) | 9 221 | combineurs TEV (109), VBO/attributs, uniformes, `Mtx_*`, `C3D_SyncTextureCopy` ; thread de streaming des textures |
| 3ds_dex_top.c | 318 | 2 textures + dessin C2D de la coque du Pokédex |

---

## 3. API à fournir côté PC (exportée par les unités natives)

Liste tirée des définitions non `static` des 16 unités natives.
Avec l'option A du §6, `3ds_video.c`, `ctr_voxel.c` et `3ds_dex_top.c`
restent tels quels : seules les lignes marquées ★ sont à réécrire.

- ★ **main** (`main_3ds.c`) : `main`, `__system_allocateHeaps`
- ★ **CtrPlatform_*** (`3ds_platform.c`) : `Init`, `Shutdown`, `BeginFrame`,
  `EndFrame`, `FrameCount`, `Ticks`, `TickMs`, `Milliseconds`, `WallSeconds`,
  `SleepUs`, `StartThread`, `SoundTick`, `SetHooks`, `Fatal`, `ShowDataError`,
  `ReportMemory`, `SetBattleScene`, `BattleSpeedBadge`, `BattleSpeedKeys`,
  `StepBattleSpeed` + `Port_ProfileMark`, `Port_ProfBegin`, `Port_ProfAcc`
- ★ **CtrInput_*** : `Scan`, `Get`, `Clear`
- ★ **CtrAudio_*** : `Init`, `Shutdown`, `StartWorker`, `Queue`, `Kick`,
  `SetClock`, `LockSound`, `UnlockSound`
- ★ **CtrBottom_*** : `Blit`, `BlitDim`, `BlitRect`, `LastBlitTicks`
- ★ **LocalLink_*** (17, stubs) : `Init`, `Shutdown`, `Enabled`, `Send`,
  `PollEvent`, `StartSession`, `LeaveSession`, `InSession`, `GetPeers`,
  `GetSession`, `GetSessionKind`, `MyId`, `Generation`, `SetAdvert`,
  `Milliseconds`, `AfterWake`, `LeaveBeforeSleep`
- ★ **CtrGpuMem_*** : `LargestVram`, `Report` (valeurs factices)
- **CtrData_*** (shim) : `Init`, `BackendName`, `Exists`, `Open`, `Load`,
  `Locate`, `Prefetch`, `ReadRange`, `Size`. Note : `fopencookie` (glibc)
  n'existe pas sous MinGW → `funopen`/fichier temporaire, ou lecture en
  mémoire (le pak fait < 100 Mo).
- **CtrFs_*** (shim) : `Init`, `Shutdown`, `OpenAsset`, `OpenData`
- **CtrLog_*** (shim) : `Init`, `Close`, `Enabled`, `Write`, `WriteBinary`,
  `LastDrain`, `ShowFatal`
- **CtrProf_*** / `Port_Prof*` (shim) : `Close`, `EndFrame`, `LogIdentity`,
  `Scene`, `Port_ProfAdd`, `Port_ProfScene`, `Port_ProfTick`
- **CtrScenario_*** (shim) : `Init`, `Close`, `Checkpoint`, `CheckpointDue`,
  `Clock`, `Input`, `SavePath`, `SaveWrites`, `Slider`, `ForceSlider`
- **CtrSettings_*** (shim, 28 fonctions) : `Load`, `Defaults`, `ApplyLine`,
  `Get/SetInt`, `Text`, `Freeze`, `Shutdown`, réglages voxel, vitesse, FPS…
- **CtrVideo_*** (49, MIXTE) : `Init`, `Shutdown`, `Present`, `Bind`,
  `HoldTop`, `NotifyWrite`, `NotifyTilesetAnimWrite`, `SetLine{Registers,
  Scroll,Window}`, `SetTileBank`, `Register/UnregisterTilemapBanks`,
  `TilemapRequested`, `Mark*Oam`/`Clear*Oam` (écran, météo, brouillard, vol,
  machine, palettes étendues, nuages voxel), `SetOamAnchor`,
  `ClearOamAnchors`, `ExtPalette`, `LatchExtPalettes`, `SetFieldBanner`,
  `SetFieldLight`, `BottomPictureY`, `BottomStrips`, `CopiesBegin/End`,
  `DecodeCounts`, `RequestPlaneRelease`, `TryVoxelUpload`,
  `TryVoxelAnimUpload`, `VoxelUploadsLeft`, `ShowNoHome`, `CtrBattle_Sandstorm`
  + 6 `__wrap_GX_*`/`GSPGPU` (sans objet sur PC)
- **CtrVoxel_*** (34, MIXTE) : `Init`, `Shutdown`, `Update`, `Draw`, `Idle`,
  `IsAvailable(ForBattle)`, `IsWarmingUp`, `Begin/EndBattle`, `InBattle`,
  `BattleIntro`, `SetBattleFrame`, `SetBrightness`, `SetStereo`, `SetSwoop`,
  `SwoopNeutral`, `Bloom`, `Gloom`, `Draws{Fog,Sand,Weather}`,
  `PlayerLightSpot`, `ProjectPictureTile`, `OverlayParallax`,
  `NotifyTilesetAnimWrite`, `NoteFrameWait`, `AfterSubmit`, `ArenaUse`,
  `HoldsVram`, `Park/UnparkVram`, `ReleaseIdleVram(For)`
- **CtrDexTop_*** (7, MIXTE) : `Show`, `Upload`, `Idle`, `DrawEye`,
  `BackdropBegin`, `BackdropRow`, `BackdropComplete`

En sens inverse, les natifs appellent des fonctions du jeu (`AgbMain`,
callbacks de VBlank, lecture des registres GBA émulés via `3ds_emu_regs.h`).
Ces fonctions restent portables.

---

## 4. Audit 32 bits (arbre du jeu patché, `src/` + `include/`)

| Motif | Occurrences | Risque en 32 bits | Risque en 64 bits |
|---|---|---|---|
| lecture de pointeurs dans les scripts (`T1_READ_PTR`, `ScriptReadWord`…) | 383 | aucun | **élevé** : pointeurs sur 4 octets dans les données de script/bundles |
| `SetWordTaskArg`/`GetWordTaskArg` (pointeur dans 2×s16) | 33 | aucun | **élevé** |
| `u32 ptr = (u32)…` / `u32 addr = ScriptReadWord` | 4 (`pokedex.c:5590`, `scrcmd.c:209/217/1764`) | aucun | élevé |
| casts pointeur → u32/int explicites | 5 | aucun | élevé |
| `STATIC_ASSERT` de tailles | 26 | à vérifier (i686 ≠ ARM pour `double`/`u64`) | élevé |
| bitfields dans `include/` | 469 | **MinGW : `-mno-ms-bitfields`** | idem |
| asm inline | 10 | aucun (`#ifndef PORTABLE`) | aucun |
| bundles (`gamedata/scripts/songs`) | — | relocations 32 bits : OK en i686 | **refonte du format** |

Le 64 bits demande de refaire le format des bundles et des recettes, en plus
du nettoyage du C. Le report au J11 se confirme.

---

## 5. Ordre de travail du J1 (ajusté)

1. `pc_port/compat/3ds.h` (libctru-lite) + `CMakeLists.txt` i686 (Linux,
   `-m32`), en réutilisant les listes de `full.mk`.
2. Test de disposition des structs ARM ⇄ i686 (§1.5). Il conditionne les
   saves et les recettes.
3. Unités du jeu + unités portables + stubs ★ → link.
4. Portage de `gen_gamedata_bundle.py`/`ctr_bundle.py` sur l'ELF i686
   (`readelf` hôte), plus un script de link i686 calqué sur
   `emerald3ds.ld.in`.
5. Génération d'un pak de **développement** depuis le build PC local
   (`make pak` équivalent), puis démarrage, chargement, boucle avec logs.
   Les recettes PC (pour les joueurs) viennent au J7/J9.

---

## 6. Décisions ouvertes (pour Tom)

**D1 — Rendu : réécrire ou émuler citro ?**
- **A. Shim « citro-sur-GL » (recommandé).** Implémenter le sous-ensemble
  utilisé de citro2d, citro3d et Mtx (≈70 fonctions) au-dessus d'OpenGL 3.3 :
  combineurs TEV traduits en fragment shader générique paramétré par
  uniformes, détuilage Morton à l'upload, cibles de rendu → FBO, les deux
  vertex shaders portés à la main en GLSL. `3ds_video.c`, `ctr_voxel.c` et
  `3ds_dex_top.c` compilent **sans modification** ; les mises à jour de l'amont
  s'appliquent d'elles-mêmes ; rendu au pixel près, facile à comparer.
  Coût : ≈3–4 kLoC, une seule fois. Risque : la précision des combineurs PICA
  (connue et documentée, Citra l'a fait).
- **B. Réécriture (`pc_video.c` + `pc_voxel_gl.c`, plan initial).** Code GL
  plus idiomatique et plus libre (résolutions hautes plus faciles). Mais
  ≈20 kLoC de logique dupliquée, qui diverge à chaque sync de l'amont.
- Avec A, le plein écran à l'échelle entière et les dispositions (J4) se font
  au moment de composer les deux cibles finales, sans toucher au compositeur.
  Le rendu voxel en haute résolution reste possible plus tard (cibles à N×).

**D2 — Windows : PE ou autre ?** Les bundles exigent d'extraire les relocations
d'une section. Il faut soit écrire l'extracteur PE (`.reloc`), soit une
autre approche. À trancher au début du J9 ; aucun impact avant.

**D3 — Un exécutable par langue** (comme la 3DS : `--spanish-rom` modifie
l'arbre au bootstrap) : il est probable que la langue soit fixée à la
compilation, ce qui donne un exe et une recette par langue. À confirmer au J7.
