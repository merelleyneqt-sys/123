#pragma once
// =============================================================================
//  ABYSS - Nonlinear Harmonic Collapse
//
//  Header-only, JUCE-independent saturation engine (plain C++17).
//  It is meant to run INSIDE an oversampled block: prepare() takes the
//  oversampled sample rate and process() takes oversampled audio.
//
//  Signal flow (per sample, stereo-linked analysis):
//
//     in --> [analysis: transient / sustain / level gate]
//        --> COLLAPSE   drive + transient compression + asymmetric transfer
//                       curve that morphs into a wavefolder
//        --> FRACTURE   3-band LR4 split (phase-coherent), each band hit by a
//                       different nonlinear network, cross-band intermodulation,
//                       slow bounded random "instability"
//        --> AFTERSHOCK 4-line feedback delay network, saturating + damped +
//                       high-passed loop, provably contractive (loop gain < 1)
//        --> makeup, DC blocker, soft output ceiling, NaN guard
//
//  No allocations in process(). All state is double precision.
// =============================================================================

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace abyss
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kQ  = 0.70710678118654752440;   // Butterworth Q

inline double clampd (double v, double lo, double hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }

inline double smoothstep (double e0, double e1, double x) noexcept
{
    const double t = clampd ((x - e0) / (e1 - e0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

inline double dbToGain (double db) noexcept { return std::pow (10.0, db * 0.05); }

//------------------------------------------------------------------------------
// Biquad, transposed direct form II, double precision. RBJ designs.
//------------------------------------------------------------------------------
struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0, z1 = 0.0, z2 = 0.0;

    inline double process (double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void reset() noexcept { z1 = z2 = 0.0; }

    void copyCoeffs (const Biquad& o) noexcept { b0 = o.b0; b1 = o.b1; b2 = o.b2; a1 = o.a1; a2 = o.a2; }

    void setRaw (double nb0, double nb1, double nb2, double na0, double na1, double na2) noexcept
    {
        const double inv = 1.0 / na0;
        b0 = nb0 * inv; b1 = nb1 * inv; b2 = nb2 * inv; a1 = na1 * inv; a2 = na2 * inv;
    }

    void setLowpass (double fs, double fc, double q = kQ) noexcept
    {
        const double w0 = 2.0 * kPi * clampd (fc, 1.0, fs * 0.49) / fs;
        const double cw = std::cos (w0), al = std::sin (w0) / (2.0 * q);
        setRaw ((1.0 - cw) * 0.5, 1.0 - cw, (1.0 - cw) * 0.5, 1.0 + al, -2.0 * cw, 1.0 - al);
    }

    void setHighpass (double fs, double fc, double q = kQ) noexcept
    {
        const double w0 = 2.0 * kPi * clampd (fc, 1.0, fs * 0.49) / fs;
        const double cw = std::cos (w0), al = std::sin (w0) / (2.0 * q);
        setRaw ((1.0 + cw) * 0.5, -(1.0 + cw), (1.0 + cw) * 0.5, 1.0 + al, -2.0 * cw, 1.0 - al);
    }

    void setAllpass (double fs, double fc, double q = kQ) noexcept
    {
        const double w0 = 2.0 * kPi * clampd (fc, 1.0, fs * 0.49) / fs;
        const double cw = std::cos (w0), al = std::sin (w0) / (2.0 * q);
        setRaw (1.0 - al, -2.0 * cw, 1.0 + al, 1.0 + al, -2.0 * cw, 1.0 - al);
    }

    // gainLin is the linear gain of the shelf (same convention as juce::dsp)
    void setLowShelf (double fs, double fc, double q, double gainLin) noexcept
    {
        const double A = std::sqrt (std::max (gainLin, 0.0));
        const double w0 = 2.0 * kPi * clampd (fc, 2.0, fs * 0.49) / fs;
        const double cw = std::cos (w0), beta = std::sin (w0) * std::sqrt (A) / q;
        setRaw (A * ((A + 1.0) - (A - 1.0) * cw + beta),
                2.0 * A * ((A - 1.0) - (A + 1.0) * cw),
                A * ((A + 1.0) - (A - 1.0) * cw - beta),
                (A + 1.0) + (A - 1.0) * cw + beta,
                -2.0 * ((A - 1.0) + (A + 1.0) * cw),
                (A + 1.0) + (A - 1.0) * cw - beta);
    }

    void setHighShelf (double fs, double fc, double q, double gainLin) noexcept
    {
        const double A = std::sqrt (std::max (gainLin, 0.0));
        const double w0 = 2.0 * kPi * clampd (fc, 2.0, fs * 0.49) / fs;
        const double cw = std::cos (w0), beta = std::sin (w0) * std::sqrt (A) / q;
        setRaw (A * ((A + 1.0) + (A - 1.0) * cw + beta),
                -2.0 * A * ((A - 1.0) + (A + 1.0) * cw),
                A * ((A + 1.0) + (A - 1.0) * cw - beta),
                (A + 1.0) - (A - 1.0) * cw + beta,
                2.0 * ((A - 1.0) - (A + 1.0) * cw),
                (A + 1.0) - (A - 1.0) * cw - beta);
    }
};

// Linkwitz-Riley 4th order = two identical Butterworth biquads
struct LR4
{
    Biquad s1, s2;
    void setLowpass  (double fs, double fc) noexcept { s1.setLowpass  (fs, fc); s2.copyCoeffs (s1); }
    void setHighpass (double fs, double fc) noexcept { s1.setHighpass (fs, fc); s2.copyCoeffs (s1); }
    inline double process (double x) noexcept { return s2.process (s1.process (x)); }
    void reset() noexcept { s1.reset(); s2.reset(); }
};

// 3-band LR4 splitter. The low band gets a 2nd-order all-pass at the upper
// crossover (exactly LP4+HP4 of that crossover), so low+mid+high sums to a
// flat-magnitude all-pass: no cancellation holes when the bands recombine.
struct Split3
{
    LR4 lo1, hi1, lo2, hi2;
    Biquad ap;

    void prepare (double fs, double f1, double f2) noexcept
    {
        lo1.setLowpass (fs, f1);  hi1.setHighpass (fs, f1);
        lo2.setLowpass (fs, f2);  hi2.setHighpass (fs, f2);
        ap.setAllpass (fs, f2);
        reset();
    }

    void reset() noexcept { lo1.reset(); hi1.reset(); lo2.reset(); hi2.reset(); ap.reset(); }

    inline void process (double x, double& low, double& mid, double& high) noexcept
    {
        low = ap.process (lo1.process (x));
        const double h = hi1.process (x);
        mid  = lo2.process (h);
        high = hi2.process (h);
    }
};

//------------------------------------------------------------------------------
struct Follower
{
    double a = 0.01, r = 0.001, v = 0.0;
    void set (double fs, double attMs, double relMs) noexcept
    {
        a = 1.0 - std::exp (-1.0 / (fs * attMs * 0.001));
        r = 1.0 - std::exp (-1.0 / (fs * relMs * 0.001));
    }
    inline double process (double x) noexcept
    {
        x = std::fabs (x);
        v += (x > v ? a : r) * (x - v);
        return v;
    }
    void reset() noexcept { v = 0.0; }
};

struct DelayLine
{
    std::vector<double> buf;
    int w = 0, size = 0;

    void init (int n) { size = n; buf.assign ((size_t) n, 0.0); w = 0; }
    void clear() noexcept { std::fill (buf.begin(), buf.end(), 0.0); w = 0; }

    inline void write (double x) noexcept { buf[(size_t) w] = x; if (++w >= size) w = 0; }

    // delay in samples (>= 1), linear interpolation
    inline double read (double d) const noexcept
    {
        d = clampd (d, 1.0, (double) (size - 2));
        double rp = (double) w - d;
        if (rp < 0.0) rp += (double) size;
        const int i0 = (int) rp;
        const int i1 = (i0 + 1 >= size) ? 0 : i0 + 1;
        const double fr = rp - (double) i0;
        return buf[(size_t) i0] * (1.0 - fr) + buf[(size_t) i1] * fr;
    }
};

//------------------------------------------------------------------------------
// Nonlinear building blocks (free functions so tests can inspect them)
//------------------------------------------------------------------------------

// Saturator used INSIDE the AFTERSHOCK feedback loop. Small-signal slope is
// ~1 and the global Lipschitz constant is verified < 1.05 by the test-suite,
// which (with loop gain <= 0.8 and an orthogonal mixing matrix) makes the loop
// a contraction: it can never run away or self-oscillate.
inline double loopSat (double u) noexcept
{
    const double t = std::tanh (u);
    return t + 0.15 * t * std::fabs (t);
}

// Final output protection: transparent below 0.8, smooth knee, ceiling at 1.0
inline double softCeiling (double x) noexcept
{
    const double ax = std::fabs (x);
    if (ax <= 0.8) return x;
    const double y = 0.8 + 0.2 * std::tanh ((ax - 0.8) / 0.2);
    return x < 0.0 ? -y : y;
}

//------------------------------------------------------------------------------
class Engine
{
public:
    // sampleRate = the rate process() will actually run at (e.g. 4x host rate)
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        smoothK  = 1.0 - std::exp (-1.0 / (fs * 0.025));            // 25 ms parameter smoothing
        kTr      = 1.0 - std::exp (-1.0 / (fs * 0.006));            // transient/sustain smoothing
        kDelay   = 1.0 - std::exp (-1.0 / (fs * 0.080));            // delay-time smoothing
        kInst    = 1.0 - std::exp (-(double) kCtl / (fs * 0.12));   // instability glide

        envFast.set (fs, 0.3, 25.0);
        envSlow.set (fs, 15.0, 400.0);
        envComp.set (fs, 0.2, 40.0);

        for (auto& s : split) s.prepare (fs, 180.0, 2200.0);

        dcR = std::exp (-2.0 * kPi * 10.0 / fs);
        hpK = 1.0 - std::exp (-2.0 * kPi * 90.0 / fs);

        const int maxLen = (int) std::ceil (0.026 * fs) + 8;
        for (auto& d : lines) d.init (maxLen);
        for (size_t k = 0; k < 4; ++k) baseSamples[k] = baseMs[k] * 0.001 * fs;

        reset();
    }

    void reset() noexcept
    {
        c = tC; f = tF; a = tA;
        envFast.reset(); envSlow.reset(); envComp.reset();
        transS = 0.0; susS = 0.0;
        for (auto& s : split) s.reset();
        for (auto& d : lines) d.clear();
        lpS.fill (0.0); hpS.fill (0.0);
        delayScale = 1.0;
        dcX = { 0.0, 0.0 }; dcY = { 0.0, 0.0 };
        rng = 0x9E3779B9u;
        inst = 0.0; instTarget = 0.0; instCount = 0;
        ctlCount = 0;
    }

    // all three 0..1
    void setParams (double collapse, double fracture, double aftershock) noexcept
    {
        tC = clampd (collapse, 0.0, 1.0);
        tF = clampd (fracture, 0.0, 1.0);
        tA = clampd (aftershock, 0.0, 1.0);
    }

    // r may be nullptr (mono): then only l is read and written
    void process (float* l, float* r, int n) noexcept
    {
        const bool stereo = (r != nullptr);

        for (int i = 0; i < n; ++i)
        {
            c += smoothK * (tC - c);
            f += smoothK * (tF - f);
            a += smoothK * (tA - a);

            double xl = (double) l[i];
            double xr = stereo ? (double) r[i] : xl;
            if (! (std::fabs (xl) < 1.0e6)) xl = 0.0;       // also catches NaN
            if (! (std::fabs (xr) < 1.0e6)) xr = 0.0;

            //------------------------------------------------------------ analysis
            const double m  = std::max (std::fabs (xl), std::fabs (xr));
            const double ef = envFast.process (m);
            const double es = envSlow.process (m);
            const double eps = 1.0e-6;
            transS += kTr * (clampd ((ef - 1.6 * es) / (ef + eps), 0.0, 1.0) - transS);
            susS   += kTr * (clampd (es / (ef + eps), 0.0, 1.0) - susS);
            const double gate = smoothstep (2.0e-4, 4.0e-3, ef);   // quiet material never excites the network

            if (ctlCount <= 0) { updateControl(); ctlCount = kCtl; }
            --ctlCount;

            //------------------------------------------------------------ COLLAPSE
            const double gDrive = driveGain * (1.0 + 0.7 * transS * c);   // transients collapse harder
            double gl = xl * gDrive, gr = xr * gDrive;
            const double ec  = envComp.process (std::max (std::fabs (gl), std::fabs (gr)));
            const double red = 1.0 / (1.0 + compK * ec);                  // progressive transient compression
            gl *= red; gr *= red;

            double yl = collapseShape (gl) * makeupC;
            double yr = collapseShape (gr) * makeupC;

            //------------------------------------------------------------ FRACTURE
            if (fe > 1.0e-4)
            {
                yl = fractureChannel (split[0], yl);
                yr = fractureChannel (split[1], yr);
            }
            else
            {
                // keep the crossover running (phase continuity), no nonlinearity
                double lo, mi, hi;
                split[0].process (yl, lo, mi, hi); yl = lo + mi + hi;
                split[1].process (yr, lo, mi, hi); yr = lo + mi + hi;
            }

            yl *= fracMakeup; yr *= fracMakeup;

            //------------------------------------------------------------ AFTERSHOCK
            double wetL = 0.0, wetR = 0.0;
            {
                const double inL = send * gate * yl;
                const double inR = send * gate * yr;

                delayScale += kDelay * (delayScaleTarget - delayScale);

                double v[4];
                for (int k = 0; k < 4; ++k)
                {
                    v[k] = lines[(size_t) k].read (baseSamples[(size_t) k] * delayScale);
                    lpS[(size_t) k] += dampK * (v[k] - lpS[(size_t) k]);          // damping (dark resonance)
                    hpS[(size_t) k] += hpK   * (lpS[(size_t) k] - hpS[(size_t) k]);
                    double u = lpS[(size_t) k] - hpS[(size_t) k];                 // high-pass: no LF/DC build-up
                    if (std::fabs (u) < 1.0e-18) u = 0.0;                         // flush tiny values
                    v[k] = loopSat (u);                                           // saturation inside the loop
                }

                const double sum = v[0] + v[1] + v[2] + v[3];                     // Householder mix (orthogonal)
                const double m0 = v[0] - 0.5 * sum, m1 = v[1] - 0.5 * sum,
                             m2 = v[2] - 0.5 * sum, m3 = v[3] - 0.5 * sum;

                lines[0].write (clampd (inL + gFb * m0, -6.0, 6.0));
                lines[1].write (clampd (inR + gFb * m1, -6.0, 6.0));
                lines[2].write (clampd (inL + gFb * m2, -6.0, 6.0));
                lines[3].write (clampd (inR + gFb * m3, -6.0, 6.0));

                wetL = 0.5 * (v[0] + v[2]);
                wetR = 0.5 * (v[1] + v[3]);
            }

            double ol = yl + wetL;
            double orr = yr + wetR;

            //------------------------------------------------------------ output stage
            ol  = dcBlock (0, ol);
            orr = dcBlock (1, orr);
            ol  = softCeiling (ol);
            orr = softCeiling (orr);

            if (! (std::fabs (ol) < 4.0 && std::fabs (orr) < 4.0))       // NaN / runaway guard
            {
                hardResetState();
                ol = orr = 0.0;
            }

            l[i] = (float) ol;
            if (stereo) r[i] = (float) orr;
        }
    }

    // exposed for the test-suite
    double getCollapse() const noexcept { return c; }

private:
    static constexpr int kCtl = 16;                      // control-rate divider (oversampled samples)

    // COLLAPSE makeup (dB) at collapse = 0, .2, .4, .6, .8, 1 (measured, see tests)
    static constexpr double kMakeupDb[6] = { 0.0, -5.2, -9.4, -11.8, -12.3, -12.1 };

    //-------------------------------------------------------------------------
    struct CollapseParams { double a = 1, b = 1, fm = 0, fa = 1.4, norm = 1; };

    inline double collapseShape (double x) const noexcept
    {
        // asymmetric soft clip: positive half hardens faster than the negative
        // half, so the curve produces even AND odd harmonics
        double y = (x >= 0.0) ? std::tanh (cp.a * x) / cp.a
                              : std::tanh (cp.b * x) / cp.b;

        if (cp.fm > 0.0)   // past ~45%: blend in a sine wavefolder (unity small-signal slope)
        {
            const double fold = std::sin (cp.fa * x) / cp.fa;
            y += (fold - y) * cp.fm;
        }
        return y * cp.norm;
    }

    inline double asymSat (double x) const noexcept
    {
        return x >= 0.0 ? std::tanh (1.6 * x) / 1.6 : std::tanh (x);
    }

    inline double fractureChannel (Split3& sp, double x) noexcept
    {
        double lo, mi, hi;
        sp.process (x, lo, mi, hi);

        // LOW: gentle asymmetric saturation, blended - the fundamental stays intact
        const double lo2 = lo + (asymSat (lo) - lo) * lowBlend;

        // MID: soft clip -> wavefold -> intermodulation with the low band (metallic edge)
        const double ma = std::tanh (midDrive * mi) / midDrive;
        const double mb = std::sin (midFold * ma) / midFold;
        double mc = ma + (mb - ma) * midFoldAmt;
        mc *= 1.0 + interMod * std::tanh (3.0 * lo);
        const double mi2 = mi + (mc - mi) * midBlend;

        // HIGH: harder clip + fold (crisp, aggressive)
        const double ha = std::tanh (highDrive * hi) / highDrive;
        const double hb = std::sin (highFold * ha) / highFold;
        const double hc = ha + (hb - ha) * highFoldAmt;
        const double hi2 = hi + (hc - hi) * highBlend;

        return lo2 + mi2 + hi2;
    }

    inline double dcBlock (int ch, double x) noexcept
    {
        const double y = x - dcX[(size_t) ch] + dcR * dcY[(size_t) ch];
        dcX[(size_t) ch] = x;
        dcY[(size_t) ch] = std::fabs (y) < 1.0e-20 ? 0.0 : y;
        return y;
    }

    inline double rand01() noexcept
    {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return (double) (rng >> 8) * (1.0 / 16777216.0);
    }

    // makeup gain (dB) after COLLAPSE, measured with the test-suite
    static double makeupDb (double cc) noexcept
    {
        const double p = clampd (cc, 0.0, 1.0) * 5.0;
        const int i = std::min (4, (int) p);
        const double fr = p - (double) i;
        return kMakeupDb[i] + (kMakeupDb[i + 1] - kMakeupDb[i]) * fr;
    }

    void updateControl() noexcept
    {
        // ---- COLLAPSE
        driveGain = dbToGain (26.0 * std::pow (c, 1.15));          // 0 .. +26 dB
        compK     = 1.4 * c;
        cp.a      = 1.0 + 2.4 * c;
        cp.b      = 1.0 + 0.6 * c;
        cp.fm     = 0.65 * smoothstep (0.45, 1.0, c);
        cp.fa     = 1.4 + 2.2 * c;
        cp.norm   = 2.0 / (1.0 / cp.a + 1.0 / cp.b);
        makeupC   = dbToGain (makeupDb (c));

        // ---- controlled instability: bounded random walk, deterministic RNG
        if ((instCount -= kCtl) <= 0)
        {
            instTarget = rand01() * 2.0 - 1.0;
            instCount  = (int) (fs * 0.03 * (0.7 + 0.6 * rand01()));
        }
        inst += kInst * (instTarget - inst);

        // ---- FRACTURE (signal-reactive: sustained material gets more of it)
        fe = clampd (f * (0.8 + 0.4 * susS), 0.0, 1.0);
        const double f2 = fe * fe;
        lowBlend     = 0.55 * fe;
        midDrive     = 1.0 + 2.0 * fe;
        midFold      = (2.0 + 4.0 * fe) * (1.0 + 0.2 * f2 * inst);
        midFoldAmt   = f2;
        midBlend     = std::min(1.0, 4.0 * fe);
        interMod     = 0.45 * fe * (1.0 + 0.3 * f2 * inst);
        highDrive    = 1.5 + 2.0 * fe;
        highFold     = 3.0 + 5.0 * fe;
        highFoldAmt  = f2;
        highBlend    = std::min (1.0, 3.0 * fe);
        fracMakeup   = dbToGain (3.0 * fe);               // FRACTURE costs ~3 dB at full (measured)

        // ---- AFTERSHOCK
        gFb  = clampd ((0.12 + 0.76 * a) * (0.85 + 0.15 * susS), 0.0, 0.9);
        send = 0.85 * std::pow (a, 1.4) * (0.8 + 0.6 * transS);
        delayScaleTarget = 1.0 - 0.40 * std::pow (a, 1.5);          // shorter lines = more metallic
        dampK = 1.0 - std::exp (-2.0 * kPi * (7000.0 * std::pow (0.31, a)) / fs);   // darker with depth
    }

    void hardResetState() noexcept
    {
        for (auto& s : split) s.reset();
        for (auto& d : lines) d.clear();
        lpS.fill (0.0); hpS.fill (0.0);
        dcX = { 0.0, 0.0 }; dcY = { 0.0, 0.0 };
        envFast.reset(); envSlow.reset(); envComp.reset();
    }

    //-------------------------------------------------------------------------
    double fs = 176400.0;
    double smoothK = 0.001, kTr = 0.001, kDelay = 0.001, kInst = 0.01;

    // targets / smoothed values
    double tC = 0.35, tF = 0.25, tA = 0.10;
    double c = 0.35, f = 0.25, a = 0.10;

    // analysis
    Follower envFast, envSlow, envComp;
    double transS = 0.0, susS = 0.0;

    // COLLAPSE
    CollapseParams cp;
    double driveGain = 1.0, compK = 0.0, makeupC = 1.0;

    // FRACTURE
    std::array<Split3, 2> split;
    double fe = 0.0, lowBlend = 0, midDrive = 1, midFold = 2, midFoldAmt = 0, midBlend = 0, interMod = 0,
           highDrive = 1.5, highFold = 3, highFoldAmt = 0, highBlend = 0;
    double fracMakeup = 1.0;
    double inst = 0.0, instTarget = 0.0;
    int instCount = 0;
    uint32_t rng = 0x9E3779B9u;

    // AFTERSHOCK: 4-line FDN
    std::array<DelayLine, 4> lines;
    std::array<double, 4> lpS { 0, 0, 0, 0 }, hpS { 0, 0, 0, 0 };
    static constexpr double baseMs[4] = { 5.9, 8.3, 12.7, 17.9 };
    std::array<double, 4> baseSamples { 0, 0, 0, 0 };
    double gFb = 0.0, send = 0.0, dampK = 0.5, hpK = 0.001;
    double delayScale = 1.0, delayScaleTarget = 1.0;

    // output
    std::array<double, 2> dcX { 0, 0 }, dcY { 0, 0 };
    double dcR = 0.9996;

    int ctlCount = 0;
};

} // namespace abyss
