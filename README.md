# NX Player 1.0 — lecteur bit-perfect PCM · DXD · DSD

Lecteur audiophile de la **NX Audio Suite** (AuraEngine DSD, AuraDAW). C++20 · JUCE 8.0.15 · macOS (CoreAudio) ·
Windows (ASIO, WASAPI exclusif) · Linux (ALSA). Licence **MIT** (voir « Licences » pour JUCE et le SDK ASIO).

- **Moteur bit-perfect** : les échantillons entiers ne passent jamais par le flottant ; DSD natif (ASIO),
  DoP partout, conversion DSD → DXD 64 bits si le DAC ne prend que le PCM ; gapless ; changement automatique de
  fréquence du DAC ; Signal Path façon Roon.
- **Bibliothèque locale et réseau** (SQLite, recherche plein texte) : fichiers locaux et partages montés
  surveillés, serveurs UPnP / DLNA du NAS (découverte automatique ou adresse IP, navigation par dossiers et pochettes).
  Les connecteurs de streaming (Qobuz, Tidal, Spotify, YouTube Music) restent dans le code mais sont retirés de
  l'interface pour l'instant (`-DNX_ENABLE_STREAMING=ON` pour les rétablir).
- **Interface façon Cider 2** : verre dépoli sur la pochette floutée, barre latérale en trois piliers (Local,
  Favoris, Playlists) étendue ou compacte, filtres par qualité en pastilles (DXD, DSD, Hi-Res, CD), pages artistes
  (photo, bannière, biographie), réglages en fenêtre modale avec tableau de bord matériel, vue immersive plein écran,
  assistant de premier démarrage, interface en français ou en anglais.
- **Matériel** : profils dédiés Merging Anubis et RME ADI-2 (sélecteur dans la barre de lecture), Memory Play
  (morceau entier en RAM), veille empêchée pendant la lecture, Outils Studio (mono, Mid / Side, phase, crossfeed),
  pont vers AuraEngine X.
- **NX Remote** : QR code à scanner, le téléphone pilote la lecture, le volume, la boucle A-B et les profils
  depuis son navigateur (serveur local démarré à la demande).
- **Extensible** : page « Extensions & Thèmes » (galerie à la Spicetify, aperçus en direct, activation en un clic),
  catalogue de plug-ins en plus, thèmes `.nxtheme` rechargés à chaud (CSS + shader GLSL, disposition de la vue
  immersive), plug-ins d'interface en JavaScript, plug-ins DSP en C.

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
`NX_ENABLE_STREAMING` (OFF : sources Qobuz / Tidal / Spotify / YouTube dans l'interface),
`NX_BUNDLED_SQLITE` (toujours actif sous Windows).

La CI (`.github/workflows/build.yml`) compile macOS, Windows (avec ASIO) et Linux, lance le banc d'essai et publie
les archives sur une étiquette `v*`.

## Banc d'essai

```bash
"build/NXPlayer_artefacts/Release/NX Player" --selftest [filtre]        # Linux / Windows
"build/NXPlayer_artefacts/Release/NX Player.app/Contents/MacOS/NX Player" --selftest
```

27 sections, 288 vérifications, code de sortie 0 si tout passe. Elles écrivent les fichiers de test et les
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
| 109 | Profils matériel | détection Merging / RME, plans des 5 profils, filtre à phase minimale (même amplitude, énergie au début), DSD64 -> DXD +6 dB limité sans écrêtage, DoP « Sync Safe » : 1,5 s de silence DoP puis DSD intact |
| 110 | Boucle A-B | 8 tours bit-perfect hors des 4 ms de fondu, période exacte, horloge affichée dans la boucle, DoP sur mots entiers, DAC jamais reconfiguré |
| 111 | Memory Play, Outils Studio | fichier relu en RAM octet pour octet (seek, mémoire rendue), lecture 24 / 96 bit-perfect avec et sans Memory Play, nœuds « Moteur » et « DAC » du Signal Path ; mono, solo Mid / Side, phase (échantillons exactement opposés), crossfeed bs2b (−7 dB à 100 Hz, bien plus bas à 10 kHz), outils dans le moteur (L = R), DSD converti en PCM tant qu'un outil est actif |
| 201–204 | Bibliothèque | tags (FLAC, ID3v2 dans WAV / DSF, MP4), SQLite + FTS5 (recherche sans accents), veille de dossiers, LRC |
| 205 | Connecteurs | signature Qobuz, PKCE (RFC 7636), JSON Tidal / Qobuz / Spotify, MPD + fMP4 FLAC → flux FLAC identique, DIDL-Lite, SSDP, fusion des doublons |
| 206 | NAS | serveur HTTP local → flux progressif → moteur → DAC : bit-perfect |
| 207 | OAuth | serveur de retour local : préconnexion du navigateur, /favicon.ico, code reçu |
| 208 | NAS UPnP / DLNA | faux NAS sur 127.0.0.1 : M-SEARCH unicast, Browse paginé (serveur plafonné à 2 par page), meilleure ressource, erreurs SOAP, import, lecture bit-perfect 24 / 96 |
| 209 | Hors-ligne | téléchargement depuis un faux NAS : fichier original copié octet pour octet (Artiste / Album / NN Titre), pochette, ajout à la bibliothèque, badge hors-ligne relié à l'original |
| 210 | Bibliothèque v1 | filtres DXD / DSD / Hi-Res / CD (le MP3 exclu), genres, favoris (morceaux et albums), playlists (ajout d'un morceau puis d'un album, retrait, renommage, suppression), badge NAS, retrait d'un album NAS, des serveurs indisponibles, purge du NAS |
| 211 | Pages artistes | faux TheAudioDB + Wikipédia : bon artiste parmi plusieurs résultats, bio française nettoyée, photo, bannière, repli Wikipédia, cache 30 jours sans nouvelle requête, file d'attente en arrière-plan |
| 301 | Interface | chaque page rendue hors écran (NAS, galerie, plug-ins), thème à chaud, installation / retrait d'un plug-in du catalogue, historique ‹ ›, barre latérale masquée ; v1 : barre latérale en piliers, pastilles de qualité, glisser-déposer vers Favoris et une playlist, page artiste, badge NAS + retrait + purge, fenêtre Réglages et tableau de bord (matrice des fréquences), NX Remote (volume et profil depuis le téléphone), sélecteur de profils, Outils Studio, vue immersive (palette, forme d'onde, disposition du thème puis des réglages puis d'un plug-in), assistant en 4 étapes, interface anglaise compacte |
| 401–403 | Extensions | CSS / var(), rechargement à chaud, export / import ; plug-ins JS (rendu, erreurs, setLayout, rechargement) ; plug-in DSP C dans la chaîne |
| 501 | NX Remote, système | serveur HTTP : page web, 403 sans jeton, état JSON, commandes GET / POST sur le fil de l'interface, bibliothèque et pochettes, révocation par nouveau code, arrêt ; QR code (motifs de repérage) ; commande AuraEngine X ; RAM / CPU mesurés ; dictionnaire anglais (≥ 300 entrées) appliqué à chaud |

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
| `Source/GUI` | interface (fond, verre, barre latérale, pages, En cours, paroles, Signal Path, mini-lecteur, fenêtre Réglages + tableau de bord, assistant, vue immersive, dessins des DAC) |
| `Source/Remote` | NX Remote : serveur HTTP local, jeton, API JSON (la web app est `Resources/remote/index.html`) |
| `Source/Platform` | touches multimédia, mesures système (CPU, RAM), anti-veille, pont AuraEngine X |
| `Resources/i18n` | dictionnaire anglais `en.json` (clé = texte français) ; `tools/i18n_check.py` liste les textes sans traduction |
| `Source/Ext` | thèmes, plug-ins JS et DSP, analyseur (spectre, VU, oscilloscope) |
| `Themes`, `Plugins` | 2 thèmes, 2 plug-ins d'interface, 1 plug-in DSP d'exemple |
| `ThirdParty/alac` | codec ALAC d'Apple (Apache 2.0), endianness corrigée pour arm64 |
| `ThirdParty/qrcodegen` | générateur de QR codes de Project Nayuki (MIT) |

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

## Sources

| Source | Accès | Moteur bit-perfect |
|---|---|---|
| Fichiers locaux, SMB / NFS montés | dossiers surveillés (mtime des dossiers, scan complet toutes les 10 min) | oui |
| NAS UPnP / DLNA | découverte SSDP au lancement (M-SEARCH MediaServer + ContentDirectory, répétée, et écoute des annonces), serveurs mémorisés d'une session à l'autre, ou adresse IP du NAS (M-SEARCH unicast, utile si le réseau filtre le multicast) ; dossiers en grille avec pochettes, « Lire ce dossier », import dans la bibliothèque | oui (flux HTTP, reprise par Range) |

Testé avec un faux serveur qui se comporte comme les vrais (pages plafonnées, plusieurs `<res>`, erreurs SOAP).
Serveurs visés : MinimServer, Asset UPnP, Synology Media Server, QNAP, JRiver, Jellyfin, foobar2000. Sous macOS,
le système demande l'autorisation « Réseau local » la première fois que vous ouvrez la page NAS (aucun scan au
démarrage) : acceptez-la, sinon aucun serveur n'apparaît. L'app est signée ad hoc avec un identifiant fixe
(`com.nxaudio.nxplayer`) pour que macOS retienne la réponse ; une nouvelle version téléchargée peut la redemander une fois.

Les connecteurs de streaming restent dans le code (`Source/Providers/StreamingProviders.*`, tests 205 et 207) ;
avec `-DNX_ENABLE_STREAMING=ON` ils reviennent dans la barre latérale. NX Player n'extrait aucun identifiant des
applications des services et ne contourne aucune protection.

Paroles : fichier `.lrc` à côté du morceau, puis tags (USLT, LYRICS), puis LRCLIB (désactivable dans les réglages).

Identifiants des services (si le streaming est réactivé) : options CMake ou secrets GitHub `NX_SPOTIFY_CLIENT_ID`,
`NX_QOBUZ_APP_ID`, `NX_QOBUZ_APP_SECRET`, `NX_TIDAL_CLIENT_ID`, `NX_TIDAL_CLIENT_SECRET`.

## Utilisation

| Action | Touche |
|---|---|
| Lecture / pause | Espace |
| ± 10 s | ← / → |
| Piste précédente / suivante | Cmd/Ctrl + ← / → |
| Rechercher | Cmd/Ctrl + F |
| Paroles | Cmd/Ctrl + L |
| Mini-lecteur | Cmd/Ctrl + M |
| Page précédente / suivante (‹ ›) | Cmd/Ctrl + [ / ], ou Alt + ← / → |
| Touches multimédia du clavier (lecture, suivant, précédent) | aussi quand NX Player est en arrière-plan |
| Masquer / afficher la barre latérale | Cmd/Ctrl + B |
| Boucle A-B : début / fin / arrêt | I / O / L |
| Vue immersive (plein écran) | F, Échap pour sortir |
| Réglages | Cmd/Ctrl + , (ou la roue crantée en haut à droite) |

Volume : à 100 % le flux reste bit-perfect ; en dessous, volume numérique 64 bits + dither. Double-clic sur un
morceau pour le lire, clic droit pour la file d'attente, l'album, l'artiste, les crédits.

**Premier démarrage** : un assistant en 4 étapes (langue et thème, disposition, matériel détecté avec son profil
optimal en un clic, dossier musical et NAS / Jellyfin). Il se relance depuis Réglages › Interface.

**Bibliothèque** (barre latérale › Local) : pastilles Albums · Artistes · Pistes · Genres · Ajouts récents, puis les
filtres de qualité DXD (≥ 352,8 kHz) · DSD · Hi-Res (> 48 kHz ou > 16 bits) · CD (16 bits, 44,1 / 48 kHz), avec le
nombre de morceaux. **Favoris** : cœur sur chaque morceau, clic droit › Ajouter aux favoris sur un album.
**Playlists** : clic droit › « Ajouter à une playlist » sur un morceau ou un album entier (ou une sélection d'albums,
Cmd / Ctrl + clic, Maj + clic), ou glisser-déposer sur une playlist de la barre latérale (sur « Playlists » : nouvelle
playlist). **Artistes** : photos en médaillons, page avec bannière, biographie et discographie de la bibliothèque
(TheAudioDB, sinon Wikipédia ; seul le nom de l'artiste est envoyé, réponses gardées 30 jours, désactivable).
**NAS / Jellyfin** : badge « NAS » (ou « JELLYFIN ») à côté du format ; clic droit › Retirer de la bibliothèque (un
album, une sélection, tous les albums d'un serveur) ; Réglages › Bibliothèque & NAS › « Retirer les indisponibles » ou
« Purger le NAS ». Les fichiers restent sur le serveur.

**Réglages** (roue crantée) : fenêtre par-dessus l'interface. L'onglet Audio & Système commence par le tableau de
bord : carte ORDINATEUR (système, RAM et CPU en direct, Memory Play, veille) reliée à la carte PÉRIPHÉRIQUE AUDIO
(dessin du DAC, matrice des fréquences : plein = accepté, contour = DSD en DoP, halo = en cours) par le type de
liaison (intégré, USB, réseau) et le cadenas d'accès exclusif.

**Barre de lecture** : à droite, la miniature du DAC et son profil (menu : les profils Anubis / RME, ou bit-perfect /
DSD -> DXD / suréchantillonnage pour un autre DAC) ; le badge de qualité ouvre le Signal Path (diodes colorées :
Source → Décodage → Moteur NX 64 bits / Memory Play → Sortie exclusive → DAC + profil) ; le bouton Outils Studio
(mono L+R, solo Mid ou Side, inversion de phase Ø, Smart Crossfeed, « Analyser / Convertir dans AuraEngine X ») ; le
bouton Vue immersive.

**Memory Play** (actif par défaut) : chaque fichier local est copié en RAM dès le début de la lecture (en arrière-plan,
la musique démarre tout de suite) ; plus d'accès disque pendant l'écoute. Limite par fichier réglable (1,5 Go par
défaut). **Veille** : macOS et Windows restent éveillés pendant la lecture (l'écran peut s'éteindre).

**Outils Studio** : contrôles d'écoute en 64 bits. Ils font sortir du bit-perfect (le Signal Path l'indique) ; tant
qu'un outil est actif, le DSD est converti en DXD pour qu'il s'applique. Rien n'est mémorisé d'une session à l'autre.
**AuraEngine X** : clic droit › « Analyser / Convertir dans AuraEngine X » ouvre les fichiers locaux dans
l'application (emplacement détecté ou choisi dans Réglages › Outils Studio).

**Vue immersive** (F) : fond animé aux couleurs de la pochette, pochette HD, paroles synchronisées, forme d'onde
cliquable (calculée en arrière-plan), infos DSD / DXD, VU-mètres et spectre en option. Le menu ⊞ choisit la
disposition et les widgets ; les thèmes (`immersive.json`, variables `--nx-immersive-*`) et les plug-ins
(`nx.ui.setLayout({ immersive: … })`, emplacement `immersive.overlay`) peuvent la modifier (voir `docs/THEMES.md`).

**NX Remote** (icône téléphone en haut à droite, ou Réglages › NX Remote) : activez, puis scannez le QR code avec un
téléphone du même réseau Wi-Fi. La page web pilote lecture, position, volume (verrouillé à 100 % avec la sécurité RME),
boucle A-B, profils Anubis / RME, et lance un album ou un morceau de la bibliothèque. Le lien contient un jeton
aléatoire ; « Nouveau code » révoque les appareils déjà connectés. Le serveur ne tourne qu'à la demande (HTTP en
clair sur le réseau local : à n'activer que sur un réseau de confiance).

**Profils matériel** (Réglages › Audio & Système › Profil matériel, détection automatique par le nom du périphérique) :

| Interface | Profil | Ce qu'il fait |
|---|---|---|
| Merging Anubis / RAVENNA (MT VAD) | Studio DXD Reference *(défaut)* | DSD64 à DSD512 -> DXD 352,8 kHz (FIR phase linéaire 64 bits, gain 0 / +3 / +6 dB + limiteur −0,2 dBFS) ; PCM bit-perfect ; aucun DoP sur le réseau |
| | Analog Warmth | DSD et PCM -> DXD 352,8 / 384 kHz, filtre à phase minimale (sans pré-écho), harmonique paire −90 dB et −0,5 dB au-dessus de 18 kHz (débrayable) |
| | Pure DoP 1-Bit | DoP 1.0, tampon 2048, 2 canaux, 1,5 s de silence DoP à chaque changement de fréquence (verrouillage PTP) |
| RME ADI-2 (USB) | SteadyClock DoP Native *(défaut)* | DoP (jusqu'au DSD256 si l'ADI-2 annonce la fréquence), synchro 150 ms, PCM bit-perfect, option « Sécurité DSD Direct » (volume logiciel verrouillé à 100 %) |
| | AKM Velvet DXD | DSD -> DXD 352,8 kHz +6 dB, phase minimale : l'EQ, le loudness et le crossfeed de l'ADI-2 restent utilisables |

Le badge de la barre de lecture affiche le chemin actif : `DSD64 → DXD 352,8 kHz [Anubis Ref]`, `PCM 44,1 → DXD [Warm]`,
`DSD128 DoP [RME Bit-Perfect]`.

**Boucle A-B** : bouton [A-B] à côté de Répéter (1er clic = A, 2e = B et boucle, 3e = arrêt), touches I / O / L, marqueurs
A et B déplaçables sur la barre de progression. La boucle se fait dans le décodeur : le périphérique n'est jamais arrêté
ni reconfiguré ; fondu de 4 ms en PCM, coupure sur des mots DoP entiers en DSD. Poser ou retirer la boucle relit une fois
le tampon depuis la position de lecture (≈ 150 ms).

**Touches multimédia et « À l'écoute » du système** : macOS (`MPRemoteCommandCenter` / `MPNowPlayingInfoCenter`,
Centre de contrôle, Touch Bar, casques) et Windows (`SystemMediaTransportControls` : touches du clavier, panneau du
volume, écran de verrouillage) reçoivent le titre, l'artiste, l'album, la durée, la position et la pochette.

**Hors-ligne** : sur un morceau d'un NAS / Jellyfin, l'icône ↓ (ou clic droit › Télécharger pour l'écoute hors-ligne,
ou « Télécharger » sur un album / un dossier du NAS) copie le fichier original en arrière-plan dans le dossier choisi
dans Réglages › Téléchargements (par défaut `Musique/NX Player`, rangé Artiste / Album). La progression s'affiche dans
la liste et en bas de la barre latérale ; le morceau rejoint ensuite la bibliothèque avec la mention « Hors-ligne ».

**Paroles** : fichier `.lrc`, tags, puis LRCLIB (exact, puis titre nettoyé, puis recherche par durée), puis
lyrics.ovh en secours (texte non synchronisé).

**Extensions & Thèmes** (barre latérale › Personnaliser ; l'onglet « Plugins utilisateur › » mène au catalogue) : chaque thème et chaque plug-in est une carte avec un
aperçu en direct ; un clic sur l'interrupteur l'active ou le désactive, un clic sur un thème l'applique.
**Plugins utilisateur** : le *Catalogue* propose des plug-ins en plus livrés avec NX Player (Oscilloscope,
Spectre circulaire, Spectrogramme, Crêtes-mètre numérique, Horloge & session d'écoute, DSP Tonalité & balance) ;
« Installer » les copie dans votre dossier et les active. *Installés* liste vos plug-ins, avec « Importer un plugin… »
(dossier ou .zip) et l'ouverture du dossier.

Thèmes : `docs/THEMES.md`. Plug-ins : `docs/PLUGINS.md`.

## Limites connues

- **Matériel** : développé et testé sous Linux sans carte son. Les sorties WASAPI exclusif et ASIO sont compilées
  et liées avec le compilateur croisé llvm-mingw, la partie macOS (Hog Mode, format physique) n'a pas pu être
  compilée ici : la CI macOS / Windows est la première vraie compilation de ces fichiers, et un essai avec un DAC
  (RME ADI-2, Merging) reste à faire.
- **DSD** : DST (DFF compressé) non pris en charge. Le DoP suppose un DAC qui reconnaît les marqueurs.
- **Formats** : AAC seulement sous macOS (décodeur système) ; pas de WavPack, APE, MQA.
- **Streaming** : retiré de l'interface pour l'instant. Qobuz et Tidal demandent un accès partenaire (la clé
  « Third party » de Tidal ne donne que des extraits de 30 s) ; Spotify ne délivre pas de flux audio aux applications
  tierces.
- **NAS** : seek dans un flux HTTP : attend que l'octet visé soit téléchargé. La découverte multicast n'est pas
  testable en CI (le test utilise le M-SEARCH unicast) ; essai réel avec un NAS à faire.
- **Shaders GLSL** : rendu OpenGL non testé sur ce banc (pas de GPU) ; le dégradé de repli l'est.
- **Interface** : rendue hors écran et lancée sous X virtuel ; une séance d'utilisation réelle reste à faire.
- **v1.0** : NX Remote et les mesures du tableau de bord passent le banc d'essai sur les trois systèmes (CI) ;
  l'anti-veille (macOS, Windows) n'est pas testée automatiquement et n'existe pas sous Linux. Les infos d'artistes demandent un accès à
  TheAudioDB / Wikipédia (testées sur un faux serveur). L'anglais couvre l'interface ; quelques messages d'erreur
  internes restent en français. Le pont AuraEngine X ouvre les fichiers dans l'application (pas d'options de
  conversion transmises en ligne de commande).
  Pas de bord de fenêtre personnalisé (barre de titre native).

## Licences

Le code de NX Player est sous licence MIT (`LICENSE`). Il s'appuie sur :
- **JUCE 8** : AGPLv3 ou licence commerciale. Distribuer un binaire de NX Player impose donc l'une des deux
  (code source complet sous AGPLv3, ou licence JUCE).
- **SDK ASIO** (optionnel, non fourni) : licence Steinberg ou GPLv3. Un binaire compilé avec ASIO suit l'une d'elles.
- **ALAC** (Apple, Apache 2.0), **SQLite** (domaine public), **QuickJS** (MIT, via JUCE), **QR Code generator**
  (Project Nayuki, MIT).

Détails : `THIRD_PARTY_NOTICES.md`.
