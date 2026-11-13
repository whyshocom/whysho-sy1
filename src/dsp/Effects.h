#pragma once
#include "DspUtil.h"
#include <vector>

// Post-voice effects. All state is allocated in prepare(); process() is
// allocation-free. Every effect cross-fades in / out when toggled (≈10 ms) and
// stops costing CPU once fully faded out.
namespace wsho::dsp
{
struct OnePole     // per-sample parameter smoother
{
    float z = 0.0f, c = 1.0f;
    void setTime (float seconds, float sr) noexcept { c = 1.0f - std::exp (-1.0f / std::max (1.0f, seconds * sr)); }
    float next (float target) noexcept { z += c * (target - z); return z; }
    void snap (float v) noexcept { z = v; }
};

class DelayLine
{
public:
    void resize (int minSamples);                 // rounds up to a power of two
    void clear() noexcept                         { std::fill (buf.begin(), buf.end(), 0.0f); }
    void write (float v) noexcept                 { buf[(size_t) w] = v; w = (w + 1) & mask; }
    float readInt (int delay) const noexcept      { return buf[(size_t) ((w - delay) & mask)]; }   // delay >= 1
    float readLinear (float delay) const noexcept;
    float readHermite (float delay) const noexcept;
    int   capacity() const noexcept               { return mask + 1; }
private:
    std::vector<float> buf { 0.0f, 0.0f };
    int mask = 1, w = 0;
};

class Drive
{
public:
    void prepare (double sr);
    void reset() noexcept;
    void process (float* l, float* r, int n, bool on, int type, float amt, float mix) noexcept;
private:
    float  sampleRate = 48000.0f;
    OnePole mixS, gainS, compS;
    bool   idle = true;
    int    lastType = -1;
    double x1[2] {}, f1[2] {};
    float  dcX[2] {}, dcY[2] {}, dcCoef = 0.999f;
    float  crushPhase = 1.0f, held[2] {};
};

class Chorus
{
public:
    void prepare (double sr);
    void reset() noexcept;
    void process (float* l, float* r, int n, bool on, float rateHz, float depth, float mix) noexcept;
private:
    float sampleRate = 48000.0f, phase = 0.0f;
    OnePole mixS, depthS, inS;
    bool idle = true;
    DelayLine line[2];
};

class Delay
{
public:
    void prepare (double sr);
    void reset() noexcept;
    void process (float* l, float* r, int n, bool on, float seconds, float feedback, bool pingPong, float mix) noexcept;
private:
    float sampleRate = 48000.0f;
    OnePole mixS, fbS, pingS, inS;
    bool idle = true, timeValid = false, xfActive = false;
    float dCur = 2.0f, dNext = 2.0f, xf = 0.0f, xfStep = 0.001f;   // time changes cross-fade between two taps
    DelayLine line[2];
    float lp[2] {}, hp[2] {}, lpCoef = 0.5f, hpCoef = 0.01f;
};

class Reverb
{
public:
    void prepare (double sr);
    void reset() noexcept;
    void process (float* l, float* r, int n, bool on, float size, float damp, float mix) noexcept;
private:
    static constexpr int kLines = 8, kAp = 3;
    float sampleRate = 48000.0f;
    OnePole mixS, inS;
    bool idle = true;
    DelayLine line[kLines], ap[2][kAp];
    int   len[kLines] {}, apLen[2][kAp] {};
    float lpState[kLines] {}, gain[kLines] {}, modPhase[kLines] {}, modInc[kLines] {}, modDepth = 0.0f;
    float dampCoef = 0.5f, inHp[2] {}, inHpCoef = 0.01f;
    float lastSize = -1.0f, lastDamp = -1.0f;
};

// DC blocker → master gain → safety limiter + soft clip. Untouched below
// -1 dBFS; nothing ever leaves above 0 dBFS.
class Master
{
public:
    void prepare (double sr);
    void reset() noexcept;
    void process (float* l, float* r, int n, float gainDb) noexcept;
private:
    OnePole gainS;
    bool  gainValid = false;
    float dcX[2] {}, dcY[2] {}, dcCoef = 0.999f;
    float env = 1.0f, relCoef = 0.001f, attCoef = 0.5f;
};
} // namespace wsho::dsp
