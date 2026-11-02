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

static Result run (const std::string& name, const std::vector<Note>& notes, double range, double lowestHz, bool expectOctave, double noiseDb = -200.0)
{
    auto in = pluck (notes, 1.5);
    {
        std::mt19937 rng (7); std::normal_distribution<double> g (0.0, std::pow (10.0, noiseDb / 20.0));
        for (auto& v : in) v += g (rng);
    }
    oc3::PolyOctaveEngine eng; eng.prepare (FS);
    oc3::PolyOctaveEngine::Params p; p.rangeHz = range; p.compact = 0.5; eng.setParams (p);
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

    // Charge processeur : secondes de calcul pour 10 s de son
    {
        auto in = pluck (strum ({ E2, B2, E3, Gs3, B3, E4 }), 10.0);
        oc3::PolyOctaveEngine eng; eng.prepare (FS);
        oc3::PolyOctaveEngine::Params p; p.rangeHz = 400; eng.setParams (p);
        auto c0 = std::chrono::steady_clock::now();
        double acc = 0; for (double v : in) acc += eng.processSample (v);
        double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - c0).count();
        std::printf ("Charge CPU (range max) : %.3f s de calcul pour 10 s audio (%.2f %% d'un coeur) [%g]\n", sec, sec * 10.0, acc * 0);
    }
    return 0;
}
