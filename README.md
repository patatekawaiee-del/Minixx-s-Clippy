# Minixx's Clippy (Geode, iOS)

Mod de replay instantané pour Geometry Dash iOS. Cible : Geode 5.11.x, GD 2.2081.

## Compiler (sans Mac, avec GitHub Actions)

1. Crée un dépôt GitHub (privé ou public) et envoie **tout le contenu de ce dossier** (en gardant les sous-dossiers `src/` et `.github/workflows/`).
2. Onglet **Actions** -> workflow *Build Minixx's Clippy (iOS)* -> **Run workflow** (il se lance aussi à chaque push).
3. Quand c'est vert, ouvre l'exécution et télécharge l'artefact **minixx.clippy**. Dézippe-le : tu obtiens `minixx.clippy.geode`.

Si le build se plaint de la version de Geode : mets dans `mod.json` la version qu'il affiche (champ `"geode"`), ou fixe le SDK dans `.github/workflows/build.yml` (`sdk: v5.11.2`).

## Installer

Copie `minixx.clippy.geode` dans le dossier des mods de Geode, comme pour n'importe quel autre mod (voir la doc de l'iOS Launcher : https://docs.geode-sdk.org/misc/ios/). Relance le jeu.

## Premier test (2 minutes)

1. Au menu principal, iOS demande l'autorisation d'enregistrer l'écran : accepte.
2. Ouvre le Studio (bouton trombone en bas du menu). La pastille doit afficher `REC x / 30 s` en vert.
3. Appuie sur **Clipper les 20 dernières s** : un clip apparaît dans la galerie. **Partager** -> *Enregistrer la vidéo*.
4. Lance un niveau, meurs après 80 % : vibration, puis clip dans le Studio.

## Si quelque chose cloche

- **Clip couché ou à l'envers** : Studio -> réglages -> *Tampon* -> *Correction rotation* (Sens horaire / anti-horaire / À l'envers). C'est la partie que je n'ai pas pu tester sur un vrai iPhone.
- **Pas de son / pas de vidéo / erreur** : Studio -> réglages -> *Clippy* -> *Diagnostic*, et envoie-moi le texte. Les logs de Geode contiennent aussi des lignes `Encoder ready`, `Clip saved`, etc.
- **Le jeu manque de mémoire** : baisse la durée du tampon ou la qualité.
- **Erreur de compilation** : copie-moi le message. Tout ce qui lit des champs du jeu est dans `src/GDAccess.hpp`.

## Structure

- `src/Native.mm` : ReplayKit + VideoToolbox + AVFoundation (le recorder).
- `src/Engine.*` : déclencheurs, clips, bibliothèque sur disque.
- `src/UI.cpp`, `src/Widgets.hpp` : Studio, Clippy, notifications.
- `src/main.cpp` : les hooks du jeu.
- `src/Strings.cpp` : textes FR / EN.
