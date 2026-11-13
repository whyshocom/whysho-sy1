#include "Effects.h"
#include "Tables.h"
#include "../Parameters.h"

namespace wsho::dsp
{
namespace
{
constexpr float kOnThreshold = 1.0e-4f;

inline double logCosh (double x) noexcept
{
    const double a = std::abs (x);
    return a + std::log1p (std::exp (-2.0 * a)) - 0.6931471805599453;     // overflow-safe
}

// Static waveshapers f and their antiderivatives F (for first-order ADAA,
// which removes most of the aliasing a hard nonlinearity would add).
constexpr double kTubeBias = 0.5;
const double kTubeTanhB = std::tanh (kTubeBias);
const double kTubeNorm  = 1.0 / (1.0 - kTubeTanhB * kTubeTanhB);

inline double shapeF (int type, double x) noexcept
{
    switch (type)
    {
        case dHard: return x < -1.0 ? -1.0 : (x > 1.0 ? 1.0 : x);
        case dTube: return (std::tanh (x + kTubeBias) - kTubeTanhB) * kTubeNorm;
        case dFold: return std::sin (1.5707963267948966 * x);
        default:    return std::tanh (x);
    }
}
inline double shapeAD (int type, double x) noexcept
{
    switch (type)
    {
        case dHard: { const double a = std::abs (x); return a <= 1.0 ? 0.5 * x * x : a - 0.5; }
        case dTube: return (logCosh (x + kTubeBias) - x * kTubeTanhB) * kTubeNorm;
        case dFold: return -0.6366197723675814 * std::cos (1.5707963267948966 * x);
        default:    return logCosh (x);
    }
}
// Constant-power dry / wet law. sin / cos (rather than sqrt) so a mix fading
// in from zero has a finite slope and cannot click.
inline float equalPowerDry (float m) noexcept { return sinHalfPi (1.0f - m); }
inline float equalPowerWet (float m) noexcept { return sinHalfPi (m); }
} // namespace

// ---------------------------------------------------------------------------
void DelayLine::resize (int minSamples)
{
    int size = 4;
    while (size < minSamples) size <<= 1;
    buf.assign ((size_t) size, 0.0f);
    mask = size - 1;
    w = 0;
}

float DelayLine::readLinear (float delay) const noexcept
{
    const int di = (int) delay;
    const float f = delay - (float) di;
    const float a = buf[(size_t) ((w - di) & mask)], b = buf[(size_t) ((w - di - 1) & mask)];
    return a + f * (b - a);
}

float DelayLine::readHermite (float delay) const noexcept
{
    const int di = (int) delay;
    const float f = delay - (float) di;
    const float ym1 = buf[(size_t) ((w - di + 1) & mask)];
    const float y0  = buf[(size_t) ((w - di) & mask)];
    const float y1  = buf[(size_t) ((w - di - 1) & mask)];
    const float y2  = buf[(size_t) ((w - di - 2) & mask)];
    const float c1 = 0.5f * (y1 - ym1);
    const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
    const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
    return ((c3 * f + c2) * f + c1) * f + y0;
}

// ---------------------------------------------------------------------------
void Drive::prepare (double sr)
{
    sampleRate = (float) sr;
    mixS.setTime (0.01f, sampleRate);
    gainS.setTime (0.02f, sampleRate);
    compS.setTime (0.02f, sampleRate);
    dcCoef = 1.0f - 2.0f * kPi * 10.0f / sampleRate;
    reset();
}

void Drive::reset() noexcept
{
    mixS.snap (0.0f);
    idle = true;
    lastType = -1;
    for (int c = 0; c < 2; ++c) { x1[c] = f1[c] = 0.0; dcX[c] = dcY[c] = held[c] = 0.0f; }
    crushPhase = 1.0f;
}

void Drive::process (float* l, float* r, int n, bool on, int type, float amt, float mix) noexcept
{
    if (! on && mixS.z < kOnThreshold) { if (! idle) reset(); return; }

    const float gTarget = type == dFold ? 1.0f + 7.0f * amt : (type == dCrush ? 1.0f : std::pow (10.0f, 1.5f * amt));
    // Loudness compensation: keep a nominal -12 dBFS peak at about the level it came in.
    float compTarget = 1.0f;
    if (type != dCrush)
    {
        // Match the level of a nominal -12 dBFS peak signal; a saturated wave has a
        // much lower crest factor than what went in, so back off further as it squares up.
        const float ref = 0.25f, x = gTarget * ref;
        if (type == dFold)
        {
            const float shaped = std::sin (0.5f * kPi * std::min (1.0f, x));
            compTarget = ref / shaped * (1.0f - 0.3f * clampf ((x - 0.6f) * 1.25f, 0.0f, 1.0f));
        }
        else
        {
            const float shaped = 0.5f * (float) (std::abs (shapeF (type, (double) x)) + std::abs (shapeF (type, (double) -x)));
            const float squash = 1.0f - shaped / x;      // 0 = linear, → 1 = fully clipped
            compTarget = ref / std::max (0.05f, shaped) * (1.0f - 0.45f * squash);
        }
    }
    if (idle) { gainS.snap (gTarget); compS.snap (compTarget); idle = false; }
    if (type != lastType)
    {
        for (int c = 0; c < 2; ++c) f1[c] = shapeAD (type, x1[c]);
        lastType = type;
    }

    const float mixTarget = on ? mix : 0.0f;
    float* ch[2] = { l, r };

    if (type == dCrush)
    {
        const float inc = amt < 0.001f ? 1.0f : std::min (1.0f, 48000.0f * std::exp2 (-5.5f * amt) / sampleRate);
        const float levels = std::exp2 (15.0f - 12.5f * amt);
        const float invLevels = 1.0f / levels;
        for (int i = 0; i < n; ++i)
        {
            const float m = mixS.next (mixTarget);
            crushPhase += inc;
            if (crushPhase >= 1.0f)
            {
                crushPhase -= 1.0f;
                for (int c = 0; c < 2; ++c)
                    held[c] = std::floor (clampf (ch[c][i], -4.0f, 4.0f) * levels + 0.5f) * invLevels;
            }
            for (int c = 0; c < 2; ++c) ch[c][i] += m * (held[c] - ch[c][i]);
        }
        return;
    }

    const bool tube = type == dTube;
    for (int i = 0; i < n; ++i)
    {
        const float m = mixS.next (mixTarget);
        const float g = gainS.next (gTarget);
        const float comp = compS.next (compTarget);
        for (int c = 0; c < 2; ++c)
        {
            const float dry = ch[c][i];
            const double x = (double) clampf (dry * g, -200.0f, 200.0f);
            const double fx = shapeAD (type, x);
            const double d = x - x1[c];
            double y = std::abs (d) > 1.0e-5 ? (fx - f1[c]) / d : shapeF (type, 0.5 * (x + x1[c]));
            x1[c] = x; f1[c] = fx;

            float wet = (float) y;
            if (tube)      // asymmetric shaping creates DC — block it
            {
                const float hp = wet - dcX[c] + dcCoef * dcY[c];
                dcX[c] = wet; dcY[c] = hp;
                wet = hp;
            }
            ch[c][i] = dry + m * (wet * comp - dry);
        }
    }
}

// ---------------------------------------------------------------------------
void Chorus::prepare (double sr)
{
    sampleRate = (float) sr;
    for (auto& d : line) d.resize ((int) (0.05 * sr) + 8);
    mixS.setTime (0.01f, sampleRate);
    depthS.setTime (0.05f, sampleRate);
    inS.setTime (0.004f, sampleRate);
    reset();
}

void Chorus::reset() noexcept
{
    for (auto& d : line) d.clear();
    mixS.snap (0.0f);
    idle = true;
}

void Chorus::process (float* l, float* r, int n, bool on, float rateHz, float depth, float mix) noexcept
{
    if (! on && mixS.z < kOnThreshold) { if (! idle) reset(); return; }
    // Feeding a delay line from mid-waveform puts a step into it that plays
    // back later: the input is faded as well as the mix.
    if (idle) { depthS.snap (depth); inS.snap (0.0f); idle = false; }

    const float mixTarget = on ? mix : 0.0f, inTarget = on ? 1.0f : 0.0f;
    const float inc = rateHz / sampleRate;
    const float base1 = 0.008f * sampleRate, base2 = 0.0135f * sampleRate;
    const float* sine = WaveTables::get().sine();

    for (int i = 0; i < n; ++i)
    {
        const float m = mixS.next (mixTarget);
        const float dep = depthS.next (depth) * 0.0055f * sampleRate;
        phase += inc; if (phase >= 1.0f) phase -= 1.0f;

        // two taps per side, quadrature between the channels for width
        const float a = WaveTables::read (sine, phase);
        const float b = WaveTables::read (sine, wrap01 (phase + 0.25f));
        const float inG = inS.next (inTarget);
        line[0].write (l[i] * inG);
        line[1].write (r[i] * inG);
        const float wl = 0.5f * (line[0].readHermite (base1 + dep * a) + line[0].readHermite (base2 - dep * b));
        const float wr = 0.5f * (line[1].readHermite (base1 + dep * b) + line[1].readHermite (base2 + dep * a));

        const float dg = equalPowerDry (m), wg = equalPowerWet (m);
        l[i] = l[i] * dg + wl * wg;
        r[i] = r[i] * dg + wr * wg;
    }
}

// ---------------------------------------------------------------------------
void Delay::prepare (double sr)
{
    sampleRate = (float) sr;
    for (auto& d : line) d.resize ((int) (2.0 * sr) + 16);
    mixS.setTime (0.01f, sampleRate);
    xfStep = 1.0f / (0.04f * sampleRate);   // 40 ms tap cross-fade
    inS.setTime (0.004f, sampleRate);
    fbS.setTime (0.02f, sampleRate);
    pingS.setTime (0.02f, sampleRate);
    lpCoef = 1.0f - std::exp (-2.0f * kPi * 7000.0f / sampleRate);
    hpCoef = 1.0f - std::exp (-2.0f * kPi * 120.0f / sampleRate);
    reset();
}

void Delay::reset() noexcept
{
    for (auto& d : line) d.clear();
    for (int c = 0; c < 2; ++c) lp[c] = hp[c] = 0.0f;
    mixS.snap (0.0f);
    idle = true;
    timeValid = false;
}

void Delay::process (float* l, float* r, int n, bool on, float seconds, float feedback, bool pingPong, float mix) noexcept
{
    if (! on && mixS.z < kOnThreshold) { if (! idle) reset(); return; }

    const float target = clampf (seconds * sampleRate, 2.0f, (float) (line[0].capacity() - 8));
    if (! timeValid)
    {
        dCur = dNext = target; xfActive = false;
        fbS.snap (feedback); pingS.snap (pingPong ? 1.0f : 0.0f); inS.snap (0.0f);
        timeValid = true;
    }
    idle = false;
    const float mixTarget = on ? mix : 0.0f, inTarget = on ? 1.0f : 0.0f;
    const float pingTarget = pingPong ? 1.0f : 0.0f;

    for (int i = 0; i < n; ++i)
    {
        const float m = mixS.next (mixTarget);
        const float fb = fbS.next (feedback);
        const float pg = pingS.next (pingTarget);

        // A new delay time is cross-faded in from a second tap: no pitch
        // sweep, no click — tempo / division changes stay musical.
        if (! xfActive && std::abs (target - dCur) > 0.5f) { dNext = target; xf = 0.0f; xfActive = true; }
        float tl = line[0].readHermite (dCur), tr = line[1].readHermite (dCur);
        if (xfActive)
        {
            xf += xfStep;
            const float x = std::min (1.0f, xf), w = x * x * (3.0f - 2.0f * x);
            tl += w * (line[0].readHermite (dNext) - tl);
            tr += w * (line[1].readHermite (dNext) - tr);
            if (xf >= 1.0f) { dCur = dNext; xfActive = false; }
        }

        // feedback path: gentle band-limiting so repeats darken and sub build-up is avoided
        float fl = tl, fr = tr;
        lp[0] += lpCoef * (fl - lp[0]); fl = lp[0];
        lp[1] += lpCoef * (fr - lp[1]); fr = lp[1];
        hp[0] += hpCoef * (fl - hp[0]); fl -= hp[0];
        hp[1] += hpCoef * (fr - hp[1]); fr -= hp[1];

        const float inG = inS.next (inTarget);
        const float inL = l[i] * inG, inR = r[i] * inG;
        const float mono = 0.5f * (inL + inR);
        // ping-pong: mono in → left line, left → right → left ...
        line[0].write (inL + pg * (mono - inL) + fb * (fl + pg * (fr - fl)));
        line[1].write (inR - pg * inR + fb * (fr + pg * (fl - fr)));

        const float dg = equalPowerDry (m), wg = equalPowerWet (m);
        l[i] = l[i] * dg + tl * wg;
        r[i] = r[i] * dg + tr * wg;
    }
}

// ---------------------------------------------------------------------------
void Reverb::prepare (double sr)
{
    sampleRate = (float) sr;
    const double scale = sr / 48000.0;
    static const int base[kLines] = { 1423, 1667, 1949, 2273, 2609, 2969, 3331, 3769 };
    static const int apBase[2][kAp] = { { 241, 367, 487 }, { 269, 397, 521 } };
    static const float rates[kLines] = { 0.31f, 0.47f, 0.23f, 0.59f, 0.37f, 0.53f, 0.29f, 0.43f };

    modDepth = (float) (6.0 * scale);
    for (int i = 0; i < kLines; ++i)
    {
        len[i] = (int) (base[i] * scale) | 1;
        line[i].resize (len[i] + (int) modDepth + 8);
        modPhase[i] = (float) i / (float) kLines;
        modInc[i] = rates[i] / sampleRate;
    }
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < kAp; ++i)
        {
            apLen[c][i] = (int) (apBase[c][i] * scale) | 1;
            ap[c][i].resize (apLen[c][i] + 4);
        }
    inHpCoef = 1.0f - std::exp (-2.0f * kPi * 110.0f / sampleRate);
    mixS.setTime (0.01f, sampleRate);
    inS.setTime (0.004f, sampleRate);
    lastSize = lastDamp = -1.0f;
    reset();
}

void Reverb::reset() noexcept
{
    for (auto& d : line) d.clear();
    for (auto& c : ap) for (auto& d : c) d.clear();
    for (auto& v : lpState) v = 0.0f;
    inHp[0] = inHp[1] = 0.0f;
    mixS.snap (0.0f);
    idle = true;
}

void Reverb::process (float* l, float* r, int n, bool on, float size, float damp, float mix) noexcept
{
    if (! on && mixS.z < kOnThreshold) { if (! idle) reset(); return; }
    if (idle) { inS.snap (0.0f); idle = false; }

    if (differs (size, lastSize))
    {
        lastSize = size;
        const float rt60 = 0.3f * std::pow (40.0f, size);        // 0.3 s … 12 s
        for (int i = 0; i < kLines; ++i)
            gain[i] = std::pow (10.0f, -3.0f * (float) len[i] / (sampleRate * rt60));
    }
    if (differs (damp, lastDamp))
    {
        lastDamp = damp;
        const float fc = std::min (0.45f * sampleRate, 18000.0f * std::pow (0.06f, damp));   // 18 k … 1.1 k
        dampCoef = 1.0f - std::exp (-2.0f * kPi * fc / sampleRate);
    }

    const float mixTarget = on ? mix : 0.0f, inTarget = on ? 1.0f : 0.0f;
    const float apG = 0.6f;

    for (int i = 0; i < n; ++i)
    {
        const float m = mixS.next (mixTarget);

        // input: low-cut (keeps 808 subs out of the tank) then allpass diffusion
        const float inG = inS.next (inTarget);
        float in[2] = { l[i] * inG, r[i] * inG };
        for (int c = 0; c < 2; ++c)
        {
            inHp[c] += inHpCoef * (in[c] - inHp[c]);
            float x = in[c] - inHp[c];
            for (int a = 0; a < kAp; ++a)
            {
                const float delayed = ap[c][a].readInt (apLen[c][a]);
                const float v = x + apG * delayed;
                ap[c][a].write (v);
                x = delayed - apG * v;
            }
            in[c] = x;
        }

        // 8-line FDN: slowly modulated reads → damping → Hadamard mix → write back
        float v[kLines];
        float outL = 0.0f, outR = 0.0f;
        for (int k = 0; k < kLines; ++k)
        {
            float ph = modPhase[k] + modInc[k]; if (ph >= 1.0f) ph -= 1.0f;
            modPhase[k] = ph;
            const float tri = 1.0f - std::abs (2.0f * ph - 1.0f);                 // 0..1
            const float x = line[k].readLinear ((float) len[k] + modDepth * tri);
            lpState[k] += dampCoef * (x - lpState[k]);
            v[k] = lpState[k] * gain[k];
            const float sign = (k & 2) ? -1.0f : 1.0f;
            if (k & 1) outR += sign * x; else outL += sign * x;
        }
        for (int h = 1; h < kLines; h <<= 1)
            for (int k = 0; k < kLines; k += h << 1)
                for (int j = k; j < k + h; ++j)
                {
                    const float a = v[j], b = v[j + h];
                    v[j] = a + b; v[j + h] = a - b;
                }
        const float norm = 0.35355339f;                                           // 1 / sqrt (8)
        for (int k = 0; k < kLines; ++k)
            line[k].write (v[k] * norm + in[k & 1]);

        const float dg = equalPowerDry (m), wg = equalPowerWet (m) * 0.4f;
        l[i] = l[i] * dg + outL * wg;
        r[i] = r[i] * dg + outR * wg;
    }
}

// ---------------------------------------------------------------------------
void Master::prepare (double sr)
{
    const float fs = (float) sr;
    gainS.setTime (0.02f, fs);
    dcCoef  = 1.0f - 2.0f * kPi * 5.0f / fs;
    attCoef = 1.0f - std::exp (-1.0f / (0.0001f * fs));      // 0.1 ms
    relCoef = 1.0f - std::exp (-1.0f / (0.12f * fs));        // 120 ms
    reset();
}

void Master::reset() noexcept
{
    for (int c = 0; c < 2; ++c) dcX[c] = dcY[c] = 0.0f;
    env = 1.0f;
    gainValid = false;
}

void Master::process (float* l, float* r, int n, float gainDb) noexcept
{
    constexpr float thr = 0.891f;      // -1 dBFS: below this the stage is bit-transparent (bar DC blocking)
    const float gTarget = std::pow (10.0f, gainDb * 0.05f);
    if (! gainValid) { gainS.snap (gTarget); gainValid = true; }
    float* ch[2] = { l, r };

    for (int i = 0; i < n; ++i)
    {
        const float g = gainS.next (gTarget);
        float x[2];
        for (int c = 0; c < 2; ++c)
        {
            const float in = ch[c][i];
            const float hp = in - dcX[c] + dcCoef * dcY[c];
            dcX[c] = in; dcY[c] = hp;
            x[c] = hp * g;
        }

        // stereo-linked limiter: fast attack, slow release ...
        const float pk = std::max (std::abs (x[0]), std::abs (x[1]));
        const float want = pk > thr ? thr / pk : 1.0f;
        env += (want < env ? attCoef : relCoef) * (want - env);

        // ... and a soft clip for whatever overshoots during the attack
        for (int c = 0; c < 2; ++c)
        {
            float y = x[c] * env;
            const float a = std::abs (y);
            if (a > thr)
                y = std::copysign (thr + (1.0f - thr) * fastTanh ((a - thr) / (1.0f - thr)), y);
            ch[c][i] = y;
        }
    }

    // Let the DC blocker settle to exact digital silence instead of decaying forever.
    for (int c = 0; c < 2; ++c)
        if (std::abs (dcX[c]) < 1.0e-9f && std::abs (dcY[c]) < 1.0e-9f) dcX[c] = dcY[c] = 0.0f;
}
} // namespace wsho::dsp
