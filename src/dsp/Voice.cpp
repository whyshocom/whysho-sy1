#include "Voice.h"
#include "Diagnostics.h"

namespace wsho::dsp
{
namespace
{
// One default note (saw, level 0.8, velocity 100, vel sens 0.5) peaks ≈ -12 dBFS.
constexpr float kVoiceGain = 0.45f;
constexpr float kStage2K   = 1.0f / 0.5412f;      // 2nd section of a 4-pole Butterworth
constexpr float kMinK      = 0.025f;              // Q = 40 at resonance 1.0: rings, never self-destructs
constexpr float kFmMaxCycles = 1.0f;              // osc_fm = 1 → phase-mod index 2*pi

// JP-style 7-saw detune spread and a fixed stereo placement (L/R weights have
// unit mean power so each channel is as loud as the mono sum would be).
constexpr float kSsOffsets[7] = { -0.11002313f, -0.06288439f, -0.01952356f, 0.0f, 0.01991221f, 0.06216538f, 0.10745242f };
constexpr float kSsL[7] = { 1.30f, 0.55f, 1.30f, 1.0f, 0.55f, 1.30f, 0.55f };
constexpr float kSsR[7] = { 0.55f, 1.30f, 0.55f, 1.0f, 1.30f, 0.55f, 1.30f };
constexpr float kSsNorm = 0.37796447f;            // 1 / sqrt (7)

constexpr float kInvSqrt[kMaxUnison + 1] = { 1.0f, 1.0f, 0.70710678f, 0.57735027f, 0.5f, 0.4472136f, 0.40824829f, 0.37796447f, 0.35355339f };

const float kZeros[kCtrl] = {};

// Unison voice u of U: detune position in [-1, 1] ...
inline float uniPos (int u, int U) noexcept { return U > 1 ? (2.0f * (float) u / (float) (U - 1) - 1.0f) : 0.0f; }
// ... and a pan slot that alternates sides so pitch and pan are not correlated.
inline float uniPanPos (int u, int U) noexcept { return uniPos ((u & 1) ? U - 1 - (u - 1) / 2 : u / 2, U); }

inline float softLimit (float y) noexcept
{
    // Transparent below ±2 (≈ +12 dB over a nominal voice), then eases towards ±4:
    // keeps a screaming resonance musical instead of slamming the master limiter.
    const float a = std::abs (y);
    if (a <= 2.0f) return y;
    const float e = (a - 2.0f) * 0.5f;
    return std::copysign (2.0f + 2.0f * e / (1.0f + e), y);
}

template <int Type>
inline void runFilter (float* l, float* r, int n, float& g, float gStep, float& k, float kStep,
                       float& in, float inStep, float ic1[2][2], float ic2[2][2]) noexcept
{
    const bool two = (Type == fLP24 || Type == fHP24);
    float* x[2] = { l, r };

    for (int i = 0; i < n; ++i)
    {
        g += gStep; k += kStep; in += inStep;
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float a2 = g * a1, a3 = g * a2;
        float b1 = 0.0f, b2 = 0.0f, b3 = 0.0f;
        if (two) { b1 = 1.0f / (1.0f + g * (g + kStage2K)); b2 = g * b1; b3 = g * b2; }
        const float bpGain = Type == fBP12 ? 1.19f * std::sqrt (k) : 0.0f;

        for (int c = 0; c < 2; ++c)
        {
            const float v0 = x[c][i] * in;
            const float v3 = v0 - ic2[0][c];
            const float v1 = a1 * ic1[0][c] + a2 * v3;
            const float v2 = ic2[0][c] + a2 * ic1[0][c] + a3 * v3;
            ic1[0][c] = 2.0f * v1 - ic1[0][c];
            ic2[0][c] = 2.0f * v2 - ic2[0][c];

            float y;
            if (Type == fLP24 || Type == fLP12)       y = v2;
            else if (Type == fBP12)                   y = v1 * bpGain;
            else if (Type == fHP12 || Type == fHP24)  y = v0 - k * v1 - v2;
            else                                      y = v0 - k * v1;      // notch

            if (two)
            {
                const float w3 = y - ic2[1][c];
                const float w1 = b1 * ic1[1][c] + b2 * w3;
                const float w2 = ic2[1][c] + b2 * ic1[1][c] + b3 * w3;
                ic1[1][c] = 2.0f * w1 - ic1[1][c];
                ic2[1][c] = 2.0f * w2 - ic2[1][c];
                y = (Type == fLP24) ? w2 : (y - kStage2K * w1 - w2);
            }
            x[c][i] = softLimit (y);
        }
    }
}
} // namespace

EnvCoefs makeEnvCoefs (float a, float d, float s, float r, float stepsPerSecond) noexcept
{
    auto coef = [stepsPerSecond] (float seconds, float logRatio)
    {
        const float steps = seconds * stepsPerSecond;
        return steps > 0.25f ? std::exp (-logRatio / steps) : 0.0f;
    };
    EnvCoefs c;
    c.aCoef = coef (a, std::log (Env::kAtkTarget / (Env::kAtkTarget - 1.0f)));
    c.dCoef = coef (d, 6.9078f);     // -60 dB
    c.rCoef = coef (r, 6.9078f);
    c.sustain = s;
    return c;
}

void Voice::prepare() noexcept { kill(); }

void Voice::resetFilter() noexcept
{
    for (auto& a : ic1) a[0] = a[1] = 0.0f;
    for (auto& a : ic2) a[0] = a[1] = 0.0f;
}

void Voice::kill() noexcept
{
    active = fading = keyDown = sustained = false;
    ampEnv.kill();
    fltEnv.kill();
    note = -1;
}

void Voice::setGlide (float target, float seconds, const Shared& s) noexcept
{
    targetPitch = target;
    const float dist = std::abs (target - pitch);
    if (seconds < 0.001f || dist < 1.0e-4f) { pitch = target; glideStep = 0.0f; }
    else glideStep = dist * s.ctrlDt / seconds;      // constant-time portamento, linear in pitch
}

void Voice::start (int n, float velocity01, float fromPitch, float glideSeconds,
                   const Shared& s, uint32_t newSeed, uint64_t newAge) noexcept
{
    active = true; fading = false; keyDown = true; sustained = false;
    note = n; velocity = velocity01; age = newAge;
    seed = newSeed; rng.seed (hash32 (newSeed));
    randVal = hashBipolar (newSeed ^ 0xA511E9B3u);

    pitch = fromPitch >= 0.0f ? fromPitch : (float) n;
    setGlide ((float) n, glideSeconds, s);

    ampEnv.kill(); ampEnv.trigger();
    fltEnv.kill(); fltEnv.trigger();
    pitchEnv = 1.0f;

    for (int l = 0; l < kNumLfos; ++l)
    {
        lfoFree[l]  = ! s.lfoRetrig[l];
        lfoPhase[l] = lfoFree[l] ? s.lfoGlobalPhase[l] : 0.0f;
        lfoCycle[l] = lfoFree[l] ? s.lfoGlobalCycle[l] : 0u;
        lfoVal[l] = lfoRateMod[l] = 0.0f;
    }

    // Deterministic start phase for the first unison voice (consistent 808 /
    // pluck transients); the rest, and every supersaw partial, start random.
    for (int o = 0; o < kNumOscs; ++o)
    {
        for (int u = 0; u < kMaxUnison; ++u)
        {
            phase[o][u] = u == 0 ? 0.0f : rng.next01();
            for (auto& p : ssPhase[o][u]) p = rng.next01();
            gainL[o][u] = {}; gainR[o][u] = {};
        }
        noiseLp[o] = 0.0f;
        oscRun[o] = false;
        oscUni[o] = 0;
    }
    subPhase = 0.0f;
    subGain = {}; noiseGain = {};
    resetFilter();

    firstTick = true;
    ctrlCountdown = 0;
    fadeGain = 1.0f; fadeStep = 0.0f;
}

void Voice::retrigger (int n, float velocity01, float glideSeconds, const Shared& s, uint64_t newAge) noexcept
{
    note = n; velocity = velocity01; age = newAge;
    keyDown = true; sustained = false;
    setGlide ((float) n, glideSeconds, s);
    ampEnv.trigger();
    fltEnv.trigger();
    pitchEnv = 1.0f;
    for (int l = 0; l < kNumLfos; ++l)
        if (! lfoFree[l]) { lfoPhase[l] = 0.0f; lfoCycle[l] = 0u; }
}

void Voice::glideTo (int n, float glideSeconds, const Shared& s) noexcept
{
    note = n;
    keyDown = true; sustained = false;
    setGlide ((float) n, glideSeconds, s);
}

void Voice::noteOff() noexcept
{
    keyDown = sustained = false;
    ampEnv.release_();
    fltEnv.release_();
}

void Voice::steal (float sampleRate) noexcept
{
    if (fading) return;
    fading = true;
    keyDown = sustained = false;
    fadeStep = 1.0f / std::max (8.0f, 0.004f * sampleRate);      // 4 ms
}

// ---------------------------------------------------------------------------
void Voice::control (const Shared& s) noexcept
{
    const bool snap = firstTick;
    const auto& wt = WaveTables::get();

    // ---- LFOs (rate modulation uses last tick's matrix output)
    for (int l = 0; l < kNumLfos; ++l)
    {
        if (! snap)
        {
            float rate = s.lfoRate[l];
            if (nonZero (lfoRateMod[l])) rate *= std::exp2 (3.0f * lfoRateMod[l]);     // ±1 → ×/÷ 8
            float ph = lfoPhase[l] + std::min (rate * s.ctrlDt, 0.5f);
            if (ph >= 1.0f) { ph -= 1.0f; ++lfoCycle[l]; }
            lfoPhase[l] = ph;
        }
        const float ph = lfoPhase[l];
        float v;
        switch (s.lfoWave[l])
        {
            default:
            case lSine:       v = WaveTables::read (wt.sine(), ph); break;
            case lTriangle:   v = ph < 0.25f ? 4.0f * ph : (ph < 0.75f ? 2.0f - 4.0f * ph : 4.0f * ph - 4.0f); break;
            case lSaw:        v = 1.0f - 2.0f * ph; break;
            case lSquare:     v = ph < 0.5f ? 1.0f : -1.0f; break;
            case lSampleHold: v = hashBipolar (lfoCycle[l] * 0x9E3779B1u + (lfoFree[l] ? 0x51EDu + (uint32_t) l : seed + (uint32_t) l)); break;
        }
        lfoVal[l] = v;
    }

    // ---- envelopes
    const float fe = fltEnv.next (s.fltEnvC);
    const float ae = ampEnv.level;
    if (! snap) pitchEnv *= s.pitchCoef;

    if (differs (pitch, targetPitch))
    {
        if (pitch < targetPitch) pitch = std::min (targetPitch, pitch + glideStep);
        else                     pitch = std::max (targetPitch, pitch - glideStep);
        if (glideStep <= 0.0f) pitch = targetPitch;
    }

    // ---- mod matrix
    float md[mdFm + 1] = {};
    for (int m = 0; m < kNumModSlots; ++m)
    {
        const int src = s.modSrc[m], dst = s.modDst[m];
        if (src == msOff || dst == mdOff) continue;
        float v = 0.0f;
        switch (src)
        {
            case msLfo1:     v = lfoVal[0]; break;
            case msLfo2:     v = lfoVal[1]; break;
            case msFltEnv:   v = fe; break;
            case msAmpEnv:   v = ae; break;
            case msVelocity: v = velocity; break;
            case msModWheel: v = s.modWheel; break;
            case msKeyTrack: v = clampf ((pitch - 60.0f) * (1.0f / 60.0f), -1.0f, 1.0f); break;
            case msRandom:   v = randVal; break;
            default: break;
        }
        float amt = s.modAmt[m];
        if (dst >= mdPitch && dst <= mdOsc3Pitch) amt *= std::abs (amt);     // fine control near zero (vibrato)
        md[dst] += amt * v;
    }
    lfoRateMod[0] = clampf (md[mdLfo1Rate], -1.0f, 1.0f);
    lfoRateMod[1] = clampf (md[mdLfo2Rate], -1.0f, 1.0f);

    // ---- pitch
    const float base = pitch + s.bend + s.pitchAmt * pitchEnv + 24.0f * md[mdPitch];
    auto incFor = [&s] (float semis)
    {
        return clampf (440.0f * std::exp2 ((semis - 69.0f) * (1.0f / 12.0f)) * s.invSr, 0.0f, 0.49f);
    };

    const int uniN = s.unison;
    float uniRatio[kMaxUnison];
    for (int u = 0; u < kMaxUnison; ++u)
    {
        const float x = s.uniDetune * 50.0f * uniPos (u, uniN) * (0.69314718f / 1200.0f);
        uniRatio[u] = u < uniN ? 1.0f + x + 0.5f * x * x : 1.0f;
    }
    const float maxUniRatio = uniN > 1 ? uniRatio[uniN - 1] : 1.0f;

    fmDepth = sq (clampf (s.fm + md[mdFm], 0.0f, 1.0f)) * kFmMaxCycles;
    float oscDt[kNumOscs] = {};

    for (int oi = 0; oi < kNumOscs; ++oi)
    {
        const int o = oi == 0 ? 1 : (oi == 1 ? 0 : 2);      // osc 2 first: osc 1's table choice needs its rate
        const int wave = waveL[o] = s.oscWave[o];
        const int U = wave == wNoise ? 1 : uniN;
        const float level = s.oscOn[o] ? s.oscLevel[o] : 0.0f;
        const bool wasRunning = oscRun[o];

        // Keep rendering for one more tick after switching off / shrinking
        // unison so the gains ramp to zero instead of cutting.
        bool fadingOut = false;
        if (wasRunning)
            for (int u = 0; u < oscUni[o] && ! fadingOut; ++u)
                fadingOut = nonZero (gainL[o][u].tgt) || nonZero (gainR[o][u].tgt);

        if (level <= 0.0f && ! fadingOut && ! (o == 1 && s.oscOn[1] && fmDepth > 0.0f))
        {
            for (int u = 0; u < kMaxUnison; ++u) { gainL[o][u] = {}; gainR[o][u] = {}; }
            oscRun[o] = false; oscUni[o] = 0;
            continue;
        }

        const int renderU = wasRunning ? std::max (U, oscUni[o]) : U;
        oscRun[o] = true;

        const float dt = incFor (base + s.oscTune[o] + 24.0f * md[mdOsc1Pitch + o]);
        oscDt[o] = dt;
        for (int u = 0; u < kMaxUnison; ++u) inc[o][u] = std::min (0.49f, dt * uniRatio[u]);

        const float sh = clampf (s.oscShape[o] + md[mdShape], 0.0f, 1.0f);
        shape[o] = sh;

        float topDt = dt * maxUniRatio;
        if (wave == wSuperSaw)
        {
            const float d = 0.08f + 0.92f * sh * sh;
            for (int j = 0; j < 7; ++j) ssRatio[o][j] = 1.0f + kSsOffsets[j] * d;
            topDt *= ssRatio[o][0] > ssRatio[o][6] ? ssRatio[o][0] : ssRatio[o][6];
        }
        else if (wave == wNoise)
        {
            // shape = colour: one-pole low-pass from open down to ~150 Hz
            shape[o] = sh <= 0.0f ? 1.0f : 1.0f - std::exp (-2.0f * kPi * 20000.0f * std::exp2 (-7.0f * sh) * s.invSr);
        }
        if (o == 0 && fmDepth > 0.0f && oscRun[1])
            topDt += fmDepth * 2.0f * kPi * oscDt[1];      // peak phase velocity added by the modulator

        const int ti = WaveTables::indexFor (topDt);
        table[o] = wave == wTriangle ? wt.tri (ti) : (wave == wSine ? wt.sine() : wt.saw (ti));

        const float ug = kInvSqrt[U];
        const float panBase = s.oscPan[o] + md[mdPan];
        for (int u = 0; u < kMaxUnison; ++u)
        {
            float gl = 0.0f, gr = 0.0f;
            if (u < U)
            {
                const float x = 0.5f * (clampf (panBase + s.uniSpread * uniPanPos (u, U), -1.0f, 1.0f) + 1.0f);
                gl = level * ug * sinHalfPi (1.0f - x);
                gr = level * ug * sinHalfPi (x);
            }
            const bool snapThis = snap || ! wasRunning;
            if (snapThis && ! snap) { gainL[o][u] = {}; gainR[o][u] = {}; }      // fade in from silence
            gainL[o][u].set (gl, snap);
            gainR[o][u].set (gr, snap);
        }
        oscUni[o] = renderU;
    }

    // ---- sub + noise
    subInc = incFor (base + s.subTune);
    subWaveL = s.subWave;
    subTable = subWaveL == swTriangle ? wt.tri (WaveTables::indexFor (subInc))
             : (subWaveL == swSine ? wt.sine() : wt.saw (WaveTables::indexFor (subInc)));
    subGain.set (s.subLevel * 0.70710678f, snap);
    noiseGain.set (s.noiseLevel * 0.70710678f, snap);

    // ---- filter
    fltType = s.fltType;
    const float l2 = s.cutoffLog2 + 10.0f * s.fltEnv * fe + s.fltKey * (pitch - 60.0f) * (1.0f / 12.0f) + 5.0f * md[mdCutoff];
    const float fc = std::min (std::exp2 (clampf (l2, 4.0f, 14.5f)), 0.45f * s.sr);
    const float kMax = (fltType == fLP24 || fltType == fHP24) ? 0.7654f : 1.4142f;
    const float r = clampf (s.reso + md[mdReso], 0.0f, 1.0f);
    const float kRel = std::exp2 (r * std::log2 (kMinK / kMax));       // 1 → kMinK / kMax
    fltG.set (std::tan (kPi * fc * s.invSr), snap);
    fltK.set (kMax * kRel, snap);
    fltIn.set (std::sqrt (std::sqrt (kRel)), snap);                     // tame level as resonance rises

    const float drv = s.fltDrive;
    driveWet = std::min (1.0f, drv * 10.0f);
    driveGain = 1.0f + 7.0f * drv;
    driveNorm = 0.6f / fastTanh (0.6f * driveGain);

    // ---- amp
    const float velGain = 1.0f - s.velSens * (1.0f - velocity * velocity);
    ampGain.set (kVoiceGain * velGain * clampf (1.0f + md[mdLevel], 0.0f, 2.0f), snap);
}

// ---------------------------------------------------------------------------
void Voice::renderChunk (float* outL, float* outR, int n, const Shared& s) noexcept
{
    float mixL[kCtrl] = {}, mixR[kCtrl] = {};
    float raw[kCtrl], rawR[kCtrl];
    float fmBuf[kMaxUnison][kCtrl];
    const bool fmOn = fmDepth > 0.0f && oscRun[1] && oscRun[0];
    const int fmU = oscUni[1];

    for (int oi = 0; oi < kNumOscs; ++oi)
    {
        const int o = oi == 0 ? 1 : (oi == 1 ? 0 : 2);
        if (! oscRun[o]) continue;
        const int wave = waveL[o];      // latched at the control tick, with its table / coefficients
        const float* t = table[o];
        const float sh = shape[o];

        for (int u = 0; u < oscUni[o]; ++u)
        {
            const float* pm = (o == 0 && fmOn && wave != wNoise && wave != wSuperSaw)
                                ? fmBuf[u < fmU ? u : fmU - 1] : kZeros;
            const float d = inc[o][u];
            float p = phase[o][u];
            bool stereo = false;

            switch (wave)
            {
                case wSquare:
                {
                    const float off = 0.5f + 0.45f * sh;        // 1 - pulse width
                    for (int i = 0; i < n; ++i)
                    {
                        const float q = wrap01 (p + pm[i]);
                        float q2 = q + off; if (q2 >= 1.0f) q2 -= 1.0f;
                        raw[i] = WaveTables::read (t, q) - WaveTables::read (t, q2);   // two saws → zero-mean pulse
                        p += d; if (p >= 1.0f) p -= 1.0f;
                    }
                    break;
                }
                case wSuperSaw:
                {
                    for (int i = 0; i < n; ++i) raw[i] = rawR[i] = 0.0f;
                    float* ph = ssPhase[o][u];
                    for (int j = 0; j < 7; ++j)
                    {
                        const float dj = std::min (0.49f, d * ssRatio[o][j]);
                        const float wl = kSsL[j] * kSsNorm, wr = kSsR[j] * kSsNorm;
                        float pj = ph[j];
                        for (int i = 0; i < n; ++i)
                        {
                            const float v = WaveTables::read (t, pj);
                            raw[i] += wl * v; rawR[i] += wr * v;
                            pj += dj; if (pj >= 1.0f) pj -= 1.0f;
                        }
                        ph[j] = pj;
                    }
                    stereo = true;
                    break;
                }
                case wNoise:
                {
                    const float c = sh;                         // low-pass coefficient (1 = white)
                    const float norm = std::sqrt ((2.0f - c) / c);
                    float lp = noiseLp[o];
                    for (int i = 0; i < n; ++i)
                    {
                        lp += c * (rng.nextBipolar() - lp);
                        raw[i] = lp * norm;
                    }
                    noiseLp[o] = lp;
                    break;
                }
                default:      // saw / triangle / sine (+ wavefold)
                {
                    for (int i = 0; i < n; ++i)
                    {
                        raw[i] = WaveTables::read (t, wrap01 (p + pm[i]));
                        p += d; if (p >= 1.0f) p -= 1.0f;
                    }
                    if (sh > 0.001f)
                    {
                        const float g = 1.0f + 4.0f * sh;
                        const float c = 0.5f * std::min (1.0f, sh * 8.0f);     // rounds the fold corners
                        for (int i = 0; i < n; ++i)
                        {
                            const float v = foldTri (raw[i] * g);
                            raw[i] = v + c * (v - v * v * v);
                        }
                    }
                    break;
                }
            }
            phase[o][u] = p;

            if (o == 1 && fmOn)
                for (int i = 0; i < n; ++i)
                    fmBuf[u][i] = fmDepth * (stereo ? 0.5f * (raw[i] + rawR[i]) : raw[i]);

            auto& gl = gainL[o][u];
            auto& gr = gainR[o][u];
            if (! (nonZero (gl.cur) || nonZero (gl.step) || nonZero (gr.cur) || nonZero (gr.step))) continue;
            const float* srcR = stereo ? rawR : raw;
            float a = gl.cur, b = gr.cur;
            for (int i = 0; i < n; ++i)
            {
                a += gl.step; b += gr.step;
                mixL[i] += a * raw[i];
                mixR[i] += b * srcR[i];
            }
            gl.cur = a; gr.cur = b;
        }
    }

    // ---- sub oscillator (mono, centre)
    if (nonZero (subGain.cur) || nonZero (subGain.step))
    {
        float p = subPhase, a = subGain.cur;
        const bool square = subWaveL == swSquare;
        for (int i = 0; i < n; ++i)
        {
            float v = WaveTables::read (subTable, p);
            if (square) { float q = p + 0.5f; if (q >= 1.0f) q -= 1.0f; v -= WaveTables::read (subTable, q); }
            a += subGain.step;
            mixL[i] += a * v; mixR[i] += a * v;
            p += subInc; if (p >= 1.0f) p -= 1.0f;
        }
        subPhase = p; subGain.cur = a;
    }
    else
    {
        subPhase = wrap01 (subPhase + subInc * (float) n);      // stay phase-continuous if faded in later
    }

    if (nonZero (noiseGain.cur) || nonZero (noiseGain.step))
    {
        float a = noiseGain.cur;
        for (int i = 0; i < n; ++i)
        {
            a += noiseGain.step;
            const float v = a * rng.nextBipolar();
            mixL[i] += v; mixR[i] += v;
        }
        noiseGain.cur = a;
    }

    // ---- pre-filter drive
    if (driveWet > 0.0f)
    {
        for (int i = 0; i < n; ++i)
        {
            mixL[i] += driveWet * (fastTanh (mixL[i] * driveGain) * driveNorm - mixL[i]);
            mixR[i] += driveWet * (fastTanh (mixR[i] * driveGain) * driveNorm - mixR[i]);
        }
    }

    // ---- filter
    switch (fltType)
    {
        case fLP24:  runFilter<fLP24>  (mixL, mixR, n, fltG.cur, fltG.step, fltK.cur, fltK.step, fltIn.cur, fltIn.step, ic1, ic2); break;
        case fLP12:  runFilter<fLP12>  (mixL, mixR, n, fltG.cur, fltG.step, fltK.cur, fltK.step, fltIn.cur, fltIn.step, ic1, ic2); break;
        case fBP12:  runFilter<fBP12>  (mixL, mixR, n, fltG.cur, fltG.step, fltK.cur, fltK.step, fltIn.cur, fltIn.step, ic1, ic2); break;
        case fHP12:  runFilter<fHP12>  (mixL, mixR, n, fltG.cur, fltG.step, fltK.cur, fltK.step, fltIn.cur, fltIn.step, ic1, ic2); break;
        case fHP24:  runFilter<fHP24>  (mixL, mixR, n, fltG.cur, fltG.step, fltK.cur, fltK.step, fltIn.cur, fltIn.step, ic1, ic2); break;
        default:     runFilter<fNotch> (mixL, mixR, n, fltG.cur, fltG.step, fltK.cur, fltK.step, fltIn.cur, fltIn.step, ic1, ic2); break;
    }

    // A filter can only go non-finite if it was fed garbage; recover silently.
    const float probe = std::abs (ic1[0][0]) + std::abs (ic1[0][1]) + std::abs (ic2[0][0]) + std::abs (ic2[0][1])
                      + std::abs (ic1[1][0]) + std::abs (ic1[1][1]) + std::abs (ic2[1][0]) + std::abs (ic2[1][1]);
    if (! (probe < 1.0e6f))
    {
        guardTrips().fetch_add (1, std::memory_order_relaxed);
        resetFilter();
        for (int i = 0; i < n; ++i) mixL[i] = mixR[i] = 0.0f;
    }

    // ---- amp
    float a = ampGain.cur;
    for (int i = 0; i < n; ++i)
    {
        a += ampGain.step;
        float e = ampEnv.next (s.ampEnv) * a;
        if (fading)
        {
            fadeGain -= fadeStep;
            if (fadeGain <= 0.0f) { fadeGain = 0.0f; ampEnv.kill(); }
            e *= fadeGain;
        }
        outL[i] += mixL[i] * e;
        outR[i] += mixR[i] * e;
    }
    ampGain.cur = a;

    // Free the voice once silent — including a held note that decayed to a zero sustain.
    if (ampEnv.stage == Env::idle
        || (ampEnv.stage == Env::decay && s.ampEnv.sustain <= 0.0f && ampEnv.level < 1.0e-4f))
        kill();
}

void Voice::render (float* outL, float* outR, int n, const Shared& s) noexcept
{
    while (n > 0 && active)
    {
        if (ctrlCountdown <= 0)
        {
            control (s);
            firstTick = false;
            ctrlCountdown = kCtrl;
        }
        const int m = std::min (n, ctrlCountdown);
        renderChunk (outL, outR, m, s);
        outL += m; outR += m; n -= m; ctrlCountdown -= m;
    }
}
} // namespace wsho::dsp
