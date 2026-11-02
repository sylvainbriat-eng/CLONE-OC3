// Banc d'essai hors plugin : g++ -O2 -std=c++17 -I Source tests/dsp_test.cpp -o dsp_test
// Simule des cordes pincées (Karplus-Strong) et mesure suivi, range et latence.
#include "dsp/PolyOctaveEngine.h"
#include <cstdio>
#include <vector>
#include <random>
#include <string>
#include <chrono>

static const double FS = 48000.0;

struct Note { double hz; double t0; double amp; double dur = 99.0; };

static std::vector<double> pluck (const std::vector<Note>& notes, double seconds, unsigned seed = 1)
{
    std::vector<double> out ((size_t) (seconds * FS), 0.0);
    std::mt19937 rng (seed);
    std::uniform_real_distribution<double> uni (-1.0, 1.0);
    for (auto& n : notes)
    {
        const int N = (int) std::round (FS / n.hz);
        std::vector<double> buf (N);
        for (auto& v : buf) v = uni (rng);
        double mean = 0; for (auto v : buf) mean += v; mean /= N; for (auto& v : buf) v -= mean;
        // pincement proche du chevalet : un peu de passe-haut pour un son de guitare
        double prev = 0; for (auto& v : buf) { double t = v; v = v - 0.5 * prev; prev = t; }
        const size_t start = (size_t) (n.t0 * FS);
        size_t idx = 0;
        for (size_t i = start; i < out.size(); ++i)
        {
            const double a = buf[idx], b = buf[(idx + 1) % N];
            const double damp = (double) i > (n.t0 + n.dur) * FS ? 0.75 : 0.996; // étouffement (~30 ms)
            const double y = damp * 0.5 * (a + b);
            out[i] += n.amp * buf[idx];
            buf[idx] = y;
            idx = (idx + 1) % N;
        }
    }
    return out;
}

static double autocorrPitch (const std::vector<double>& x, size_t a, size_t b, double fmin, double fmax)
{
    int lagMin = (int) (FS / fmax), lagMax = (int) (FS / fmin);
    double best = 0; int bestLag = 0;
    double e0 = 0; for (size_t i = a; i < b; ++i) e0 += x[i] * x[i];
    if (e0 < 1e-9) return 0;
    for (int L = lagMin; L <= lagMax; ++L)
    {
        double s = 0; for (size_t i = a; i + L < b; ++i) s += x[i] * x[i + L];
        s /= e0;
        if (s > best) { best = s; bestLag = L; }
    }
    return best > 0.3 ? FS / bestLag : 0;
}

static double rms (const std::vector<double>& x, size_t a, size_t b)
{
    double s = 0; for (size_t i = a; i < b; ++i) s += x[i] * x[i];
    return std::sqrt (s / (double) (b - a));
}

struct Result { double subHz, expectHz, subDb, onsetMs; double stablePct, earlyPct; };

static Result run (const std::string& name, const std::vector<Note>& notes, double range, double lowestHz, bool expectOctave, double noiseDb = -200.0, bool autoMode = false)
{
    auto in = pluck (notes, 1.5);
    {
        std::mt19937 rng (7); std::normal_distribution<double> g (0.0, std::pow (10.0, noiseDb / 20.0));
        for (auto& v : in) v += g (rng);
    }
    oc3::PolyOctaveEngine eng; eng.prepare (FS);
    oc3::PolyOctaveEngine::Params p; p.rangeHz = range; p.compact = 0.5; p.autoRange = autoMode; eng.setParams (p);
    std::vector<double> sub (in.size());
    int stable = 0, total = 0, early = 0, earlyTotal = 0;
    const double t0 = notes.back().t0;
    for (size_t i = 0; i < in.size(); ++i)
    {
        sub[i] = eng.processSample (0.5 * in[i]);
        const double tt = (double) i / FS - t0;
        if (tt > 0.03 && tt <= 0.15 && i % 48 == 0)
        {
            ++earlyTotal;
            if (std::abs (eng.getMeasuredHz() / lowestHz - 1.0) < 0.03) ++early;
        }
        if (tt > 0.15 && i % 48 == 0 && eng.getMeasuredHz() > 0)
        {
            ++total;
            if (std::abs (eng.getMeasuredHz() / lowestHz - 1.0) < 0.03) ++stable;
        }
    }
    const size_t a = (size_t) ((t0 + 0.3) * FS), b = (size_t) ((t0 + 0.8) * FS);
    Result r {};
    r.expectHz = expectOctave ? lowestHz / 2 : 0;
    const double subR = rms (sub, a, b), inR = rms (in, a, b) * 0.5;
    r.subDb = 20 * std::log10 (subR / inR + 1e-12);
    r.subHz = subR > 1e-4 ? autocorrPitch (sub, a, b, 20, 400) : 0;
    // latence : temps pour que l'enveloppe de l'octave atteigne 50% de son max (fenêtre 1 ms)
    double mx = 0; std::vector<double> e;
    for (size_t i = (size_t) (t0 * FS); i < (size_t) ((t0 + 0.25) * FS); i += 48) { double v = rms (sub, i, i + 48); e.push_back (v); mx = std::max (mx, v); }
    r.onsetMs = -1; for (size_t k = 0; k < e.size(); ++k) if (e[k] > 0.5 * mx) { r.onsetMs = (double) k; break; }
    r.stablePct = total ? 100.0 * stable / total : 0;
    r.earlyPct = earlyTotal ? 100.0 * early / earlyTotal : 0;
    const bool ok = expectOctave ? (std::abs (r.subHz / r.expectHz - 1) < 0.04 && r.subDb > -12)
                                 : (r.subDb < -30);
    std::printf ("%-34s range=%4.0fHz  attendu=%6.1fHz  mesuré=%6.1fHz  niveau=%6.1fdB  attaque=%4.0fms  suivi=%5.1f%% (30-150ms: %5.1f%%)  %s\n",
                 name.c_str(), range, r.expectHz, r.subHz, r.subDb, r.onsetMs, r.stablePct, r.earlyPct, ok ? "OK" : "ECHEC");
    return r;
}

// Bruits non tonaux : l'octave doit rester faible.
static std::vector<double> percussion (int kind, double seconds)
{
    std::vector<double> out ((size_t) (seconds * FS), 0.0);
    std::mt19937 rng (3); std::uniform_real_distribution<double> uni (-1.0, 1.0);
    const size_t t0 = (size_t) (0.05 * FS);
    if (kind == 0) // frappe sur la caisse : résonance de bois amortie + choc
    {
        for (size_t i = 0; i < (size_t) (0.25 * FS); ++i)
        {
            const double t = i / FS;
            double v = 0.8 * std::exp (-t / 0.012) * std::sin (2 * M_PI * 105.0 * t + 0.6 * std::sin (2 * M_PI * 37.0 * t));
            v += 0.5 * std::exp (-t / 0.008) * std::sin (2 * M_PI * 230.0 * t);
            if (i < (size_t) (0.003 * FS)) v += 0.8 * uni (rng);
            out[t0 + i] += v;
        }
    }
    else if (kind == 1) // cordes étouffées grattées : 4 coups de bruit
    {
        for (int b = 0; b < 4; ++b)
        {
            double lpS = 0, hpS = 0, prev = 0;
            const size_t s0 = t0 + (size_t) (b * 0.12 * FS);
            for (size_t i = 0; i < (size_t) (0.025 * FS); ++i)
            {
                const double n = uni (rng) * std::exp (-(double) i / (0.006 * FS));
                lpS += 0.15 * (n - lpS);           // passe-bas ~1,2 kHz
                hpS = 0.98 * (hpS + lpS - prev);   // passe-haut ~150 Hz
                prev = lpS;
                out[s0 + i] += 2.5 * hpS;
            }
        }
    }
    else // attaque de médiator sans note : clic de 1 ms
    {
        for (size_t i = 0; i < (size_t) (0.001 * FS); ++i) out[t0 + i] += 0.9 * uni (rng);
    }
    return out;
}

static void runNoise (const char* name, int kind)
{
    // référence : niveau d'octave d'un Mi grave joué à la même force
    auto ref = pluck ({ { 82.41, 0.05, 0.8 } }, 0.6);
    std::printf ("%-34s", name);
    for (double sens : { 0.0, 0.5, 1.0 })
    {
        auto measurePeak = [&] (const std::vector<double>& in)
        {
            oc3::PolyOctaveEngine eng; eng.prepare (FS);
            oc3::PolyOctaveEngine::Params p; p.rangeHz = 150; p.sensitivity = sens; eng.setParams (p);
            double peak = 0, acc = 0; int cnt = 0;
            for (size_t i = 0; i < in.size(); ++i)
            {
                const double y = eng.processSample (0.5 * in[i]);
                acc += y * y; if (++cnt == 240) { peak = std::max (peak, std::sqrt (acc / cnt)); acc = 0; cnt = 0; }
            }
            return peak;
        };
        const double leak = 20 * std::log10 (measurePeak (percussion (kind, 0.6)) / measurePeak (ref) + 1e-12);
        std::printf ("  sensibilité %3.0f%% : %6.1f dB", sens * 100, leak);
    }
    std::printf ("\n");
}

int main()
{
    const double E2 = 82.41, A2 = 110.0, D2 = 73.42, C2 = 65.41, B2 = 123.47, E3 = 164.81, Gs3 = 207.65,
                 B3 = 246.94, E4 = 329.63, A3 = 220.0, D3 = 146.83, Fs3 = 185.0, D4 = 293.66, G2 = 98.0, C3 = 130.81, G3 = 196.0;
    auto strum = [] (std::vector<double> hz) { std::vector<Note> v; double t = 0.05; for (double h : hz) { v.push_back ({ h, t, 0.6 }); t += 0.012; } return v; };

    run ("Mi grave seul (E2)", { { E2, 0.05, 0.8 } }, 150, E2, true);
    run ("La (A2)", { { A2, 0.05, 0.8 } }, 150, A2, true);
    run ("Accord Mi majeur ouvert", strum ({ E2, B2, E3, Gs3, B3, E4 }), 150, E2, true);
    run ("Open D : accord D A D F# A D", strum ({ D2, A2, D3, Fs3, A3, D4 }), 150, D2, true);
    run ("Open C : accord C G C G C E", strum ({ C2, G2, C3, G3, 261.63, E4 }), 150, C2, true);
    run ("Power chord E2+B2+E3", strum ({ E2, B2, E3 }), 150, E2, true);
    run ("Accord La (A2 E3 A3 C#4 E4)", strum ({ A2, E3, A3, 277.18, E4 }), 150, A2, true);
    run ("Mi aigu seul (E4) -> pas d'octave", { { E4, 0.05, 0.8 } }, 150, E4, false);
    run ("Ré (D3) range 120 -> pas d'octave", { { D3, 0.05, 0.8 } }, 120, D3, false);
    run ("Ré (D3) range 200 -> octave", { { D3, 0.05, 0.8 } }, 200, D3, true);
    run ("Si (B3 247Hz) range 150 -> pas d'oct.", { { B3, 0.05, 0.8 } }, 150, B3, false);
    run ("Sol (G3 196Hz) range 150 -> pas d'oct.", { { G3, 0.05, 0.8 } }, 150, G3, false);
    run ("Enchaînement E2 puis A2", { { E2, 0.05, 0.8, 0.30 }, { A2, 0.40, 0.8 } }, 150, A2, true);
    run ("Enchaînement A2 puis E2", { { A2, 0.05, 0.8, 0.30 }, { E2, 0.40, 0.8 } }, 150, E2, true);
    run ("Basse tenue E2 + mélodie aiguë", { { E2, 0.05, 0.8 }, { B3, 0.30, 0.8 } }, 150, E2, true);
    run ("Accord Mi + bruit -50 dB", strum ({ E2, B2, E3, Gs3, B3, E4 }), 150, E2, true, -50.0);
    run ("Baritone Si grave (B1 61.7Hz)", { { 61.74, 0.05, 0.8 } }, 150, 61.74, true);
    run ("Drop C accord C2 G2 C3", strum ({ C2, G2, C3 }), 150, C2, true);

    run ("AUTO : solo seul Si3 -> octave", { { B3, 0.05, 0.8 } }, 150, B3, true, -200.0, true);
    run ("AUTO : Mi2 puis Si3 seul -> pas d'oct.", { { E2, 0.05, 0.8, 0.30 }, { B3, 0.40, 0.8 } }, 150, B3, false, -200.0, true);
    run ("AUTO : accord Mi majeur", strum ({ E2, B2, E3, Gs3, B3, E4 }), 150, E2, true, -200.0, true);
    run ("AUTO : open D", strum ({ D2, A2, D3, Fs3, A3, D4 }), 150, D2, true, -200.0, true);
    {
        // TIME très court : le plafond suit immédiatement -> la mélodie seule est octaviée
        auto in = pluck ({ { E2, 0.05, 0.8, 0.30 }, { B3, 0.40, 0.8 } }, 1.5);
        oc3::PolyOctaveEngine eng; eng.prepare (FS);
        oc3::PolyOctaveEngine::Params p; p.autoRange = true; p.timeSeconds = 0.05; eng.setParams (p);
        std::vector<double> sub (in.size());
        for (size_t i = 0; i < in.size(); ++i) sub[i] = eng.processSample (0.5 * in[i]);
        const size_t a = (size_t) (0.7 * FS), b = (size_t) (1.2 * FS);
        const double db = 20 * std::log10 (rms (sub, a, b) / (0.5 * rms (in, a, b)) + 1e-12);
        const double hz = autocorrPitch (sub, a, b, 20, 400);
        const bool ok = std::abs (hz / (B3 / 2) - 1) < 0.04 && db > -12;
        std::printf ("%-34s attendu=%6.1fHz  mesuré=%6.1fHz  niveau=%6.1fdB  %s\n", "AUTO TIME court : Mi2 puis Si3", B3 / 2, hz, db, ok ? "OK" : "ECHEC");
    }
    run ("AUTO : Mi4 seul (330 Hz) -> octave", { { E4, 0.05, 0.8 } }, 150, E4, true, -200.0, true);

    {
        // Accords tenus (sons purs, volumes de cordes aléatoires de 1 à 6 fois, phases
        // aléatoires) à 44,1 / 48 / 96 kHz, en manuel et en AUTO : la note la plus grave
        // doit être suivie et octaviée plus de 98 % du temps.
        const std::vector<std::vector<double>> chords = { { E2, B2 }, { E2, E3 }, { E2, B2, E3 }, { E2, B2, E3, Gs3 },
                                                           { D2, A2, D3 }, { A2, E3, A3 }, { G2, D3, G3 } };
        std::mt19937 rng (5); std::uniform_real_distribution<double> u (0, 1);
        int worstBad = 0, worstTot = 1, cases = 0;
        for (double fs : { 44100.0, 48000.0, 96000.0 })
            for (int aut = 0; aut < 2; ++aut)
                for (auto& ch : chords)
                {
                    int bad = 0, tot = 0;
                    for (int tr = 0; tr < 10; ++tr)
                    {
                        std::vector<double> ph, am;
                        for (size_t k = 0; k < ch.size(); ++k) { ph.push_back (u (rng) * 6.28); am.push_back (0.05 + 0.25 * u (rng)); }
                        oc3::PolyOctaveEngine e; e.prepare (fs);
                        oc3::PolyOctaveEngine::Params p; p.rangeHz = 150; p.autoRange = aut != 0; e.setParams (p);
                        for (size_t i = 0; i < (size_t) fs; ++i)
                        {
                            double x = 0; for (size_t k = 0; k < ch.size(); ++k) x += am[k] * std::sin (2 * M_PI * ch[k] * i / fs + ph[k]);
                            e.processSample (x);
                            if (i > fs * 0.2 && i % 64 == 0)
                            {
                                ++tot;
                                if (! (std::abs (e.getMeasuredHz() / ch[0] - 1) < 0.03 && e.getGate() > 0.9)) ++bad;
                            }
                        }
                    }
                    ++cases;
                    if (bad * worstTot > worstBad * tot) { worstBad = bad; worstTot = tot; }
                }
        const double worst = 100.0 * worstBad / worstTot;
        std::printf ("%-34s %d séries de 10 accords, pire série : %.1f %% du temps faux  %s\n",
                     "Accords tenus (sons purs)", cases, worst, worst < 2.0 ? "OK" : "ECHEC");
    }

    {
        // FORME : l'octave doit rester à la bonne hauteur quelle que soit la forme
        const std::vector<Note> chord = strum ({ E2, B2, E3, Gs3, B3, E4 });
        for (double sh : { 0.0, 0.5, 1.0 })
        {
            auto in = pluck (chord, 1.5);
            oc3::PolyOctaveEngine eng; eng.prepare (FS);
            oc3::PolyOctaveEngine::Params p; p.shape = sh; eng.setParams (p);
            std::vector<double> sub (in.size());
            for (size_t i = 0; i < in.size(); ++i) sub[i] = eng.processSample (0.5 * in[i]);
            const size_t a = (size_t) (0.4 * FS), b = (size_t) (0.9 * FS);
            const double hz = autocorrPitch (sub, a, b, 20, 400);
            const double db = 20 * std::log10 (rms (sub, a, b) / (0.5 * rms (in, a, b)) + 1e-12);
            char name[64]; std::snprintf (name, sizeof name, "FORME %3.0f %% (accord de Mi)", sh * 100);
            const bool ok = std::abs (hz / (E2 / 2) - 1) < 0.04 && db > -12;
            std::printf ("%-34s attendu=%6.1fHz  mesuré=%6.1fHz  niveau=%6.1fdB  %s\n", name, E2 / 2, hz, db, ok ? "OK" : "ECHEC");
        }
    }

    std::printf ("\nBruits non tonaux (octave produite, par rapport à celle d'un Mi grave) :\n");
    runNoise ("Frappe sur la caisse", 0);
    runNoise ("Cordes étouffées grattées", 1);
    runNoise ("Médiator sans note", 2);
    {
        // prix à payer : temps pour que l'octave d'un Mi grave atteigne -6 dB de son niveau
        auto in = pluck ({ { 82.41, 0.05, 0.8 } }, 0.5);
        std::printf ("%-34s", "Attaque d'un Mi grave");
        for (double sens : { 0.0, 0.5, 1.0 })
        {
            oc3::PolyOctaveEngine eng; eng.prepare (FS);
            oc3::PolyOctaveEngine::Params p; p.rangeHz = 150; p.sensitivity = sens; eng.setParams (p);
            std::vector<double> e; double acc = 0; int cnt = 0;
            for (size_t i = 0; i < in.size(); ++i)
            {
                const double y = eng.processSample (0.5 * in[i]);
                acc += y * y; if (++cnt == 48) { e.push_back (std::sqrt (acc / cnt)); acc = 0; cnt = 0; }
            }
            double mx = 0; for (size_t k = 50; k < 300 && k < e.size(); ++k) mx = std::max (mx, e[k]);
            int ms = -1; for (size_t k = 50; k < e.size(); ++k) if (e[k] > 0.5 * mx) { ms = (int) k - 50; break; }
            std::printf ("  sensibilité %3.0f%% : %4d ms   ", sens * 100, ms);
        }
        std::printf ("\n\n");
    }

    // Charge processeur : secondes de calcul pour 10 s de son
    {
        auto in = pluck (strum ({ E2, B2, E3, Gs3, B3, E4 }), 10.0);
        oc3::PolyOctaveEngine eng; eng.prepare (FS);
        oc3::PolyOctaveEngine::Params p; p.rangeHz = 400; p.autoRange = true; eng.setParams (p);
        auto c0 = std::chrono::steady_clock::now();
        double acc = 0; for (double v : in) acc += eng.processSample (v);
        double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - c0).count();
        std::printf ("Charge CPU (mode AUTO) : %.3f s de calcul pour 10 s audio (%.2f %% d'un coeur) [%g]\n", sec, sec * 10.0, acc * 0);
    }
    return 0;
}
