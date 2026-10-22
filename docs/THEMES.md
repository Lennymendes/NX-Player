# Thèmes NX Player (`.nxtheme`)

Un thème est un **dossier** dont le nom finit par `.nxtheme` (ou une archive zip renommée `.nxtheme`) :

```
mon-theme.nxtheme/
├── manifest.json      description et arrière-plan
├── theme.css          variables CSS (couleurs, formes, dimensions)
└── background.frag    facultatif : shader GLSL d'arrière-plan
```

NX Player cherche les thèmes dans `Themes/` à côté de l'application (thèmes livrés), puis dans le dossier
utilisateur, qui l'emporte à identifiant égal :

| Système | Dossier utilisateur |
|---|---|
| macOS | `~/Library/Application Support/NX Player/Themes` |
| Windows | `%APPDATA%\NX Player\Themes` |
| Linux | `~/.config/NX Player/Themes` |

**Rechargement à chaud** : le thème actif est surveillé (toutes les 0,5 s). Enregistrez `theme.css` ou
`manifest.json` et l'interface se met à jour sans redémarrer.
**Partager** : Réglages › Thèmes › « Exporter le thème actif… » produit une archive ; « Importer un thème… »
accepte un dossier ou une archive.

## manifest.json

```json
{
  "id": "mon-theme",
  "name": "Mon thème",
  "author": "Moi",
  "version": "1.0",
  "description": "Une phrase affichée dans les réglages.",
  "stylesheet": "theme.css",
  "background": { "type": "artwork", "fallback": "mesh", "speed": 0.15 }
}
```

`background.type` :

| Valeur | Effet |
|---|---|
| `artwork` | pochette en cours, floutée, saturée et assombrie, fondu enchaîné au changement de morceau (style Cider) |
| `mesh` / `gradient` | dégradé maillé animé (accent, accent 2, couleur dominante de la pochette), réagit au niveau sonore |
| `shader` | `background.frag` exécuté par OpenGL ; `fallback` est utilisé sans OpenGL |

`speed` règle l'animation du dégradé.

## Variables (`theme.css`)

Seules les propriétés personnalisées sont lues : `--nom: valeur;` dans n'importe quel bloc. `var(--autre)` et
`var(--autre, repli)` sont résolus. Couleurs : `#RGB`, `#RGBA`, `#RRGGBB`, `#RRGGBBAA`, `rgb()`, `rgba()`, noms CSS.

| Variable | Rôle | NX Emerald |
|---|---|---|
| `--nx-bg` | fond de la fenêtre | `#0A0C0E` |
| `--nx-bg-2` | fond secondaire | `#0E1418` |
| `--nx-surface` | menus, infobulles | `#12171B` |
| `--nx-glass-tint` | teinte du verre dépoli (panneaux) | `rgba(10,14,17,.56)` |
| `--nx-glass-strong` | verre plus opaque (barre de lecture, file d'attente) | `rgba(8,11,14,.80)` |
| `--nx-border` | filet des panneaux | `rgba(255,255,255,.08)` |
| `--nx-highlight` | reflet en haut des panneaux | `rgba(255,255,255,.06)` |
| `--nx-text` / `--nx-text-dim` | texte principal / secondaire | `#EEF3F1` / `#8A9792` |
| `--nx-accent` / `--nx-accent-2` | accents (dégradés, sélection) | `#00F5A0` / `#00D9F5` |
| `--nx-on-accent` | texte sur fond accent | `#04130D` |
| `--nx-quality-bitperfect` | Signal Path : bit-perfect | `#B07CFF` |
| `--nx-quality-enhanced` | Signal Path : amélioré | accent |
| `--nx-quality-high` | Signal Path : haute qualité | `#7FE3C0` |
| `--nx-quality-low` | Signal Path : qualité réduite | `#FFB547` |
| `--nx-radius` / `--nx-radius-small` | arrondis | `14px` / `8px` |
| `--nx-sidebar-width` | largeur de la barre latérale | `236px` |
| `--nx-player-height` | hauteur de la barre de lecture | `84px` |
| `--nx-player-position` | `bottom` ou `top` | `bottom` |
| `--nx-card-size` | taille visée des pochettes de la grille | `176px` |
| `--nx-artwork-blur` | flou de la pochette d'arrière-plan | `48` |
| `--nx-artwork-dim` | assombrissement (0 à 1) | `0.58` |
| `--nx-artwork-saturation` | saturation de la pochette | `1.25` |
| `--nx-lyrics-active` / `--nx-lyrics-inactive` | ligne chantée / autres lignes | texte / 30 % |
| `--nx-lyrics-size` | taille des paroles | `30px` |
| `--nx-animation-speed` | vitesse des animations (1 = normale) | `1.0` |

## Shader d'arrière-plan (builds OpenGL : macOS, Windows, Linux avec OpenGL)

Fragment shader GLSL inséré par JUCE (`OpenGLGraphicsContextCustomShader`) : `pixelPos` (coordonnées en pixels)
et `pixelAlpha` sont fournis ; écrivez `gl_FragColor`. Uniforms passés par NX Player :

| Uniform | Type | Valeur |
|---|---|---|
| `u_time` | `float` | secondes depuis le lancement |
| `u_resolution` | `vec2` | taille de la fenêtre en pixels |
| `u_accent`, `u_accent2` | `vec4` | couleurs d'accent du thème |
| `u_level` | `float` | niveau audio lissé (0 à 1) |

Exemple complet : `Themes/aura-neon.nxtheme/background.frag`. Une erreur de compilation est écrite dans le
journal et l'arrière-plan de repli est utilisé.
