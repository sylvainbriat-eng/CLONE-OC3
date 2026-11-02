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
| **SENSIBILITÉ** | Anti-percussion. 0 % = attaque la plus franche, mais une frappe sur la caisse ou des cordes étouffées peuvent produire de l'octave. Plus haut = l'octave démarre plus doucement et ne monte à plein niveau qu'une fois une vraie note reconnue (environ 40 ms sur le Mi grave). Réglage d'origine : 30 %. |
| **AUTO** | Le plafond n'est plus fixé par RANGE (grisé) : il se cale une octave au-dessus de la note la plus grave jouée récemment, et remonte si vous jouez plus haut. Fonctionne jusqu'à ~1000 Hz. |
| **TIME** | (mode AUTO) Vitesse à laquelle le plafond remonte : de 50 ms (il suit tout de suite : la note la plus grave est toujours octaviée, même un solo) à 10 s (la basse garde la main longtemps, la mélodie au-dessus n'est pas octaviée). Réglage d'origine : 2,5 s. Grisé hors mode AUTO. |
| **BYPASS** | Coupe l'octave (le son direct continue). Le bouton OCTAVE est alors grisé. |

L'écran affiche la note grave suivie et un témoin rouge s'allume quand l'octave est active.

**La barre de notes** (en haut) montre en direct ce qui se passe, du Mi grave aux notes aiguës :
- la zone colorée = les notes qui seront octaviées (rouge en manuel jusqu'au RANGE, bleue en AUTO) ;
- le trait blanc = le plafond ; en AUTO, il bouge selon votre jeu, à la vitesse réglée par TIME ;
- le triangle (AUTO) = la note la plus grave mémorisée, point de départ du plafond ;
- le rond = la note jouée, vert quand elle est octaviée.

Repères pour RANGE : Mi grave 82 Hz, La 110 Hz, Ré 147 Hz, Sol 196 Hz, Si 247 Hz. En open D / open C / drop, la note la plus grave descend jusqu'à 65 Hz : c'est couvert.

## Installer sur Mac

Prérequis : Xcode et CMake (dans Applications).

```bash
bash scripts/build_mac.sh
```

Puis dans Ableton Live : Réglages > Plug-ins > activer VST3 > Rescanner.

## Pour les développeurs

Voir `CLAUDE.md` et les skills dans `.claude/skills/`.
