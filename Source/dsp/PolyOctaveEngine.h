// PolyOctaveEngine.h
// Moteur d'octave basse "poly" inspiré du mode POLY de l'OC-3.
// Seule la note la plus grave (sous la fréquence "Range") reçoit une octave basse.
// Aucune anticipation (lookahead) : latence ajoutée = 0 échantillon.
// Header-only, sans dépendance JUCE, pour pouvoir le tester hors plugin.
#pragma once

#include <algorithm>
#include <cmath>

namespace oc3
{

// Biquad en forme directe transposée II (RBJ cookbook).
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    void setLowpass (double fs, double f, double q)  { design (fs, f, q, true); }
    void setHighpass (double fs, double f, double q) { design (fs, f, q, false); }
    void setBandpass (double fs, double f, double q)
    {
        f = std::clamp (f, 10.0, 0.45 * fs);
        const double w0 = 2.0 * M_PI * f / fs;
        const double alpha = std::sin (w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        b0 = alpha / a0; b1 = 0; b2 = -alpha / a0;
        a1 = -2.0 * std::cos (w0) / a0;
        a2 = (1.0 - alpha) / a0;
    }

    void reset() { z1 = z2 = 0; }

    inline double process (double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

private:
    void design (double fs, double f, double q, bool lowpass)
    {
        f = std::clamp (f, 10.0, 0.45 * fs);
        const double w0 = 2.0 * M_PI * f / fs;
        const double cw = std::cos (w0), sw = std::sin (w0);
        const double alpha = sw / (2.0 * q);
        const double a0 = 1.0 + alpha;
        if (lowpass)
        {
            b0 = (1.0 - cw) * 0.5 / a0;
            b1 = (1.0 - cw) / a0;
            b2 = b0;
        }
        else
        {
            b0 = (1.0 + cw) * 0.5 / a0;
            b1 = -(1.0 + cw) / a0;
            b2 = b0;
        }
        a1 = -2.0 * cw / a0;
        a2 = (1.0 - alpha) / a0;
    }
};

// Un "pisteur" : passe-bas très raide (48 dB/oct) placé juste sous une zone de notes,
// suivi d'une bascule sur les passages par zéro. Une note située juste au-dessus
// de la coupure domine toutes les notes plus aiguës : ses passages par zéro sont
// propres, même au milieu d'un accord.
struct Tracker
{
    Biquad hp, lp[4];
    double fc = 100, hpHz = 25;
    double env = 0, prev = 0, lastFrac = 0, lastPeriod = 0, lastPeriod2 = 0;
    double hz = 0;          // dernière fréquence confirmée
    long sinceCross = 0, sinceValid = 1 << 30;
    bool armed = false;
    double flip = 1.0;
    double out = 0;         // signal filtré courant

    void setup (double fs, double cutoff)
    {
        fc = cutoff;
        hpHz = std::max (25.0, 0.5 * fc);
        hp.setHighpass (fs, hpHz, 0.707);
        // Butterworth ordre 8 : une note juste au-dessus de la coupure écrase
        // les notes plus aiguës, même 4 à 5 fois plus fortes (accords tenus).
        static const double q[4] = { 0.5412, 1.3066, 0.5412, 1.3066 };
        for (int i = 0; i < 4; ++i) lp[i].setLowpass (fs, fc, q[i]);
    }

    // gain théorique du pisteur à une fréquence (pour estimer le niveau réel d'une note)
    double gainAt (double f) const
    {
        const double r = f / fc, h = f / hpHz;
        return (h * h / std::sqrt (1.0 + h * h * h * h)) / std::sqrt (1.0 + std::pow (r, 16.0));
    }

    // niveau estimé de la note confirmée, avant filtrage
    double noteLevel() const { return hz > 0 ? env / std::max (gainAt (hz), 1.0e-3) : 0.0; }

    void reset()
    {
        hp.reset(); for (auto& b : lp) b.reset();
        env = prev = lastFrac = lastPeriod = lastPeriod2 = hz = out = 0;
        sinceCross = 0; sinceValid = 1 << 30;
        armed = false; flip = 1.0;
    }

    bool valid() const { return hz > 0 && sinceValid < validHoldPeriods * (fsCache / hz); }

    inline void process (double x, double envAtk, double envRel, double fs)
    {
        fsCache = fs;
        out = lp[3].process (lp[2].process (lp[1].process (lp[0].process (hp.process (x)))));
        const double a = std::abs (out);
        env += (a > env ? envAtk : envRel) * (a - env);

        ++sinceCross; ++sinceValid;
        if (out < -hysteresis * env) armed = true;

        if (armed && prev <= 0.0 && out > 0.0 && sinceCross >= 24)
        {
            const double frac = prev / (prev - out);
            const double period = (double) sinceCross + frac - lastFrac;
            lastFrac = frac;
            sinceCross = 0;
            armed = false;
            flip = -flip;

            // On ne retient la mesure que si les périodes concordent et si la note
            // est bien dans la zone de ce pisteur (juste au-dessus de sa coupure) :
            // sinon c'est un mélange de notes ou un transitoire.
            //  - cas simple : deux périodes successives égales (±5 %) ;
            //  - accord tenu : une quinte résiduelle décale les passages par zéro
            //    une fois sur deux (périodes courte/longue alternées) : on accepte
            //    si la période est égale à celle d'avant-avant (±3 %).
            const bool steady = lastPeriod > 0 && std::abs (period / lastPeriod - 1.0) < 0.05;
            const bool alternating = lastPeriod2 > 0 && std::abs (period / lastPeriod2 - 1.0) < 0.03
                                     && std::abs (period / lastPeriod - 1.0) < 0.15;
            const double avg = 0.5 * (period + lastPeriod);
            const double f = fs / avg;
            if ((steady || alternating) && f > windowLo * fc && f < windowHi * fc)
            {
                hz = f;
                sinceValid = 0;
            }
            lastPeriod2 = lastPeriod;
            lastPeriod = period;
        }
        prev = out;
    }

    static constexpr double hysteresis = 0.25;
    static constexpr double validHoldPeriods = 2.5;
    static constexpr double windowLo = 0.97, windowHi = 1.25; // la note doit être juste au-dessus de la coupure
    double fsCache = 48000;
};

class PolyOctaveEngine
{
public:
    struct Params
    {
        double rangeHz    = 150.0; // notes dont la fondamentale est sous cette fréquence -> octave
        double octaveGain = 1.0;   // niveau linéaire de l'octave
        double compact    = 0.5;   // 0 = dynamique naturelle, 1 = bas très compressé
        double sensitivity = 0.5;  // anti-percussion : 0 = attaque franche, 1 = rejet strict des bruits
        bool   autoRange  = false; // AUTO : le plafond suit le jeu au lieu du bouton Range
        double timeSeconds = 2.5;  // AUTO : temps pour que le plafond remonte quand on joue plus haut
    };

    // Pisteurs espacés d'1/6 d'octave, de 40 Hz à ~1080 Hz (mode AUTO ; en manuel
    // seuls ceux sous le Range tournent).
    static constexpr int trackersPerOctave = 6;
    static constexpr int maxTrackers = 29;
    static constexpr double autoInputHz = 1250.0;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        dcBlock.setHighpass (fs, 30.0, 0.707);
        envAtk     = coef (0.0005);
        envRel     = coef (0.030);
        levelAtk   = coef (0.002);
        levelRel   = coef (0.080);
        compAtk    = coef (0.004);
        compRel    = coef (0.120);
        gateSmooth = coef (0.004);
        xfadeStep  = 1.0 / (0.004 * fs);
        unconfirmedMax = (long) (0.040 * fs);
        confirmAtk = coef (0.003);
        for (int k = 0; k < maxTrackers; ++k) trackers[k].setup (fs, 40.0 * std::pow (2.0, k / (double) trackersPerOctave));
        paramsDirty = true;
        reset();
    }

    void reset()
    {
        dcBlock.reset(); lp1.reset(); lp2.reset(); post1.reset(); presLp.reset();
        presEnv = 0;
        for (auto& t : trackers) t.reset();
        env = level = compEnv = 0;
        measuredHz = 0;
        unconfirmed = 0;
        current = previous = -1;
        xfade = 1.0;
        gate = 0;
        gateTarget = 1.0;
        confirmGain = 0;
        confirmedPhrase = false;
        memoryHz = 0;
    }

    void setParams (const Params& p)
    {
        if (std::abs (p.rangeHz - params.rangeHz) > 1.0e-6 || p.autoRange != params.autoRange) paramsDirty = true;
        if (std::abs (p.timeSeconds - params.timeSeconds) > 1.0e-6 || memoryRise <= 0.0)
            memoryRise = coef (std::max (0.01, p.timeSeconds));
        params = p;
    }

    double getMeasuredHz() const { return measuredHz; }
    double getGate() const       { return gate * confirmGain; }
    // Plafond actuel : bouton Range en manuel, plafond qui suit le jeu en AUTO.
    double getCeilingHz() const  { return params.autoRange ? autoCeiling() : params.rangeHz; }
    double getMemoryHz() const   { return memoryHz; }

    // Traite un échantillon mono, renvoie uniquement le signal d'octave.
    inline double processSample (double in)
    {
        if (paramsDirty) updateFilters();

        // 1. Isolement des graves (Range) et niveau de jeu
        const double x  = dcBlock.process (in);
        const double lp = lp2.process (lp1.process (x));
        const double a = std::abs (lp);
        env   += (a > env   ? envAtk   : envRel)   * (a - env);
        level += (a > level ? levelAtk : levelRel) * (a - level);
        // référence de présence : énergie de la zone grave seulement (sinon, en AUTO,
        // les cordes aiguës masqueraient la basse d'un accord)
        const double pa = std::abs (presLp.process (x));
        presEnv += (pa > presEnv ? envAtk : envRel) * (pa - presEnv);

        // 2. Tous les pisteurs tournent en parallèle (aucune latence ajoutée).
        for (int k = 0; k < numTrackers; ++k) trackers[k].process (lp, envAtk, envRel, fs);

        // 3. La note la plus grave = la plus basse fréquence confirmée par un pisteur.
        double lowest = 0;
        // (rien n'est retenu dans le silence : le souffle ne doit pas fausser la mémoire)
        for (int k = 0; k < numTrackers; ++k)
            if (env >= silenceThreshold && trackers[k].valid() && trackers[k].noteLevel() > presenceRatio * presEnv
                && (lowest <= 0.0 || trackers[k].hz < lowest * 0.97))
                lowest = trackers[k].hz;

        if (lowest > 0)
        {
            unconfirmed = 0;
            confirmedPhrase = true;
            measuredHz = lowest;
            if (params.autoRange)
            {
                // Mémoire de la note la plus grave : descend tout de suite,
                // remonte en suivant le bouton TIME quand on joue plus haut
                // (TIME très court = on octavie toujours la note la plus grave).
                if (memoryHz <= 0.0 || lowest < memoryHz) memoryHz = lowest;
                else memoryHz *= std::pow (lowest / memoryHz, memoryRise);
            }
            // le pisteur le mieux placé : coupure juste sous la note
            int best = 0;
            for (int k = 0; k < numTrackers; ++k)
                if (trackers[k].fc <= lowest * 1.02) best = k;
            select (best);
            // 4. Fonction Range : octave seulement si la note la plus grave est
            //    sous la limite (tolérance d'un demi-ton).
            gateTarget = lowest <= getCeilingHz() * 1.03 ? 1.0 : 0.0;
        }
        else if (current < 0 || env < silenceThreshold)
        {
            // Attaque depuis le silence : on part tout de suite avec le pisteur
            // du haut de la plage (réponse immédiate), avant confirmation.
            select (numTrackers - 1);
            if (env < silenceThreshold) { gateTarget = 1.0; measuredHz = 0; unconfirmed = 0; confirmedPhrase = false; }
        }
        // On joue, mais aucune note grave n'est confirmée depuis 40 ms :
        // la note est au-dessus du Range, on coupe l'octave.
        if (lowest <= 0 && env >= silenceThreshold && ++unconfirmed > unconfirmedMax)
        {
            gateTarget = 0.0;
            confirmedPhrase = false;
        }

        gate += gateSmooth * (gateTarget - gate);

        // Anti-percussion : tant qu'aucune vraie note n'est confirmée (2 vibrations
        // régulières), l'octave reste à un niveau réduit ; elle passe à plein niveau
        // dès la confirmation. Une frappe sur la caisse ou des cordes étouffées ne
        // sont jamais confirmées : elles ne laissent qu'un souffle.
        const double sens = std::clamp (params.sensitivity, 0.0, 1.0);
        const double floorGain = (1.0 - sens) * (1.0 - sens) * (1.0 - sens);
        const double confirmTarget = confirmedPhrase ? 1.0 : floorGain;
        confirmGain += (confirmTarget > confirmGain ? confirmAtk : gateSmooth) * (confirmTarget - confirmGain);

        // En AUTO, le filtre de l'octave suit la note jouée.
        if (params.autoRange && measuredHz > 0 && (++postCounter & 15) == 0)
            post1.setLowpass (fs, std::max (60.0, 1.3 * measuredHz), 0.707);

        // 5. Octave : sinusoïde de la note grave (normalisée) x bascule -> fréquence / 2,
        //    puis on lui redonne la dynamique du jeu. Fondu court lors d'un changement de pisteur.
        double raw = voice (current);
        if (xfade < 1.0)
        {
            xfade = std::min (1.0, xfade + xfadeStep);
            raw = xfade * raw + (1.0 - xfade) * voice (previous);
        }
        double sub = post1.process (raw * level);

        // 6. Compacité : compression de l'octave
        const double s = std::abs (sub);
        compEnv += (s > compEnv ? compAtk : compRel) * (s - compEnv);
        const double ratio = 1.0 + 7.0 * params.compact;
        double g = std::pow (std::max (compEnv, 1.0e-6) / compRef, 1.0 / ratio - 1.0);
        g = std::min (g, maxCompGain);
        const double floorFade = std::clamp ((env - silenceThreshold) / (4.0 * silenceThreshold), 0.0, 1.0);

        return sub * g * gate * confirmGain * floorFade * params.octaveGain;
    }

private:
    double coef (double seconds) const { return 1.0 - std::exp (-1.0 / (seconds * fs)); }

    inline double voice (int k) const
    {
        if (k < 0) return 0.0;
        const auto& t = trackers[k];
        return t.flip * t.out / std::max (t.env, 1.0e-9);
    }

    void select (int k)
    {
        if (k == current) return;
        previous = current;
        current = k;
        xfade = previous < 0 ? 1.0 : 0.0;
    }

    double autoCeiling() const
    {
        // une octave + un demi-ton au-dessus de la note la plus grave récente
        return memoryHz > 0.0 ? memoryHz * 2.1189 : autoInputHz;
    }

    void updateFilters()
    {
        // En AUTO, tous les pisteurs écoutent (jusqu'à ~1 kHz) : le plafond est
        // décidé par la mémoire de jeu, pas par un filtre fixe.
        const double top = params.autoRange ? autoInputHz / 1.25 : params.rangeHz;
        const double fc = top * 1.25;
        lp1.setLowpass (fs, fc, 0.5412); // Butterworth ordre 4
        lp2.setLowpass (fs, fc, 1.3066);
        presLp.setLowpass (fs, params.autoRange ? 300.0 : fc, 0.707);
        post1.setLowpass (fs, std::max ((params.autoRange ? 150.0 : params.rangeHz) * 0.75, 60.0), 0.707);
        numTrackers = 1;
        while (numTrackers < maxTrackers && trackers[numTrackers].fc < top * 1.1) ++numTrackers;
        if (current >= numTrackers) current = -1;
        if (previous >= numTrackers) previous = -1;
        paramsDirty = false;
    }

    double fs = 48000.0;
    Params params;
    bool paramsDirty = true;

    Biquad dcBlock, lp1, lp2, post1, presLp;
    double presEnv = 0;
    Tracker trackers[maxTrackers];
    int numTrackers = maxTrackers;
    int current = -1, previous = -1;
    double xfade = 1.0, xfadeStep = 0.005;

    double env = 0, level = 0, compEnv = 0;
    double envAtk = 0, envRel = 0, levelAtk = 0, levelRel = 0, compAtk = 0, compRel = 0, gateSmooth = 0;
    double measuredHz = 0;
    long unconfirmed = 0, unconfirmedMax = 1920;
    double gate = 0, gateTarget = 1.0;
    double confirmGain = 0, confirmAtk = 0.01;
    bool confirmedPhrase = false;
    double memoryHz = 0, memoryRise = 0.0;
    unsigned postCounter = 0;

    static constexpr double silenceThreshold = 0.0005; // ~ -66 dBFS
    static constexpr double presenceRatio = 0.08;      // une note qui s'éteint (-22 dB) ne compte plus
    static constexpr double compRef = 0.25;            // ~ -12 dBFS
    static constexpr double maxCompGain = 6.0;
};

} // namespace oc3
