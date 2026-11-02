---
name: oc3-octave-poly
description: Algorithme d'octave basse "poly" du clone OC3 (note la plus grave seulement, fonction Range, zéro latence). À lire avant de modifier Source/dsp/PolyOctaveEngine.h ou de régler le son.
---

# Octave poly du clone OC3

Objectif : ajouter une octave basse **uniquement à la note la plus grave** jouée, si elle est sous la fréquence **Range**, avec un suivi fiable dans les accords (accordage standard, drop, open tunings) et **aucune latence ajoutée** (pas de lookahead, pas de FFT dans le chemin audio).

Pour les bases (dBFS, filtres, échantillonnage), réutiliser le skill `digichek-theorie-son`.

## Chaîne de traitement (fichier `Source/dsp/PolyOctaveEngine.h`)

1. **Passe-haut 30 Hz** (retire le continu et les bruits de manipulation).
2. **Filtre Range** : passe-bas Butterworth ordre 4 à `1,25 × Range`. Isole la zone grave.
3. **Banc de pisteurs** (`Tracker`) : un tous les 1/4 d'octave à partir de 40 Hz, jusqu'à `1,1 × Range`. Chaque pisteur =
   passe-haut ordre 2 à `0,5 × fc` + passe-bas Butterworth ordre 4 à `fc` + bascule sur les passages par zéro montants avec hystérésis (25 % de l'enveloppe).
   - Une note juste au-dessus de `fc` domine les notes plus aiguës (24 dB/octave) : ses passages par zéro sont propres même dans un accord à quintes.
   - Une mesure n'est **confirmée** que si deux périodes successives concordent à ±5 % **et** si la fréquence est dans la fenêtre du pisteur `[0,93 fc ; 1,5 fc]`. Cela rejette la « fondamentale virtuelle » d'une quinte (ex. Ré2 + La2 → 36,7 Hz).
   - Une mesure reste valable 2,5 périodes.
4. **Note la plus grave** = plus petite fréquence confirmée parmi les pisteurs dont l'énergie dépasse 8 % de l'enveloppe globale (une note qui s'éteint ne compte plus).
   Le pisteur utilisé pour le son est celui dont la coupure est juste sous cette note. Changement de pisteur = fondu de 4 ms.
5. **Fonction Range** : octave active si note la plus grave ≤ `Range × 1,03` (½ ton de tolérance). Pas de note confirmée pendant 40 ms alors qu'on joue → octave coupée. Après un silence, l'octave démarre ouverte tout de suite (attaque immédiate).
6. **Octave** = `sortie du pisteur normalisée × bascule × niveau de jeu` → fréquence / 2, forme d'onde arrondie façon OC-3, puis passe-bas à `0,75 × Range`.
7. **Anti-percussion (Sensibilité)** : tant qu'aucune note n'est confirmée depuis l'attaque (silence → jeu), l'octave est multipliée par `(1 − s)³` ; dès la confirmation elle monte à plein niveau en 3 ms. Coût : la confirmation prend ~40 ms sur Mi2 (3 passages par zéro + stabilisation du filtre).
8. **Mode AUTO** : tous les pisteurs (jusqu'à ~1080 Hz, entrée filtrée à 1250 Hz) écoutent. Mémoire `memoryHz` de la note la plus grave : descend immédiatement, remonte vers la note jouée avec la constante du bouton TIME (`timeSeconds`, 50 ms à 10 s, défaut 2,5 s ; très court = toujours octavier la note la plus grave). Plafond = `memoryHz × 2,12` (octave + ½ ton). Rien n'est mémorisé dans le silence (sinon le souffle fausse la mémoire). La présence d'une note se mesure contre l'énergie sous 300 Hz (`presEnv`), sinon les cordes aiguës masquent la basse d'un accord.
9. **Compact** : compresseur sur l'octave, ratio 1:1 à 8:1, référence −12 dBFS, gain max +15,6 dB.

## Ce qui n'a PAS marché (ne pas refaire)

- **Un seul passe-bas fixe** + bascule : échoue sur les accords (Mi2 + Si2 → 20 Hz).
- **Banc de filtres passe-bande Q=2** pour trouver la bande la plus basse : bandes trop larges, tout est plat.
- **Un seul filtre adaptatif** qui suit la note : boucle instable (le filtre descend sur ses propres transitoires) ou bloquée sur les accords à quintes.

## Réglages sensibles

| Constante | Valeur | Effet |
|---|---|---|
| `Tracker::hysteresis` | 0,25 | plus haut = moins de faux passages, mais rate les notes faibles |
| tolérance de concordance | 5 % | plus bas = suivi plus strict, confirmation plus lente |
| fenêtre `[0,93 ; 1,5] × fc` | — | doit couvrir l'écart entre pisteurs (×1,19) |
| `presenceRatio` | 0,08 | vitesse à laquelle une note qui s'éteint est oubliée |
| `unconfirmedMax` | 40 ms | délai avant de couper l'octave d'une note trop aiguë |

Après toute modification : lancer le skill `oc3-tests`.
