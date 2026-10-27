# Plug-ins NX Player

Deux familles, chacune dans son dossier :

```
Plugins/
├── UI/<nom>/    manifest.json + plugin.js        (JavaScript, dessin dans l'interface)
└── DSP/<nom>/   manifest.json + bibliothèque     (C, traitement du son)
```

Emplacements : `Plugins/` à côté de l'application (plug-ins livrés), puis le dossier utilisateur
(`~/Library/Application Support/NX Player/Plugins`, `%APPDATA%\NX Player\Plugins`, `~/.config/NX Player/Plugins`).
Activation : barre latérale › **Extensions & Thèmes** (interrupteur sur chaque carte) ou Réglages › Plug-ins.

`Catalog/` (à côté de l'application) contient des plug-ins en plus, non installés : la page **Plugins utilisateur ›
Catalogue** les affiche avec un aperçu et les copie dans le dossier utilisateur en un clic. Pour proposer le vôtre,
ajoutez son dossier dans `Catalog/UI` ou `Catalog/DSP` (un plug-in DSP du catalogue est compilé par CMake, voir
`Catalog/DSP/tone`). « Importer un plugin… » accepte un dossier ou une archive `.zip` contenant `manifest.json`.

---

## Plug-ins d'interface (JavaScript)

Le script tourne dans QuickJS (ES2023, intégré à JUCE 8), sur le fil de l'interface, avec un délai maximal de
60 ms par appel. Une erreur s'affiche dans l'emplacement du plug-in sans gêner le reste. Le script est rechargé à
chaud quand vous l'enregistrez.

### manifest.json

```json
{
  "id": "moi.oscillo",
  "name": "Oscilloscope",
  "version": "1.0",
  "author": "Moi",
  "description": "Forme d'onde en direct.",
  "main": "plugin.js",
  "slots": ["nowplaying.side", "home.top"],
  "fps": 60,
  "preferredHeight": 140,
  "enabledByDefault": false,
  "layout": { "lyrics": true }
}
```

| Emplacement | Où |
|---|---|
| `nowplaying.side` | page En cours, sous les paroles |
| `home.top` | page d'accueil, sous le titre |
| `playerbar.right` | barre de lecture, à droite du badge de qualité (petit) |
| `immersive.overlay` | vue immersive, en bas à gauche (visualiseurs, informations) |
| `sidebar.bottom` | bas de la barre latérale |

### Fonctions appelées

```js
function init(nx) { }                       // facultatif, au premier affichage
function render(g, w, h, nx, slot) { }      // à chaque image (fps du manifeste)
```

### `g` — dessin (coordonnées locales, origine en haut à gauche)

| Méthode | Effet |
|---|---|
| `g.color(c, alpha?)` | couleur de remplissage : `"#RRGGBB"`, `"rgba(...)"` ou `[r, g, b, a]` (0..1) |
| `g.gradient(x1, y1, c1, x2, y2, c2)` | dégradé linéaire |
| `g.alpha(a)` | opacité globale |
| `g.rect(x, y, w, h)`, `g.roundRect(x, y, w, h, r)`, `g.ellipse(x, y, w, h)` | remplissages |
| `g.strokeRect(x, y, w, h, épaisseur)`, `g.line(x1, y1, x2, y2, épaisseur)` | traits |
| `g.arc(cx, cy, r, a0, a1, épaisseur)` | arc (radians, 0 = haut, sens horaire) |
| `g.path([x0, y0, x1, y1, ...], fermé, "fill"\|"stroke", épaisseur)` | polyligne / polygone |
| `g.text(texte, x, y, w, h, "left"\|"center"\|"right", taille, gras)` | texte |
| `g.save()`, `g.restore()`, `g.translate(x, y)`, `g.rotate(a, cx, cy)` | état et transformations |

### `nx` — état de l'application (mis à jour à 30 Hz)

| Propriété | Contenu |
|---|---|
| `nx.time` | secondes (horloge haute résolution) |
| `nx.player` | `{ title, artist, album, duration, position, playing, format }` |
| `nx.audio` | `{ spectrum[64] (0..1, −90..0 dB, 20 Hz–20 kHz log), rmsL, rmsR, peakL, peakR, scope[256], rate, dsd }` |
| `nx.theme` | `{ accent, accent2, text, textDim, bg }` |
| `nx.ui.setLayout({...})` | `playerPosition: "top"\|"bottom"`, `sidebarWidth: px`, `lyrics: bool`, `immersive: { layout, lyrics, waveform, vu, spectrum, tech, artworkScale, motion, blur }` |
| `nx.console.log(...)` | journal de l'application |

En DSD (natif ou DoP), `nx.audio` décrit la densité du flux 1 bit (`dsd: true`) : les plug-ins voient
la modulation, le son lui-même n'est jamais touché.

Exemples livrés : `Plugins/UI/nx-spectrum` (spectre 64 bandes) et `Plugins/UI/nx-analog-vu` (VU-mètres à aiguille
avec inertie mécanique simulée).

---

## Plug-ins DSP (C, ABI stable)

En-tête : `Source/Ext/nxplayer_dsp.h`. La bibliothèque exporte :

```c
NX_DSP_EXPORT const nx_dsp_descriptor* nx_dsp_get_descriptor (void);
```

Le descripteur donne l'identifiant, les paramètres (`id`, nom, unité, min, max, défaut) et les fonctions
`create`, `destroy`, `process`, `set_param`, `reset`, `latency_frames`.

Règles :
- `process()` tourne sur le fil temps réel : pas d'allocation, pas de verrou, pas d'E/S. Tampons planaires
  `double`, traitement en place, pleine échelle ±1.
- `create`, `destroy` et `set_param` sont appelés hors du fil audio. Quand un paramètre change, NX Player
  prépare une nouvelle instance et l'échange sans verrou.
- Les plug-ins DSP ne s'appliquent qu'au PCM. Le DSD (natif ou DoP) les contourne.
- Un DSP actif sort le flux du mode bit-perfect : le Signal Path l'affiche (« Amélioré »).

### manifest.json

```json
{ "id": "nx.crossfeed", "name": "Crossfeed casque", "type": "dsp", "version": "1.0",
  "author": "NX Audio Suite", "description": "…", "library": "crossfeed" }
```

`library` est le nom sans extension : NX Player cherche `crossfeed.dll`, `crossfeed.dylib` ou `crossfeed.so`
(aussi préfixé `lib`, aussi dans `bin/`).

### Compiler l'exemple

Il est compilé avec l'application (cible CMake `nx_crossfeed`) et copié dans `Plugins/DSP/crossfeed`. À la main :

```bash
cc -O2 -shared -fPIC -I Source/Ext Plugins/DSP/crossfeed/crossfeed.c -o Plugins/DSP/crossfeed/crossfeed.so -lm    # Linux
cc -O2 -dynamiclib   -I Source/Ext Plugins/DSP/crossfeed/crossfeed.c -o Plugins/DSP/crossfeed/crossfeed.dylib     # macOS
cl /O2 /LD /I Source\Ext Plugins\DSP\crossfeed\crossfeed.c /Fe:Plugins\DSP\crossfeed\crossfeed.dll                 # Windows
```
