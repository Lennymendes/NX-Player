# NX Player 1.0 — lecteur bit-perfect PCM · DXD · DSD

Lecteur audiophile de la **NX Audio Suite** (AuraEngine DSD, AuraDAW). C++20 · JUCE 8.0.15 · macOS (CoreAudio) ·
Windows (ASIO, WASAPI exclusif) · Linux (ALSA). Licence **MIT** (voir « Licences » pour JUCE et le SDK ASIO).

- **Moteur bit-perfect** : les échantillons entiers ne passent jamais par le flottant ; DSD natif (ASIO),
  DoP partout, conversion DSD → DXD 64 bits si le DAC ne prend que le PCM ; gapless ; changement automatique de
  fréquence du DAC ; Signal Path façon Roon.
- **Bibliothèque unifiée** (SQLite, recherche plein texte) : fichiers locaux et partages montés surveillés,
  serveurs UPnP / DLNA, Qobuz, Tidal, Spotify et YouTube Music.
- **Interface façon Cider** : verre dépoli sur la pochette floutée, paroles synchronisées, mini-lecteur.
- **Extensible** : thèmes `.nxtheme` rechargés à chaud (CSS + shader GLSL), plug-ins d'interface en
  JavaScript, plug-ins DSP en C.

Captures (rendues par le banc d'essai) : `docs/screenshots/`.

## Compiler

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release          # JUCE 8.0.15 téléchargé automatiquement
cmake --build build --config Release
```

| Système | Particularités |
|---|---|
| macOS 11+ | rien à installer ; l'app n'est pas signée : clic droit › Ouvrir au premier lancement |
| Windows | Visual Studio 2022. **ASIO** (PCM + DSD natif) : `-DNX_ASIO_SDK_DIR=C:/SDK/asiosdk` (SDK Steinberg, non fourni). Sans lui : WASAPI exclusif |
| Linux | `libasound2-dev libcurl4-openssl-dev libfreetype-dev libfontconfig1-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxcomposite-dev libgl1-mesa-dev libsqlite3-dev` |

Options CMake : `NX_JUCE_DIR` (JUCE local), `NX_ENABLE_OPENGL` (shaders de thèmes, ON), `NX_LTO` (OFF ; le linker Xcode récent plante avec LTO),
`NX_BUNDLED_SQLITE` (toujours actif sous Windows).

La CI (`.github/workflows/build.yml`) compile macOS, Windows (avec ASIO) et Linux, lance le banc d'essai et publie
les archives sur une étiquette `v*`.

## Banc d'essai

```bash
"build/NXPlayer_artefacts/Release/NX Player" --selftest [filtre]        # Linux / Windows
"build/NXPlayer_artefacts/Release/NX Player.app/Contents/MacOS/NX Player" --selftest
```

18 sections, 130 vérifications, code de sortie 0 si tout passe. Elles écrivent les fichiers de test et les
captures d'écran dans le dossier temporaire `NXPlayer_selftest`.

| # | Section | Ce qui est prouvé |
|---|---|---|
| 101 | Ring SPSC, encodeur exact | les 16 777 215 valeurs 24 bits traversent chaque règle float → entier (CoreAudio, AudioData, ASIO JUCE, 32 bits tronqué) avec leurs 24 bits de poids fort intacts ; les 131 072 mots DoP possibles aussi |
| 102 | WAV, RF64, AIFF, AIFF-C | 8 / 16 / 24 / 32 bits, 24 dans 32, flottant 32 / 64, seek, bit à bit |
| 103 | FLAC, ALAC | décodage entier identique ; ALAC encodé par le codec Apple, seek au milieu d'un paquet |
| 104 | DSF, DFF | LSB / MSB first, DSD128, seek |
| 105 | DSP 64 bits | DSD64 / 128 → DXD (amplitude exacte), sinc 44,1 → 352,8, 96 → 44,1, 44,1 → 48 : erreur < −176 dB |
| 106 | Moteur | 3 pistes 24 / 96 enchaînées sans trou ni recouvrement, 0 allocation sur le fil audio, changement de fréquence à la frontière, mono → 2 voies |
| 107 | DSD | DoP (marqueurs alternés, charge intacte), DSD natif octet pour octet, DSD → PCM |
| 108 | Moteur | seek à l'échantillon, volume (dither TPDF ≤ 1 LSB), suréchantillonnage, Signal Path |
| 201–204 | Bibliothèque | tags (FLAC, ID3v2 dans WAV / DSF, MP4), SQLite + FTS5 (recherche sans accents), veille de dossiers, LRC |
| 205 | Connecteurs | signature Qobuz, PKCE (RFC 7636), JSON Tidal / Qobuz / Spotify, MPD + fMP4 FLAC → flux FLAC identique, DIDL-Lite, SSDP, fusion des doublons |
| 206 | NAS | serveur HTTP local → flux progressif → moteur → DAC : bit-perfect |
| 301 | Interface | chaque page rendue hors écran, thème appliqué à chaud, lecture lancée depuis l'interface |
| 401–403 | Extensions | CSS / var(), rechargement à chaud, export / import ; plug-ins JS (rendu, erreurs, setLayout, rechargement) ; plug-in DSP C dans la chaîne |

Le DAC du banc est virtuel (`CaptureBackend`) : il enregistre ce que le moteur envoie. Les pilotes réels
(CoreAudio, ASIO, WASAPI) ne sont pas testés par ce banc ; voir « Limites ».

## Architecture

| Dossier | Contenu |
|---|---|
| `Source/Core` | formats, ring SPSC sans verrou, encodeur exact float ↔ entier, littéraux UTF-8 |
| `Source/Audio` | décodeurs WAV / RF64 / BWF, AIFF / AIFF-C, FLAC, ALAC (MP4), DSF, DFF, MP3 / Vorbis (avec pertes) |
| `Source/DSP` | DSD → PCM (FIR à tables, 150 dB), rééchantillonneur polyphase L/M (160 dB), requantification TPDF, DoP |
| `Source/Engine` | plan de lecture, moteur (fil de décodage → ring → fil audio), chaîne DSP, Signal Path |
| `Source/Output` | sorties : JUCE (CoreAudio + Hog Mode, ALSA, WASAPI partagé), `win/` WASAPI exclusif et ASIO natifs, DAC virtuel |
| `Source/Library` | SQLite, tags, scanner + veille, paroles (LRC, LRCLIB) |
| `Source/Providers` | flux HTTP progressifs, UPnP / DLNA, Qobuz, Tidal (DASH fMP4), Spotify, YouTube Music, fusion |
| `Source/GUI` | interface (fond, verre, barre latérale, pages, En cours, paroles, Signal Path, mini-lecteur) |
| `Source/Ext` | thèmes, plug-ins JS et DSP, analyseur (spectre, VU, oscilloscope) |
| `Themes`, `Plugins` | 2 thèmes, 2 plug-ins d'interface, 1 plug-in DSP d'exemple |
| `ThirdParty/alac` | codec ALAC d'Apple (Apache 2.0), endianness corrigée pour arm64 |

### Le chemin bit-perfect

1. Le décodeur rend les entiers tels quels, justifiés à gauche dans un `int32` (un échantillon 24 bits *k*
   devient *k* << 8). Le flottant n'apparaît que pour les sources flottantes ou avec pertes.
2. Le **plan** de chaque piste choisit le transport : PCM à la fréquence du fichier, DSD natif, DoP, ou une
   conversion explicite (sinc, DSD → PCM) quand le DAC l'exige ou que vous la demandez.
3. Un **fil de décodage** remplit un ring sans verrou (8 Mo). Les changements de piste, seeks et changements
   de format voyagent dans une seconde file d'événements qui porte la position absolue dans le ring : le fil audio
   change de piste à l'échantillon près et jette les données périmées après un seek, sans verrou ni allocation.
4. Deux pistes au même format s'enchaînent sans rouvrir le DAC (gapless, filtres continus) ; un autre format
   rouvre le DAC à la frontière exacte.
5. Le volume, le DSP et les conversions ne s'appliquent que lorsqu'ils sont actifs (volume < 100 %, plug-in,
   suréchantillonnage) ; ils passent par du 64 bits et un dither TPDF vers la résolution du DAC.
6. **Sorties** :
   - **WASAPI exclusif** et **ASIO** (Windows) écrivent les entiers directement dans les tampons du pilote (16,
     24, 24 dans 32, 32 bits). ASIO bascule en **DSD natif** (`kAsioSetIoFormat`) quand le pilote le propose.
   - **CoreAudio** (macOS) prend le **Hog Mode**, choisit le format physique entier le plus profond à la fréquence
     voulue et désactive le mixage. L'IOProc est en float : chaque entier est envoyé comme *k*/2³¹, que le HAL
     reconvertit exactement (24 bits significatifs). Les sources 32 bits entiers y sont donc limitées à 24 bits.
   - **ALSA** (Linux) : périphériques `hw:` (exclusifs), avec l'encodeur exact adapté à la règle de conversion de JUCE.
   - Le WASAPI partagé reste proposé en secours ; le Signal Path le signale (« mixeur du système »).

Le **Signal Path** reprend les catégories de Roon : *Bit-perfect* (violet), *Amélioré* (traitement volontaire),
*Haute qualité* (sans perte mais modifié), *Qualité réduite* (source avec pertes, mixeur du système). Cliquez sur
le badge de qualité de la barre de lecture.

## Sources et comptes

Réglages des comptes : page de chaque source (barre latérale). Les jetons sont stockés dans `accounts.json` du
dossier utilisateur (droits 600 sous macOS / Linux).

| Source | Accès | Moteur bit-perfect |
|---|---|---|
| Fichiers locaux, SMB / NFS montés | dossiers surveillés (mtime des dossiers, scan complet toutes les 10 min) | oui |
| NAS UPnP / DLNA | découverte SSDP, Browse / Search ContentDirectory, import d'un dossier | oui (flux HTTP) |
| Qobuz | API partenaire : `app_id` / `app_secret` délivrés par Qobuz, puis e-mail / mot de passe | oui, FLAC jusqu'à 24 / 192 |
| Tidal | `client_id` délivré par Tidal, connexion par code sur link.tidal.com | oui : FLAC (BTS) et FLAC Max (DASH fMP4 reconditionné en FLAC) ; les flux chiffrés sont refusés |
| Spotify | votre `client_id` (developer.spotify.com, redirection `http://127.0.0.1:43821/callback`), PKCE | non : lecture confiée à Spotify Connect |
| YouTube Music | clé d'API YouTube Data v3, playlists publiques par identifiant | non : lecture sur music.youtube.com |

NX Player n'extrait aucun identifiant des applications des services et ne contourne aucune protection : Spotify
et YouTube ne délivrent pas de flux audio aux applications tierces, et leurs conditions interdisent de capter
l'audio. Les endpoints Qobuz et Tidal sont ceux de leurs programmes partenaires ; ils peuvent évoluer.

Paroles : fichier `.lrc` à côté du morceau, puis tags (USLT, LYRICS), puis LRCLIB (désactivable dans les réglages).

## Utilisation

| Action | Touche |
|---|---|
| Lecture / pause | Espace |
| ± 10 s | ← / → |
| Piste précédente / suivante | Cmd/Ctrl + ← / → |
| Rechercher | Cmd/Ctrl + F |
| Paroles | Cmd/Ctrl + L |
| Mini-lecteur | Cmd/Ctrl + M |

Volume : à 100 % le flux reste bit-perfect ; en dessous, volume numérique 64 bits + dither. Double-clic sur un
morceau pour le lire, clic droit pour la file d'attente, l'album, l'artiste, les crédits.

Thèmes : `docs/THEMES.md`. Plug-ins : `docs/PLUGINS.md`.

## Limites connues

- **Matériel** : développé et testé sous Linux sans carte son. Les sorties WASAPI exclusif et ASIO sont compilées
  et liées avec le compilateur croisé llvm-mingw, la partie macOS (Hog Mode, format physique) n'a pas pu être
  compilée ici : la CI macOS / Windows est la première vraie compilation de ces fichiers, et un essai avec un DAC
  (RME ADI-2, Merging) reste à faire.
- **DSD** : DST (DFF compressé) non pris en charge. Le DoP suppose un DAC qui reconnaît les marqueurs.
- **Formats** : AAC seulement sous macOS (décodeur système) ; pas de WavPack, APE, MQA.
- **Streaming** : Qobuz et Tidal demandent des identifiants partenaires que ce dépôt ne fournit pas ; ces
  connecteurs n'ont été testés que sur des réponses enregistrées (JSON, MPD, fMP4). Seek dans un flux HTTP : attend
  que l'octet visé soit téléchargé.
- **Shaders GLSL** : rendu OpenGL non testé sur ce banc (pas de GPU) ; le dégradé de repli l'est.
- **Interface** : rendue hors écran et lancée sous X virtuel ; une séance d'utilisation réelle reste à faire.
  Pas de bord de fenêtre personnalisé (barre de titre native).

## Licences

Le code de NX Player est sous licence MIT (`LICENSE`). Il s'appuie sur :
- **JUCE 8** : AGPLv3 ou licence commerciale. Distribuer un binaire de NX Player impose donc l'une des deux
  (code source complet sous AGPLv3, ou licence JUCE).
- **SDK ASIO** (optionnel, non fourni) : licence Steinberg ou GPLv3. Un binaire compilé avec ASIO suit l'une d'elles.
- **ALAC** (Apple, Apache 2.0), **SQLite** (domaine public), **QuickJS** (MIT, via JUCE).

Détails : `THIRD_PARTY_NOTICES.md`.
