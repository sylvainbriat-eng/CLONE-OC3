---
name: oc3-tests
description: Banc d'essai du moteur d'octave du clone OC3 (tests/dsp_test.cpp) - comment le lancer, lire les résultats et ajouter des cas. À utiliser après chaque modification du DSP.
---

# Tester le moteur d'octave

Le test n'a pas besoin de JUCE ni d'un Mac :

```bash
g++ -O2 -std=c++17 -I Source tests/dsp_test.cpp -o /tmp/dsp_test && /tmp/dsp_test
```

(ou `cmake --build build --target dsp_test` puis `./build/dsp_test`)

## Ce que fait le test

- Synthèse de cordes pincées (Karplus-Strong, excitation centrée pour éviter un faux continu) à 48 kHz ; les accords sont grattés (12 ms entre cordes). `dur` permet d'étouffer une note (enchaînements).
- Pour chaque scénario : fréquence de l'octave (autocorrélation entre 0,3 et 0,8 s après la dernière note), niveau de l'octave par rapport à l'entrée, temps d'attaque, et **suivi** = % du temps où la note mesurée est à ±3 % de la note la plus grave (après 150 ms, et entre 30 et 150 ms).
- Critère OK : octave attendue → fréquence à ±4 % de la note / 2 et niveau > −12 dB ; octave non attendue → niveau < −30 dB.
- Dernière ligne : charge CPU (doit rester bien sous 2 % d'un cœur).

## Résultats de référence (2 nov. 2026)

18 scénarios sur 18 OK : notes seules (Mi2, La2, Si1 baryton), accords Mi, La, power chord, open D, open C, drop C, bruit −50 dB, basse tenue + mélodie aiguë, notes au-dessus du Range (Sol3, Si3, Mi4) sans octave, enchaînements. Suivi à 100 % après 150 ms partout. CPU ≈ 1 % d'un cœur.

Point faible connu : passage d'une note grave à une plus aiguë (Mi2 → La2) : ~100 ms de transition tant que le Mi2 résonne encore.

## Ajouter un cas

Dans `main()` : `run ("nom", notes, rangeHz, noteLaPlusGraveHz, octaveAttendue, bruitDb);`
