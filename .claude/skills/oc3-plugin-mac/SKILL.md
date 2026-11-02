---
name: oc3-plugin-mac
description: Compiler, installer et dépanner le plugin VST3/AU CLONE OC3 sur le Mac de Sylvain (Xcode, CMake sans Homebrew, JUCE, Ableton Live).
---

# Plugin CLONE OC3 sur Mac

Pour le fonctionnement audio de macOS (buffers, latence, Core Audio), réutiliser le skill `digichek-audio-macos`.

## Environnement de Sylvain

- Xcode 27.2 (licence acceptée), CMake 4.4.5 dans `/Applications/CMake.app` (pas de Homebrew).
- Dossier de travail : `/Users/sylvainbriat/Documents/CLAUDE` ; dépôt GitHub `sylvainbriat-eng/CLONE-OC3`.
- Hôte : **Ableton Live**, format **VST3** (AU compilé en bonus).
- Sylvain ne code pas : tout doit marcher avec une seule commande.

## Compiler

```bash
cd /Users/sylvainbriat/Documents/CLAUDE/CLONE-OC3   # ou là où le dépôt est cloné
bash scripts/build_mac.sh
```

Le script : configure CMake (télécharge JUCE 8.0.16-security la 1re fois), lance le banc d'essai, compile VST3 + AU, les copie dans `~/Library/Audio/Plug-Ins/VST3` et `.../Components`, signe en ad hoc et retire la quarantaine.

## Structure

- `CMakeLists.txt` : `juce_add_plugin` (code fabricant `Sylv`, code plugin `Oc3p`, `COPY_PLUGIN_AFTER_BUILD`).
- `Source/PluginProcessor.*` : paramètres Range (50–400 Hz), Direct, Octave (dB), Compact (%). Entrée mono ou stéréo, somme mono pour le suivi, octave ajoutée sur chaque canal. `setLatencySamples(0)`.
- `Source/PluginEditor.*` : 4 boutons, affichage de la note grave suivie, témoin rouge quand l'octave est active.

## Dépannage

| Problème | Solution |
|---|---|
| `cmake: command not found` | le script utilise `/Applications/CMake.app/Contents/bin/cmake` |
| `xcrun: error` / SDK introuvable | `sudo xcode-select -s /Applications/Xcode.app` |
| Ableton ne voit pas le plugin | Réglages > Plug-ins > activer VST3 « Dossier système », puis Rescanner |
| Plugin refusé (« endommagé ») | `xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/CLONE\ OC3.vst3` |
| Erreur de compilation JUCE avec un SDK très récent | essayer un tag JUCE plus récent dans `CMakeLists.txt` (`GIT_TAG`), supprimer `build/` et relancer |
