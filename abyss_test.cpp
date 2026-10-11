// Stand-alone verification of the ABYSS engine.
//   g++ -O2 -std=c++17 -Wall -Wextra -I.. abyss_test.cpp -o abyss_test && ./abyss_test
#include "AbyssEngine.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>

using namespace abyss;

static int g_fail = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++g_fail; std::printf("   FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static double db (double x) { return 20.0 * std::log10 (std::max (x, 1.0e-12)); }

struct Stereo { std::vector<float> l, r; };

static double rmsOf (const std::vector<float>& v, size_t a, size_t b)
{
    double s = 0; for (size_t i = a; i < b; ++i) s += (double) v[i] * v[i];
    return std::sqrt (s / (double) (b - a));
}
static double peakOf (const std::vector<float>& v)
{
    double p = 0; for (float x : v) p = std::max (p, (double) std::fabs (x)); return p;
}
static double meanOf (const std::vector<float>& v, size_t a, size_t b)
{
    double s = 0; for (size_t i = a; i < b; ++i) s += v[i]; return s / (double) (b - a);
}
static bool allFinite (const std::vector<float>& v)
{
    for (float x : v)
        if (! std::isfinite (x)) return false;
    return true;
}
static double amplitudeAt (const std::vector<float>& v, size_t a, size_t b, double f, double fs)
{
    double re = 0, im = 0; const double w = 2.0 * kPi * f / fs;
    for (size_t i = a; i < b; ++i) { re += v[i] * std::cos (w * (double) i); im -= v[i] * std::sin (w * (double) i); }
    return 2.0 * std::sqrt (re * re + im * im) / (double) (b - a);
}

static std::vector<float> sine (double fs, double f, double amp, double sec)
{
    std::vector<float> v ((size_t) (fs * sec));
    for (size_t i = 0; i < v.size(); ++i) v[i] = (float) (amp * std::sin (2.0 * kPi * f * (double) i / fs));
    return v;
}
static std::vector<float> noise (double fs, double amp, double sec, unsigned seed)
{
    std::mt19937 g (seed); std::uniform_real_distribution<float> d (-1.0f, 1.0f);
    std::vector<float> v ((size_t) (fs * sec));
    for (auto& x : v) x = (float) amp * d (g);
    return v;
}
static std::vector<float> drums (double fs, double sec)
{
    std::mt19937 g (5); std::uniform_real_distribution<float> d (-1.0f, 1.0f);
    std::vector<float> v ((size_t) (fs * sec), 0.0f);
    for (double t0 = 0.0; t0 < sec; t0 += 0.5)               // kick
        for (size_t i = 0; i < (size_t) (0.3 * fs); ++i)
        {
            const size_t k = (size_t) (t0 * fs) + i; if (k >= v.size()) break;
            const double t = (double) i / fs;
            v[k] += (float) (0.9 * std::exp (-t * 14.0) * std::sin (2.0 * kPi * (50.0 + 90.0 * std::exp (-t * 40.0)) * t));
        }
    for (double t0 = 0.25; t0 < sec; t0 += 0.5)              // snare/hat noise burst
        for (size_t i = 0; i < (size_t) (0.12 * fs); ++i)
        {
            const size_t k = (size_t) (t0 * fs) + i; if (k >= v.size()) break;
            v[k] += 0.5f * d (g) * (float) std::exp (-(double) i / fs * 35.0);
        }
    return v;
}

static Stereo run (const std::vector<float>& inL, const std::vector<float>* inR,
                   double fs, double c, double f, double a, int block = 512)
{
    Engine e; e.setParams (c, f, a); e.prepare (fs);
    Stereo o; o.l = inL; o.r = inR ? *inR : inL;
    for (size_t p = 0; p < o.l.size(); p += (size_t) block)
    {
        const int n = (int) std::min<size_t> ((size_t) block, o.l.size() - p);
        e.process (o.l.data() + p, o.r.data() + p, n);
    }
    return o;
}

struct Cfg { double c, f, a; const char* name; };
static const Cfg kGrid[] = {
    { 0.00, 0.00, 0.00, "0/0/0   " }, { 0.35, 0.25, 0.10, "default " }, { 1.00, 1.00, 1.00, "max     " },
    { 1.00, 0.00, 0.00, "C only  " }, { 0.00, 1.00, 0.00, "F only  " }, { 0.00, 0.00, 1.00, "A only  " },
    { 1.00, 1.00, 0.00, "C+F     " }, { 0.00, 1.00, 1.00, "F+A     " } };

int main (int argc, char** argv)
{
    const double fs = (argc > 1) ? std::atof (argv[1]) : 176400.0;   // oversampled rate (4 x 44.1k)
    std::printf ("ABYSS engine test-suite, processing rate %.0f Hz\n\n", fs);

    //--------------------------------------------------------------------------
    std::printf ("[T0] crossover recombination (LR4 x2 + allpass) is flat\n");
    {
        double worst = 0;
        for (double fr : { 40.0, 100.0, 180.0, 500.0, 1000.0, 2200.0, 5000.0, 12000.0 })
        {
            Split3 s; s.prepare (fs, 180.0, 2200.0);
            auto x = sine (fs, fr, 0.5, 1.0); std::vector<float> y (x.size());
            for (size_t i = 0; i < x.size(); ++i) { double lo, mi, hi; s.process (x[i], lo, mi, hi); y[i] = (float) (lo + mi + hi); }
            const double g = amplitudeAt (y, x.size() / 2, x.size(), fr, fs) / 0.5;
            worst = std::max (worst, std::fabs (db (g)));
        }
        std::printf ("   worst deviation: %.4f dB\n", worst);
        CHECK (worst < 0.05, "crossover sum not flat (%.3f dB)", worst);
    }

    //--------------------------------------------------------------------------
    std::printf ("[T1] silence in -> silence out\n");
    for (auto& cfg : kGrid)
    {
        std::vector<float> z ((size_t) (fs * 1.0), 0.0f);
        auto o = run (z, nullptr, fs, cfg.c, cfg.f, cfg.a);
        const double p = std::max (peakOf (o.l), peakOf (o.r));
        CHECK (p == 0.0, "%s: silence produced output %.3e", cfg.name, p);
    }
    std::printf ("   ok (exact zeros for all %d settings)\n", (int) (sizeof (kGrid) / sizeof (kGrid[0])));

    //--------------------------------------------------------------------------
    std::printf ("[T2] stability / bounds / DC on test signals  (peak dBFS | |DC| | RMS out-in dB)\n");
    struct Sig { const char* name; std::vector<float> x; };
    std::vector<Sig> sigs;
    sigs.push_back ({ "sine 55 Hz  -6dB ", sine (fs, 55.0, 0.5, 3.0) });
    sigs.push_back ({ "sine 440 Hz -12dB", sine (fs, 440.0, 0.25, 3.0) });
    sigs.push_back ({ "sine 5 kHz  -12dB", sine (fs, 5000.0, 0.25, 3.0) });
    sigs.push_back ({ "noise  -18dB rms ", noise (fs, 0.2165, 3.0, 11) });
    sigs.push_back ({ "drums            ", drums (fs, 3.0) });
    sigs.push_back ({ "DC offset +0.3   ", std::vector<float> ((size_t) (fs * 3.0), 0.3f) });
    {
        std::vector<float> imp ((size_t) (fs * 2.0), 0.0f); imp[100] = 1.0f;
        sigs.push_back ({ "impulse 1.0      ", imp });
    }
    for (auto& s : sigs)
    {
        std::printf ("  %s\n", s.name);
        for (auto& cfg : kGrid)
        {
            auto o = run (s.x, nullptr, fs, cfg.c, cfg.f, cfg.a);
            const size_t n = o.l.size();
            const double pk = std::max (peakOf (o.l), peakOf (o.r));
            const double dc = std::fabs (meanOf (o.l, n - (size_t) fs, n));
            const double gain = db (rmsOf (o.l, n / 2, n)) - db (std::max (rmsOf (s.x, n / 2, n), 1e-9));
            std::printf ("    %s  %7.2f | %.5f | %+6.1f\n", cfg.name, db (pk), dc, gain);
            CHECK (allFinite (o.l) && allFinite (o.r), "%s/%s: NaN/Inf", s.name, cfg.name);
            CHECK (pk <= 1.0000001, "%s/%s: peak %.4f above ceiling", s.name, cfg.name, pk);
            if (std::strncmp (s.name, "DC", 2) != 0 && std::strncmp (s.name, "impulse", 7) != 0)
                CHECK (dc < 0.02, "%s/%s: DC %.4f", s.name, cfg.name, dc);
        }
    }

    //--------------------------------------------------------------------------
    std::printf ("[T3] bass fundamental protection: 55 Hz sine at -6 dBFS\n");
    {
        auto x = sine (fs, 55.0, 0.5, 2.0);
        const size_t a0 = (size_t) fs, b0 = x.size();
        for (auto& cfg : kGrid)
        {
            auto o = run (x, nullptr, fs, cfg.c, cfg.f, cfg.a);
            const double f1 = amplitudeAt (o.l, a0, b0, 55.0, fs);
            std::printf ("   %s fundamental %+6.1f dB vs in |", cfg.name, db (f1 / 0.5));
            for (int h = 2; h <= 5; ++h) std::printf (" H%d %+6.1f", h, db (amplitudeAt (o.l, a0, b0, 55.0 * h, fs) / std::max (f1, 1e-9)));
            std::printf (" dBc\n");
            if (cfg.c == 0.0 && cfg.a == 0.0)    // FRACTURE alone must not destroy the fundamental
                CHECK (db (f1 / 0.5) > -3.0, "%s: fundamental lost (%.1f dB)", cfg.name, db (f1 / 0.5));
        }
    }

    //--------------------------------------------------------------------------
    std::printf ("[T4] AFTERSHOCK tail decays after the input stops\n");
    for (auto& cfg : { Cfg { 1, 1, 1, "max     " }, Cfg { 0, 0, 1, "A only  " }, Cfg { 0.35, 0.25, 0.10, "default " } })
    {
        auto x = noise (fs, 0.5, 4.0, 3);
        const size_t burst = (size_t) (0.1 * fs);
        for (size_t i = burst; i < x.size(); ++i) x[i] = 0.0f;
        auto o = run (x, nullptr, fs, cfg.c, cfg.f, cfg.a);
        const size_t w = (size_t) (0.01 * fs);
        double tBelow = -1;
        for (size_t i = burst; i + w < o.l.size(); i += w)
            if (db (rmsOf (o.l, i, i + w)) < -80.0) { tBelow = (double) (i - burst) / fs; break; }
        const double late = db (rmsOf (o.l, burst + (size_t) (2.0 * fs), burst + (size_t) (2.5 * fs)));
        std::printf ("   %s fell below -80 dBFS after %.3f s, RMS at +2.0..2.5 s: %.1f dBFS\n", cfg.name, tBelow, late);
        CHECK (tBelow > 0 && tBelow < 2.0, "%s: tail too long / never ends", cfg.name);
        CHECK (late < -100.0, "%s: sustained tone after silence (%.1f dB)", cfg.name, late);
    }

    //--------------------------------------------------------------------------
    std::printf ("[T5] automation stress (random parameter jumps every 10 ms, 6 s of noise)\n");
    {
        auto runStress = [&] (std::vector<float>& outL)
        {
            Engine e; e.setParams (0.5, 0.5, 0.5); e.prepare (fs);
            std::mt19937 g (1); std::uniform_real_distribution<double> u (0.0, 1.0);
            auto x = noise (fs, 0.3, 6.0, 9);
            std::vector<float> r = x;
            const int step = (int) (fs * 0.010);
            for (size_t p = 0; p < x.size(); p += (size_t) step)
            {
                auto pick = [&] { const double v = u (g); return v < 0.1 ? 0.0 : (v > 0.9 ? 1.0 : u (g)); };
                e.setParams (pick(), pick(), pick());
                const int n = (int) std::min<size_t> ((size_t) step, x.size() - p);
                e.process (x.data() + p, r.data() + p, n);
            }
            outL = x;
        };
        std::vector<float> o1, o2; runStress (o1); runStress (o2);
        const double pk = peakOf (o1);
        std::printf ("   finite: %s, peak %.2f dBFS, deterministic (two runs identical): %s\n",
                     allFinite (o1) ? "yes" : "NO", db (pk), (o1 == o2) ? "yes" : "NO");
        CHECK (allFinite (o1), "NaN/Inf under automation");
        CHECK (pk <= 1.0000001, "peak above ceiling under automation");
        CHECK (o1 == o2, "engine is not deterministic");
    }

    //--------------------------------------------------------------------------
    std::printf ("[T6] feedback-loop contractivity\n");
    {
        double maxSlope = 0; const double h = 1.0e-5;
        for (double u = -20.0; u <= 20.0; u += 1.0e-3)
            maxSlope = std::max (maxSlope, std::fabs ((loopSat (u + h) - loopSat (u - h)) / (2 * h)));
        const double loopGain = 0.9 * maxSlope;   // 0.9 = the feedback cap in the engine
        std::printf ("   max slope of loop saturator = %.4f, worst-case loop gain = 0.9 x slope = %.4f (< 1)\n", maxSlope, loopGain);
        CHECK (loopGain < 1.0, "loop gain %.3f not contractive", loopGain);
    }

    //--------------------------------------------------------------------------
    std::printf ("[T7] stereo behaviour\n");
    {
        auto x = noise (fs, 0.3, 2.0, 21);
        for (auto& cfg : { Cfg { 1, 1, 0, "C+F     " }, Cfg { 0.35, 0.25, 0.0, "default no A" } })
        {
            auto o = run (x, &x, fs, cfg.c, cfg.f, cfg.a);
            double d = 0; for (size_t i = 0; i < o.l.size(); ++i) d = std::max (d, (double) std::fabs (o.l[i] - o.r[i]));
            std::printf ("   identical L=R in, AFTERSHOCK off (%s): max |L-R| = %.2e\n", cfg.name, d);
            CHECK (d == 0.0, "mono-compat broken: L != R");
        }
        auto s = sine (fs, 440.0, 0.3, 1.5); std::vector<float> zero (s.size(), 0.0f);
        for (auto& cfg : { Cfg { 0.35, 0.25, 0.10, "default " }, Cfg { 1, 1, 1, "max     " } })
        {
            auto o = run (s, &zero, fs, cfg.c, cfg.f, cfg.a);
            const size_t n = o.l.size();
            std::printf ("   L-only input, %s: R / L level = %.1f dB\n", cfg.name, db (rmsOf (o.r, n / 2, n)) - db (rmsOf (o.l, n / 2, n)));
        }
    }

    //--------------------------------------------------------------------------
    std::printf ("[T8] level consistency: output RMS minus input RMS in dB (noise at -30 / -18 / -6 dBFS rms)\n");
    for (double lvl : { -30.0, -18.0, -6.0 })
    {
        auto x = noise (fs, std::pow (10.0, lvl / 20.0) * 1.732, 3.0, 4);   // uniform noise: rms = amp / sqrt(3)
        const size_t n = x.size();
        std::printf ("   in %+4.0f dBFS  COLLAPSE only:", lvl);
        for (double c : { 0.0, 0.2, 0.4, 0.6, 0.8, 1.0 })
        {
            auto o = run (x, nullptr, fs, c, 0, 0);
            std::printf (" %+5.1f", db (rmsOf (o.l, n / 2, n)) - db (rmsOf (x, n / 2, n)));
        }
        std::printf ("   | all three together:");
        for (double v : { 0.0, 0.2, 0.4, 0.6, 0.8, 1.0 })
        {
            auto o = run (x, nullptr, fs, v, v, v);
            std::printf (" %+5.1f", db (rmsOf (o.l, n / 2, n)) - db (rmsOf (x, n / 2, n)));
        }
        std::printf ("\n");
    }

    //--------------------------------------------------------------------------
    std::printf ("[T9] CPU cost (this sandbox, single core, -O2) - 10 s of stereo audio\n");
    for (auto& cfg : { Cfg { 0.35, 0.25, 0.10, "default " }, Cfg { 1, 1, 1, "max     " } })
    {
        auto x = noise (fs, 0.3, 10.0, 2);
        const auto t0 = std::chrono::steady_clock::now();
        auto o = run (x, nullptr, fs, cfg.c, cfg.f, cfg.a);
        const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("   %s %.2f s wall for 10 s of audio at %.0f Hz  (%.1fx real time, ~%.1f%% of one core)\n",
                     cfg.name, sec, fs, 10.0 / sec, 100.0 * sec / 10.0);
    }

    std::printf ("\n%s (%d failed check%s)\n", g_fail == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail == 0 ? 0 : 1;
}
