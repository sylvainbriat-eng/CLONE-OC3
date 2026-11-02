# CLONE OC3

Plugin VST3 / AU pour Mac : octave basse polyphonique inspirée du mode **POLY** de l'OC-3.
Seule la note **la plus grave** que vous jouez reçoit une octave en dessous, à condition d'être sous la fréquence réglée par **RANGE**. Le reste de l'accord reste intact. Aucune latence ajoutée.

## Les réglages

| Bouton | Rôle |
|---|---|
| **RANGE** | Limite haute des notes octaviées (50 à 400 Hz). 150 Hz ≈ cordes de Mi, La et Ré à vide ; montez pour les notes plus hautes, descendez pour n'octavier que les toutes premières frettes. |
| **DIRECT** | Volume du son d'origine. |
| **OCTAVE** | Volume de l'octave basse. |
| **COMPACT** | Compression de l'octave : 0 % = dynamique naturelle, 100 % = basse très régulière. |
| **BYPASS** | Coupe l'octave (le son direct continue). Le bouton OCTAVE est alors grisé. |

L'écran affiche la note grave suivie et un témoin rouge s'allume quand l'octave est active.

Repères pour RANGE : Mi grave 82 Hz, La 110 Hz, Ré 147 Hz, Sol 196 Hz, Si 247 Hz. En open D / open C / drop, la note la plus grave descend jusqu'à 65 Hz : c'est couvert.

## Installer sur Mac

Prérequis : Xcode et CMake (dans Applications).

```bash
bash scripts/build_mac.sh
```

Puis dans Ableton Live : Réglages > Plug-ins > activer VST3 > Rescanner.

## Pour les développeurs

Voir `CLAUDE.md` et les skills dans `.claude/skills/`.
