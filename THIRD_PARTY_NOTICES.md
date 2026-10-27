# Composants tiers

| Composant | Version | Licence | Utilisation | Inclus dans le dépôt |
|---|---|---|---|---|
| JUCE | 8.0.15 | AGPLv3 ou licence commerciale JUCE | fenêtres, graphisme, périphériques audio, FLAC / MP3 / Vorbis, QuickJS, réseau | non (téléchargé par CMake) |
| QuickJS (via JUCE / choc) | — | MIT | plug-ins d'interface JavaScript | non (dans JUCE) |
| Apple Lossless Audio Codec | macosforge/alac | Apache 2.0 | décodage ALAC (encodeur utilisé par le banc d'essai) | oui, `ThirdParty/alac` (détection de l'ordre des octets corrigée pour arm64 / MSVC) |
| SQLite | système, ou amalgamation 3.38 | domaine public | bibliothèque | non (système, ou téléchargé sous Windows) |
| QR Code generator (Project Nayuki) | C++ | MIT | QR code de NX Remote | oui, `ThirdParty/qrcodegen` (en-tête de licence conservé) |
| SDK ASIO de Steinberg | 2.3.x | licence Steinberg ASIO ou GPLv3 | sortie ASIO (PCM + DSD natif), Windows | non : à fournir via `NX_ASIO_SDK_DIR` |

## Conséquences pour une distribution

- **JUCE** : un binaire de NX Player contient JUCE. Le distribuer suppose soit de respecter l'AGPLv3 (code source
  complet de l'application disponible sous AGPLv3), soit une licence commerciale JUCE. Le code de NX Player
  lui-même reste sous licence MIT.
- **ASIO** : un binaire compilé avec le SDK ASIO suit la licence Steinberg (accord à signer) ou la GPLv3.
  ASIO est une marque de Steinberg Media Technologies GmbH.
- **Services** : Qobuz, Tidal, Spotify et YouTube sont des marques de leurs propriétaires. NX Player utilise leurs
  API avec les identifiants que l'utilisateur obtient auprès d'eux et respecte leurs conditions (lecture déléguée
  pour Spotify et YouTube).

- **Infos d'artistes** : les photos et biographies viennent de TheAudioDB (clé publique gratuite, usage non
  commercial) et de Wikipédia (textes sous licence CC BY-SA : la source est affichée sous la biographie). Elles sont
  gardées dans la base locale de l'utilisateur, jamais redistribuées avec l'application ; l'option se coupe dans
  Réglages › Bibliothèque & NAS.

Le texte de la licence Apache 2.0 d'ALAC est dans `ThirdParty/alac/LICENSE`.
