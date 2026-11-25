# État du portage PC

| Jalon | État | Note |
|---|---|---|
| J0 Inventaire | fait (2026-11-23) | [PC_PORT_INVENTORY.md](PC_PORT_INVENTORY.md) |
| J1 Compile, linke, démarre (i686) | fait (2026-11-23), **Windows MinGW** au lieu de Linux | voir ci-dessous |
| J2 Écran titre + 2D (shim citro-sur-GL, SDL3) | fait (2026-11-24) ; essai de Tom : jeu non bridé, corrigé en J3 | voir ci-dessous |
| J3 Audio (+ cadence 59,83 Hz) | fait (2026-11-24) ; filtre des aigus choisi à l'écoute par Tom | voir ci-dessous |
| J4 Écran du bas + tactile | fait (2026-11-24) ; essai de Tom : F9/Tab/F11 et clic OK | voir ci-dessous |
| J5 Monde voxel | fait (2026-11-24) ; essai de Tom : Bourg-en-Vol, labo en 3D | voir ci-dessous ; restent à vérifier : combat voxel, pluie/sable, nuit |
| J6 Sauvegardes, horloge, réglages | en cours (2026-11-24) | voir « J6 : état » |
| J7 Toutes les ROM / langues | fait pour EN, FR, PT-BR (2026-11-24) ; ES/IT/DE/JP : il faut les ROM | voir « J7 : état » |
| J8 Manettes, options PC, crédits | fait en automatique (2026-11-24) ; **manette et options à essayer par Tom** | voir « J8 : état » |
| J9 Builds de release | fait en local (2026-11-25) pour le français ; **premier lancement à essayer par Tom** ; CI prête (fork à créer) | voir « J9 : état » |
| J10 RecompLauncher | fork https://github.com/LempereurTom/Emerald3D-PC, CI verte (manifeste validé contre le schéma), **release v0.1.0 publiée** (2026-11-25, français), ZallaxDev prévenu (issue #314) ; **reste : soumission sur recomplauncher.com (Tom)** | voir « J10 : état » |
| J11 | — | |

## J10 : état
- Lu avant d'écrire : le schéma officiel (`https://recomplauncher.com/schema/recomplauncher.json`,
  draft-07, `additionalProperties: false` partout) et le guide éditeur
  (`https://recomplauncher.com/sdk-files/guide.md`). Le manifeste d'EmeraldRecomp ne valide pas
  (clé `requires` inconnue) et utilise `game`, déprécié : pas repris.
- `recomplauncher.json` (racine du dépôt, et en haut du zip à côté de l'exe, comme le guide le
  demande) : une entrée `inputs` `game` (fichier `.gba`, dump français CRC32 `a3fdccb1`, 16 Mio) ;
  `prepare` lance `emerald-pc-builder` avec `--rom {input:game} --home {data}` (le builder écrit
  alors des lignes `progress 0.42 …`, la forme du lanceur, quand `RECOMPLAUNCHER=1`), `done` :
  `{data}/devdata/.emerald3ds-dev` ; `start` lance `emerald-pc` avec `--home {data}` (nouvelle
  option, lue en UTF-8) ; `saves` : la sauvegarde et ses compagnons.
- Nom du zip : `EmeraldPC-vX.Y.Z-Windows-x86.zip` (le lanceur choisit le fichier par le système
  nommé dedans ; l'exe est 32 bits). Un seul fichier par système : la release actuelle est en
  français. Plusieurs langues demanderont un lanceur de langue (D3 : un exe par langue).
- CI : `.github/workflows/pc.yml` valide le manifeste contre le schéma officiel (jsonschema).
- Non vérifié : que `done` accepte `{data}` (le guide ne le dit pas ; sinon le jeu refait de toute
  façon les données lui-même au démarrage, `pc_setup.c`).
- Reste (Tom) : créer le fork GitHub, y mettre une section « How to play » en haut du README
  (le guide le recommande) et les crédits, prévenir ZallaxDev, publier une release, soumettre le
  dépôt sur `https://recomplauncher.com/submit`, puis le badge « Get it on RecompLauncher ».

## J9 : état
- **D2 (Tom, 2026-11-25) : option 1, la recette**, comme sur 3DS. L'exe publié ne contient aucune
  donnée du jeu ; elles sont refaites chez le joueur depuis sa ROM, une fois, au premier lancement.
- **Données du jeu hors de l'exe** : `pc.mk` (`pc_group_sections.sh`) regroupe les données en
  lecture seule du jeu (tables, textes, scripts, musiques : le `.gamedata` de la console, plus
  scripts et chansons) entre deux marqueurs (`pc_game_group.c` : `__ctr_gamedata`,
  `__ctr_gamedata_end`, 5,3 Mo). `pc_port/tools/pc_release.py` en tire
  `gamedata/gamedata.bin` (+ `.rel` : les 88 750 pointeurs de l'exe, lus dans les relocations
  de base du PE), met la plage à zéro, réécrit le marqueur « EXTERNAL » et retire les infos de
  débogage. Au démarrage, `pc_bundles.c` relit la plage depuis les données du joueur (adresses
  fixes, sans ASLR : rien à reloger). Les fichiers moteur vont dans `engine/` à côté de l'exe.
- **Recette** : `tools/gen_recipe.py` tel quel, sur la staging PC, avec `pc_port/tools/pc_nm.py`
  (nm du PE sans le `_` de Windows, tailles prises dans le map ; symboles de référence de la
  langue lus dans `build/<langue>-reference.s` sans assembleur ARM). Français :
  **93 % des données du jeu depuis la ROM**, 1 519 octets de tables du jeu en littéral (plafond
  4 096), graphismes 99,98 %, cartes 100 %. Puis `tools/voxel_precompile.py` (programmes voxel,
  tous vérifiés) : recette de 1,7 Mo.
- `-malign-data=abi` ajouté (`pc.mk`) : sans lui GCC aligne tout tableau de 32 octets ou plus sur
  32, la taille de chaque objet dans le map est alors arrondie et la recette ne reconnaît plus
  les tables (`gMonBackPicCoords`…). La disposition des structures (sauvegardes) ne change pas.
  Le pin Python 3.13 de `voxel_precompile.py` n'est pas appliqué au PC : les fichiers voxel du PC
  sont faits par le Python de la build (3.14), le même qui les vérifie.
- **Builder** : `pc_port/tools/pc_builder.py` lance le builder d'Emerald 3DS (recette, CRC de
  chaque fichier, voxel) et écrit les fichiers dans `<home>/devdata` avec le marqueur de l'ABI ;
  gelé par PyInstaller (`emerald-pc-builder.exe`, PyInstaller de MSYS2 UCRT64).
- **Premier lancement** (`pc_setup.c`) : si l'exe est une release et que les données manquent ou
  ne correspondent pas (ABI), une boîte explique, le sélecteur de fichier demande la ROM, le
  builder tourne avec une barre de progression, puis le jeu démarre. Erreur : message et
  « Réessayer ».
- **Release** : `pc_port/tools/pc_build_release.py --version X --tree build/upstream-pc-fr --rom
  <ROM> --language fr --pyinstaller` : exe, recette, builder, `engine/`, DLL, README, LICENSES,
  zip, puis **`release_audit.py --rom` : propre** (aucune suite d'octets de la ROM ; l'exe n'en
  partage que 32, incidentels, sous le seuil de 256). Liste d'autorisation PC :
  `pc_port/release_audit_allow.toml`, fusionnée avec celle du dépôt sans la modifier.
- Vérifié de bout en bout : zip décompressé dans un dossier vide → `emerald-pc-builder.exe` avec la
  ROM de Tom → 6 860 fichiers (dont les followers) → `emerald-pc.exe` démarre sur ces données
  (« game data: 5394 KiB in place, from the player's data »), image identique.
- **CI** : `.github/workflows/pc.yml` (tests de disposition ; build Windows MSYS2, test des
  sauvegardes, extraction des données). Rien n'est publié par la CI : la release demande la ROM,
  elle se fait sur la machine du mainteneur, comme en amont. Elle tournera une fois le fork créé.
- Reste : le premier lancement avec la vraie boîte de dialogue (Tom) ; les autres langues (il faut
  leurs ROM ; l'anglais demande l'ELF du jeu original) ; Linux (J9 bis : `pc.mk` est MinGW).

## J8 : état
- **Onglets PC dans OPTIONS** (demande de Tom) : ÉCRAN (fenêtre / plein écran / plein écran
  exclusif, résolution de l'exclusif, taille de fenêtre 1x–6x, V-Sync, mise à l'échelle entière
  ou étirée, filtre net ou lissé, disposition et inversion des écrans) et COMMANDES (touches du
  clavier redéfinissables : flèche de la ligne puis la touche, Échap annule, 5 s sans touche
  annule ; « touches d'origine » ; disposition de la manette Xbox/Nintendo). **À PROPOS** dans
  GÉNÉRAL : crédits et licence exigés (Emerald 3DS par Zallax et contributeurs, lien, portage
  PC non officiel modifié, CC BY-NC-SA 4.0). Textes dans les 7 langues du port.
- Fichiers partagés : `3ds_extras.c` et `3ds_bottom_options.inc` reçoivent 23 lignes sous
  `#ifdef PORT_PC` (des `#include` de `pc_port/compat/pc_extras*.inc` et `pc_options.inc`, deux
  entrées d'énumération) ; tout le reste est dans `pc_port`. Valeurs dans `settings.txt`.
- **Manettes** (SDL3, toutes celles que SDL connaît, plusieurs à la fois) : boutons de face par
  position, gâchettes = ZL/ZR (vitesse du jeu), stick gauche = pad circulaire, croix = croix.
  **Pas encore essayée avec une vraie manette.**
- Vérifié : les 13 écrans d'options s'affichent (captures), la capture de touche affiche « … »,
  TAILLE FENÊTRE redimensionne la fenêtre. La géométrie « étirée » passe par `PcLayout_ComputeFit`.
- Note : l'arbre français (`build/upstream-pc-fr`) a l'ancienne copie de `3ds_port/` tant qu'on
  n'y relance pas le bootstrap et la compilation.
- **Retours de Tom après l'essai du J8** (2026-11-24), tous faits, compilés, vérifiés par captures :
  - START (Options sur PS5, Entrée au clavier) ouvre le menu de l'écran du bas sur le terrain
    (`3ds_bottom_ui.c`, sous `PORT_PC`, comme X ; les menus classiques gardent START).
  - Plein écran sans grandes bandes noires : MISE À L'ÉCHELLE « ÉTIRÉE » par défaut, avec un
    échantillonnage « net-bilinéaire » (pixels nets à toute échelle). Un `settings.txt` qui a
    déjà `pc_stretch=0` garde l'échelle entière : la repasser dans les options.
  - Nouvelle disposition **FUSION** : seul l'écran du haut remplit la fenêtre, avec la barre
    d'icônes (le dock) en bas ; l'écran tactile s'ouvre par-dessus le monde assombri quand le
    jeu en a besoin (menus, sac…) ou avec Tab. F9 fait défiler les dispositions.
  - Volumes réglables (GÉNÉRAL > SON) : général, musique, effets (0–10), par l'enveloppe de
    `TrkVolPitSet` (`pc_sound_volume.inc`) et le gain de NDSP.
  - COULEURS (désactivé / vives / intenses ; « vives » par défaut) : contraste et saturation
    dans le shader d'affichage.
  - PARTICULES selon le lieu, **en extérieur seulement** (villes, routes ; aucune dans les
    maisons, centres, boutiques, bases secrètes) : feuilles (bois, routes boisées), pétales
    (routes fleuries), lucioles la nuit, poussière dans les grottes ; rien quand le jeu a sa
    propre météo. Pas de neige : Hoenn n'a pas de zone enneigée.
  - MÉTÉO DYNAMIQUE : averses sur les routes (6–14 min de sec, 2–4 min de pluie, 6 s de
    transition), traits de pluie en pixels et monde un peu plus gris. **Visuelle seulement** :
    la météo du jeu (qui agit en combat) n'est pas changée, donc rien ne change pour les combats.
  - Options réorganisées : GÉNÉRAL (+ SON, À PROPOS) ; ÉCRAN : AFFICHAGE, IMAGE, DISPOSITION,
    AMBIANCE ; COMMANDES : BOUTONS, CROIX ET MENUS, VITESSE, MANETTE.
  - Contrôles : `EMERALD_PC_AMBIENCE=leaves|petals|dust|night|rain` force une ambiance.
- **FUSION refait en interface « un seul écran »** (2026-11-25, d'après la maquette validée par Tom) :
  le monde remplit la fenêtre ; en haut à gauche le lieu, l'heure et COURIR (Maj) ; en bas à
  gauche une mini-carte (la carte de l'écran du bas autour du joueur ; clic ou M : la carte
  entière) ; en bas à droite ÉQUIPE (P), SAC (I), puis SAUVER (F5) et OPTIONS (O), plus petits.
  Ce sont les pixels de l'écran du bas (plaques et icônes du jeu), nets ; un clic ou une touche
  est un toucher à cet endroit, donc l'écran du bas fait ce qu'il fait toujours et s'ouvre
  par-dessus le monde assombri. L'interface ne s'affiche que quand le joueur peut marcher (pas
  pendant un message, un combat, un menu) ; les badges de touches disparaissent dès qu'on joue
  à la manette. Tab / M / un clic à côté referment l'écran du bas (retour à la carte).
  Côté jeu : `CtrBottom_PcHud` et `CtrBottom_PcGoMap` (`3ds_bottom_ui.c`, sous `PORT_PC`) ; côté
  fenêtre : `pc_port/src/gl/pc_hud.c` (police 5x7 en pixels pour les libellés).
- Retours de Tom sur l'interface (2026-11-25) : les plaques sont redessinées à part par l'interface
  de l'écran du bas (sur noir et sur blanc, pour retrouver leur transparence : plus de coins verts),
  mini-carte aux coins arrondis, interface réaffichée sans délai en sortant d'un menu, pointeur de
  la souris caché en plein écran FUSION (il revient 1,5 s quand on la bouge).
- Retours de Tom (2026-11-25, suite) : **voxel 3D et FUSION par défaut** sur PC (nouveaux
  réglages ; `3ds_settings.c` sous `PORT_PC`) ; **barre de menus Windows** sous le titre avec toutes
  les options de l'écran OPTIONS (`pc_port/src/gl/pc_menu.c`, construite depuis la table de
  l'écran : `CtrBottom_PcOpt*`, `pc_menu.h` ; cases, choix radio avec la valeur à droite, ◀ ▶ pour
  les valeurs numériques, un clic puis la touche pour COMMANDES ; grisé comme à l'écran ; cachée
  en plein écran ; `EMERALD_PC_MENU_DUMP` l'écrit en texte pour les vérifications) ;
  **Échap** ferme le menu de l'écran du bas (focus du dock, OPTIONS, carte de dresseur, carte de
  Vol ; un écran du jeu ouvert reçoit B) ; titre de fenêtre « Emerald PC » pour une release.
- Rond (B, Échap) dans OPTIONS revient à la carte (`3ds_bottom_options.inc`, sous `PORT_PC`).
- Vérifié par captures en 1920x1080 (`EMERALD_PC_DUMP_EFFECTS=1` dessine FUSION à cette taille).
  Pas encore vérifié : les clics et touches de l'interface en vrai (Tom).

## J7 : état
- **D3 (Claude, 2026-11-24, réversible) : un exécutable par langue**, comme sur 3DS. La langue est
  fixée à la compilation : `tools/localize_<langue>.py` remplace textes, graphismes et champs du
  code dans l'arbre généré à partir de la ROM propre du joueur (SHA-1 vérifié), et le moteur
  n'accepte que les données de sa langue. Un seul exe multilingue demanderait de refaire ce
  mécanisme : à reconsidérer plus tard si besoin.
- Un arbre par langue : `python tools/bootstrap.py --dir build/upstream-pc-fr --french-rom <ROM>`
  (sans `--make` : pas de chaîne 3DS), puis la même commande `make … -f pc_port/pc.mk pc` dans cet
  arbre. L'arbre anglais `build/upstream-pc` reste celui du développement.
- L'exe et le romfs de développement d'une langue contiennent ses textes, tirés de la ROM :
  **jamais distribués** (la version publiable reconstruit les données chez le joueur, J9).
- `pc_port/tools/rom_info.py` : taille, CRC32, SHA-1 d'une ROM et vérification contre les six
  dumps pris en charge (pour le manifeste, J10).
- Vérifié (début de partie rejoué, voxel activé, arrêt propre) : **anglais** ; **français** depuis
  la ROM de Tom (titre « VERSION ÉMERAUDE », Birch, « CAMIONNETTE », « BOURG-EN-VOL », Maman) ;
  **portugais du Brésil** (« VERSÃO ESMERALDA », « SEU NOME? », « MAMÃE… »). `run-dev.ps1 -Lang fr`.
- **Correctif partagé** : les localisateurs français, espagnol, italien et allemand écrivaient les
  étages du Mont Dresseurs en assembleur ELF seulement (`.type`, `.size`) ; refusé par l'assembleur
  Windows. Ils émettent maintenant aussi une variante COFF sous `#ifdef __ELF__` (branche ELF
  inchangée : rien ne change pour la 3DS). À proposer à l'amont.
- Reste : espagnol, italien, allemand, japonais (il faut les ROM ; le japonais a son propre
  localisateur, pas encore essayé sur PC). Taille/CRC32 des dumps autres que BPEF : à relever avec
  `rom_info.py`.

## J6 : état (en cours)
- Fait : dossier utilisateur par OS (`%APPDATA%\EmeraldPC`, `$XDG_DATA_HOME/emerald-pc`), romfs à côté
  de l'exe, chemins UTF-8 (appels larges Windows, testé avec un dossier accentué) ; sauvegarde de
  secours avant la 1re écriture de chaque session dans `backup/<AAAAMMJJ-HHMMSS>/` (testé,
  identique) ; `time_t` 64 bits (`-D_TIME_BITS=64`, sinon l'horloge du jeu s'arrêtait en 2038) ;
  **réinitialisation logicielle réparée** (variables du jeu regroupées, `pc_game_group.c`,
  testé : retour au titre puis CONTINUER) ; marge de 1 Kio après `gDisplayedStringBattle`
  (texte de défaite de Juan) comme sur 3DS ; rapport de plantage `crash.txt` (`pc_crash.c`).
- Fichiers .clock / .nuzlocke / settings : encodés octet par octet, même époque Unix : compatibles.
- Vérifié aussi : `layout_test` **152/152** avec les options J6 ; horloge du jeu fixée au 15 juin
  2040 à 12 h UTC → l'écran du bas affiche **14:00** (heure d'été) : plus de bug de 2038. Les baies
  et les événements datés lisent cette même horloge.
- Reste : une vraie sauvegarde 3DS sur PC et inversement (il faut une save de Tom).
- Piège corrigé : `make pc` produisait un index d'assets vide après le regroupement ; il échoue
  maintenant si l'index est vide.

## J5 : ce qui marche
- `ctr_voxel.c` (9 000 lignes) tourne **sans modification** sur le shim ; les autres unités
  voxel l'étaient déjà. `voxel.v.pica` et `voxel_weather.v.pica` réécrits en GLSL instruction
  par instruction (`pc_port/src/gl/pc_shaders.c`), avec la couleur de sommet bornée à [0, 1]
  comme le fait le PICA ; `romfs/shaders/voxel.shbin` est un marqueur `PCSH:voxel,weather`.
- Trois manques du shim trouvés et comblés :
  - `__C3D_Context` : `ctr_voxel.c` délie une unité de texture en écrivant dans le contexte
    interne de citro3d (décalages 0x20 et 0x118, valables en 32 bits) ; la table des textures
    du shim est maintenant à cet endroit.
  - **File GX** : citro3d lie sa file à `C3D_Init` et le port l'observe (`--wrap=GX_BindQueue`)
    pour savoir s'il reste de la place pour un envoi ; sans file, l'atlas d'une carte n'était
    jamais envoyé et le monde restait en 2D. Le shim lie une file, et le lien PC reprend les
    wraps GX de la console.
- Vérifié (scénarios avec téléportations, option de mesure `CTR_MEASURE_AID` le temps du test) :
  camion, Bourg-en-Vol, maison de Maman, **Route 101, Rosyère, Route 104** (plage, pont, eau
  animée et reflets), bord de **Mérouville** ; traversée **Rosyère → Route 101** à pied : aucun
  trou ni carré noir sur les captures. Ombres portées, brume, reflets, cendres du mont
  Chimney (shader météo).
- Pas encore vérifié : combat en voxel, pluie et tempête de sable, nuit, 3D stéréo (option),
  performances en jeu réel sur la durée (coût moyen mesuré ≈0,4 ms par image, mais le shim
  renvoie les sommets au GPU à chaque dessin : à optimiser si besoin).
- À savoir avant de signaler un défaut du monde 3D : comparer avec `docs/VOXEL_STATUS.md` de
  l'amont. Ex. (remarque de Tom) les petits arbres couverts de cendre du mont Chimney sont
  plats **sur 3DS aussi** (« still flat: no art for it yet ») ; la terre des baies reste plate
  exprès (VOXEL_GUIDE).
- Test accéléré (`EMERALD_PC_NOPACE`) : mettre `audio_clock=0`, sinon le moteur sonore suit le
  temps réel et ce qui attend la musique (un fondu de téléportation) dure des milliers d'images.
- Outil : `EMERALD_PC_SCENARIO_SAVES=1` laisse un scénario écrire sa sauvegarde
  (`pc_port/tests/scenarios/save_in_house.txt` en fait une dans la maison de Maman).

## Demandes de Tom (2026-11-24, après l'essai du J4)
- **Options graphiques dans le jeu**, « comme dans les jeux d'aujourd'hui » : dans OPTIONS
  (catégorie à part ou générale) : résolution / taille de fenêtre, plein écran, disposition,
  mise à l'échelle, filtrage, V-Sync… (prévu au J8, à avancer selon Tom).
- **Manettes** : à prendre en charge (J8 : SDL_Gamepad, remappage).

## J4 : ce qui marche
- **Dispositions** (`pc_port/src/pc_layout.c`, géométrie seule, sans SDL ni GL) : verticale
  (la 3DS), côte à côte, incrustée (petit écran du bas dans le coin du haut ; inversée, petit
  écran du haut **à côté** du grand écran du bas, pour ne pas cacher son dock), un seul écran.
  Échelle entière la plus grande que la fenêtre contient, centrée ; en dessous de 1x, ajustée.
  Touches : **F9** / Maj+F9 disposition suivante / précédente, **Tab** inverser les écrans,
  **F11** ou Alt+Entrée plein écran. Choix gardé dans `settings.txt` (`pc_layout`, `pc_swap`,
  clés inconnues conservées par `3ds_settings.c`, donc sans effet sur la 3DS).
- **Tactile** : un clic sur l'écran du bas (où qu'il soit) commence un appui, qui suit la souris
  en restant collé au bord tant que le bouton est tenu ; un clic sur l'écran du haut n'est
  jamais un appui. Un écran tactile passe par les événements souris de SDL.
- Vérifié : `pc_port/tests/layout_geometry_test.c` (**595/595** : coins → (0,0)/(319,239), milieu,
  bords, échelle entière, proportions, 7 tailles de fenêtre × 4 dispositions × inversé) ;
  scénario `pc_port/tests/scenarios/touch_dock.txt` (taps sur le dock) : **SAC, CARTE,
  OPTIONS, SAUVEGARDE, CARTE DU MONDE** s'ouvrent et se ferment au stylet ; POKéDEX, POKéMON,
  POKéNAV sont grisés à ce moment du jeu (pas encore obtenus), comme sur 3DS. Captures du sac
  ouvert dans les 8 variantes de disposition (`EMERALD_PC_DUMP_LAYOUTS=1`).
- **Bug trouvé et corrigé** : pendant un menu du jeu (sac…), le port « tient » l'écran du haut et
  attend la VBlank sans dessiner de trame ; il écrit le menu directement dans le framebuffer du
  bas, que l'écran de la 3DS affiche tout seul. Le PC ne rafraîchissait la fenêtre qu'en fin de
  trame : fenêtre figée et clavier/souris ignorés pendant les menus. Désormais une VBlank sans
  trame réaffiche les écrans, comme l'écran LCD.
- Pas encore vérifié : la souris réelle, F9/Tab/F11 dans la vraie fenêtre (essai de Tom).
  POKéMON et POKéNAV au stylet restent à revoir plus loin dans le jeu.

## J3 : ce qui marche
- **Cadence** : le premier essai de Tom (`port.log`) montrait le jeu à 1 700–6 000 images/s :
  `C3D_FrameBegin(SYNCDRAW)` et `gspWaitForVBlank()` n'attendaient rien. Le shim a maintenant
  l'horloge VBlank de la console (59,8261 Hz, celle de `3ds_platform.c`), quelle que soit la
  fréquence de l'écran du PC (`PcDisplay_WaitVBlank`, `SDL_DelayPrecise`). Mesuré : `fps=59.8`.
  Accélération (SPEED) et « real-time pacing » du port reposent dessus sans changement.
- **Son** : `3ds_audio.c` et `3ds_sound.c` tournent **sans modification** ; c'est NDSP qui est
  réimplémenté sur SDL3 (`pc_port/src/pc_ndsp.c`, en-têtes NDSP de libctru vendorés). Les
  tampons sont consommés dans l'ordre par le thread audio de SDL (QUEUED → PLAYING → DONE), la
  correction de dérive du port passe par le ratio de fréquence du flux, le rappel de trame
  NDSP réveille le mixeur. Le moteur m4a tourne donc sur son propre thread, à l'horloge du son,
  comme sur New 3DS.
- Vérifié par le scénario `new_game` cadencé (7 400 images, 2 min) avec `SDL_AUDIO_DRIVER=dummy`
  et capture WAV : musique de l'intro, du titre, de Birch, du camion, de Bourg-en-Vol ;
  **0 sous-alimentation en 135 s** ; images identiques au J2. Périphérique réel (WASAPI) ouvert
  sans erreur. Coût : mixeur ≈0,03 ms par image.
- **Grésillement** (écoute de Tom, 2026-11-24) : ni coupure (0 en 30 s sur WASAPI), ni artefact
  de SDL (son mesuré avant/après conversion). C'est le mixeur lui-même (m4a + canaux carrés de
  `cgb_audio.c`, fronts nets à 42 kHz, repliement des aigus), que la console adoucit. Choix de
  Tom à l'écoute (entre « tel quel » et un filtre 12 kHz d'ordre 4) : **passe-bas doux, une
  biquad Butterworth à 14 kHz**, dans `pc_ndsp.c`, réglable (`EMERALD_PC_AUDIO_FILTER`, 0 =
  coupé). Si ça ne suffit pas : canaux carrés à bande limitée dans `cgb_audio.c` (fichier
  partagé, `#ifdef PORT_PC`, utile aussi à la 3DS) ; à proposer à Tom.

## J2 : ce qui marche
- `3ds_video.c`, `3ds_bottom_screen.c`, `3ds_dex_top.c` (≈13 600 lignes) tournent **sans
  modification** sur le shim citro-sur-OpenGL 3.3 (`pc_port/src/gl/`), avec les en-têtes et
  les maths de citro3d/citro2d/libctru et le `base.c` de citro2d vendorés tels quels
  (`pc_port/third_party/`, licence zlib).
- Fenêtre SDL3 (disposition 3DS : haut 400×240 au-dessus du bas 320×240, mise à l'échelle
  entière), clavier → touches 3DS, souris sur l'écran du bas → tactile ; `pc_port/run-dev.ps1`.
- Vérifié par scénario automatique (`pc_port/tests/scenarios/new_game.txt`, généré par
  `pc_port/tools/make_scenario.py`, joué par `3ds_scenario.c`) et captures PNG :
  intro complète (feuilles, vélo, Flygon/Manectric, Groudon/Kyogre), écran titre, menu,
  discours de Birch avec texte, saisie du nom, camion, **Bourg-en-Vol en 2D** (écran large
  400 px), maison de Maman ; écran du bas du port (carte du Hoenn, lieu, horloge, dock).
- Performance : ≈0,6 ms de travail par image en moyenne (RX 9070 XT), aucune image manquée.
- Fonctionne lancé depuis PowerShell normal (DLL SDL3/MinGW à côté de l'exe).

## J2 : choix techniques du shim (pour la suite)
- La « mémoire GPU » (textures, tampons de rendu, framebuffers) reste en mémoire CPU, aux
  formats PICA (tuiles Morton 8×8) ; OpenGL ne sert que de rastériseur. Chaque surface a une
  texture GL miroir, avec deux drapeaux (CPU plus récent / GPU plus récent) ; relecture GL →
  mémoire seulement quand le CPU lit (transferts GX vers l'écran du bas, copies).
- Les lignes PICA sont rangées de haut en bas (t = 1 en ligne 0) : inversées à la frontière
  mémoire ↔ GL (comme Citra). L'écran du haut (cible de rendu) et l'écran du bas (framebuffer
  CPU) n'ont donc pas la même orientation dans GL : géré à l'affichage.
- **Exécution différée** : comme sur la console (liste de commandes soumise à
  `C3D_FrameSplit`/`FrameEnd`), les dessins sont enregistrés avec un instantané de leur état et
  exécutés à la soumission, avant toute opération GX et avant toute libération. Sans ça, les
  tuiles décodées dans l'atlas en cours d'image n'étaient pas vues (calques de l'intro faux).
- Écritures CPU dans les textures détectées par `MEM_WRITE_WATCH` (Windows) ; hachage pour les
  petits blocs. Linux : à faire (mprotect/hachage).
- Shaders : `render2d` de citro2d réécrit en GLSL ; un fragment shader unique émule les
  6 combineurs (sources, opérandes, fonctions, échelles, tampon), le test alpha, la texture
  procédurale de citro2d. Les binaires `.shbin` sont remplacés par des marqueurs `PCSH:nom`.

## J1 : ce qui marche
- `make -C build/upstream-pc/3ds_port -f $PWD/pc_port/pc.mk pc pc-data` (shell MINGW32) produit
  `emerald-pc.exe` (i686, PE) et ses données de développement dans `build/pc-romfs`
  (mode d'emploi : [pc_port/README.md](../pc_port/README.md)).
- Les 857 objets du jeu (C de pokeemerald + données `.s` + unités du port + voxel)
  compilent avec les flags par fichier de `full.mk`, sans modifier aucun fichier partagé.
- 19 unités natives de `3ds_port/src` sont réutilisées telles quelles grâce à libctru-lite
  (`pc_port/compat/3ds.h`) : plateforme, entrée, log, réglages, données, pak, prof,
  scénario, horloge, Nuzlocke…
- Exécution sans fenêtre : démarrage complet (index des assets 6 464 entrées,
  cartes 882 descripteurs, ressources de l'écran du bas, mixeur m4a), jeu arrivé dans
  `MainCB2_Intro`, 601 images, arrêt propre (`EMERALD_PC_FRAMES=600`).
- Sauvegardes : `pc_port/tests/layout_test.py` → **152/152** tailles et offsets identiques
  ARM ⇄ i686 (SaveBlock1 15 752 o, SaveBlock2 3 884 o, PokemonStorage 33 744 o).
  Le test détecte bien une différence (canari MS-bitfields : 4 vs 8 octets sans `-mno-ms-bitfields`).

## Décisions
- D1 (2026-11-23, Tom) : **shim citro-sur-GL** (option A). `3ds_video.c`, `ctr_voxel.c`
  et `3ds_dex_top.c` restent tels quels.
- Toolchain (2026-11-23, Tom) : **aucune distribution Linux sur le PC de Tom**. MSYS2 portable
  dans `A:\msys64` (à supprimer à la fin du projet, avec `A:\msys2-download`).
  Le J1 se fait donc sous **Windows i686 (MinGW, PE)** ; Linux viendra en CI GitHub.
- Système de build (Claude, réversible) : **surcouche `pc_port/pc.mk`** sur `3ds_port/Makefile`
  + `full.mk` (variables écrasées par `override`), pas de CMake. Arbre séparé
  `build/upstream-pc` (`bootstrap.py --dir`), donc `build/` distinct du build 3DS.
- Même logique que D1 pour la plateforme (Claude, réversible) : `3ds_platform.c`,
  `3ds_input.c` et les autres natifs « légers » sont gardés ; c'est libctru-lite qui
  s'adapte (APT, HID, threads…). En J2, HID sera alimenté par SDL (`PcHid_SetState`).
- Build de **développement** : scripts, chansons et données du jeu sont **liés dans l'exe**
  (`pc_bundles.c`) au lieu des bundles. Cet exe contient du contenu du jeu (issu des
  sources de la décompilation) : **jamais distribué**.
- Assembleur i686 PE : `--divide` (sinon `/` = commentaire), `.align N` → `.p2align N`
  (`pc_port/tools/pc_asm.py`, qui développe aussi les `.include`), `.pushsection` remplacé.
- Préfixe `_` des symboles C en 32 bits Windows : les objets assemblés sont recopiés avec
  `objcopy --redefine-syms` (règle `build/pcus/%.o`).
- Pas d'ASLR (`--disable-dynamicbase`) : l'index des assets contient des adresses absolues.
  La section `.reloc` est gardée pour les outils de release.
- D2 (2026-11-25, Tom) : release par recette (voir « J9 : état »). D3 : un exe par langue.

## Pièges rencontrés
- **`make pc` sans `pc-data`** : l'index des assets du romfs PC contient des adresses absolues
  dans l'exe ; après un nouveau lien, un ancien index lit de mauvais octets (écrans noirs,
  palettes fausses). `pc` reconstruit maintenant toujours `pc-data`.

## Bugs connus / limites
- **Plantage intermittent** (segfault) vu une fois au J4, à Bourg-en-Vol (image ≈6 500 du
  scénario, joué plus vite que le temps réel avec `EMERALD_PC_NOPACE`) ; non reproduit en 8 essais sous gdb
  (gdb 64 bits de MSYS2, qui débogue l'exe 32 bits). Piste : le moteur sonore sur son propre
  thread depuis le J3. À surveiller ; relancer sous gdb s'il revient.
- 3D stéréo : l'œil droit n'est pas affiché (option prévue au J5).
- Local sans fil absent (stubs) ; Mystery Gift : stubs qui s'arrêtent net (code
  inaccessible sur 3DS aussi, retiré là-bas par `--gc-sections`).
- `fopencookie` (backend pak) passe par un fichier temporaire : lent, à remplacer avant
  d'utiliser le pak sur PC.
- `romfs/engine/abi.bin` non généré pour le romfs PC (sans effet sur le backend romfs).
- `buildinfo` : `dirty=501` n'a pas de sens dans l'arbre bootstrapé (idem amont).

## Notes d'environnement
- Dépôt local : `core.autocrlf=false` obligatoire sous Windows, sinon `bootstrap.py` échoue.
- MSYS2 : `ParallelDownloads = 1` dans `A:\msys64\etc\pacman.conf` (les miroirs renvoient
  des 429 en parallèle). Outils de la décompilation compilés en UCRT64, jeu en MINGW32
  (`/ucrt64/bin` ajouté en fin de PATH par `pc.mk`).
- Clone de ZallaxDev/emerald-3ds (`origin`) ; le fork GitHub de Tom n'est pas encore créé.
  Rien n'est commité.

## Dumps vérifiés (pour recomplauncher.json)
| Langue | Code | Taille | SHA-1 | CRC32 |
|---|---|---|---|---|
| Français | BPEF | 16 777 216 | `ca666651374d89ca439007bed54d839eb7bd14d0` (= recette, FRENCH.md) | `a3fdccb1` |
