#pragma once
#include "DspUtil.h"
#include "Tables.h"
#include "../Parameters.h"

namespace wsho::dsp
{
// ---- envelopes -------------------------------------------------------------
// Exponential segments. Attack charges towards 1.3 (so it is near-linear with
// an analogue knee) and lands on 1.0 after exactly `attack` seconds; decay and
// release times are "time to fall 60 dB".
struct EnvCoefs { float aCoef = 0.0f, dCoef = 0.0f, rCoef = 0.0f, sustain = 1.0f; };
EnvCoefs makeEnvCoefs (float a, float d, float s, float r, float stepsPerSecond) noexcept;

struct Env
{
    enum Stage { idle, attack, decay, release };
    static constexpr float kAtkTarget = 1.3f;
    int   stage = idle;
    float level = 0.0f;

    void trigger() noexcept  { stage = attack; }               // from the current level → click-free retrigger
    void release_() noexcept { if (stage != idle) stage = release; }
    void kill() noexcept     { stage = idle; level = 0.0f; }

    float next (const EnvCoefs& c) noexcept
    {
        switch (stage)
        {
            case attack:
                level = kAtkTarget + (level - kAtkTarget) * c.aCoef;
                if (level >= 1.0f) { level = 1.0f; stage = decay; }
                break;
            case decay:
                level = c.sustain + (level - c.sustain) * c.dCoef;
                break;
            case release:
                level *= c.rCoef;
                if (level < 1.0e-4f) { level = 0.0f; stage = idle; }
                break;
            default: break;
        }
        return level;
    }
};

// ---- everything a voice needs that is common to all voices ------------------
// Filled by the engine: discrete values once per block, continuous ones
// smoothed at the global control tick.
struct Shared
{
    float sr = 48000.0f, invSr = 1.0f / 48000.0f;
    float ctrlDt = (float) kCtrl / 48000.0f;       // seconds per control tick

    // oscillators
    bool  oscOn[kNumOscs] {};
    int   oscWave[kNumOscs] {};
    float oscTune[kNumOscs] {};                    // semitones (oct + semi + fine)
    float oscLevel[kNumOscs] {}, oscPan[kNumOscs] {}, oscShape[kNumOscs] {};   // smoothed
    float subLevel = 0.0f, noiseLevel = 0.0f, fm = 0.0f;                       // smoothed
    int   subWave = 0;
    float subTune = -12.0f;

    // filter
    int   fltType = 0;
    float cutoffLog2 = 14.0f, reso = 0.0f, fltDrive = 0.0f, fltEnv = 0.0f, fltKey = 0.0f;   // smoothed

    // envelopes
    EnvCoefs ampEnv, fltEnvC;                      // amp: per sample, filter: per control tick
    float pitchAmt = 0.0f, pitchCoef = 0.0f;

    // LFOs
    int   lfoWave[kNumLfos] {};
    float lfoRate[kNumLfos] {};                    // Hz (tempo sync already resolved)
    bool  lfoRetrig[kNumLfos] {};
    float lfoGlobalPhase[kNumLfos] {};             // free-running phase new notes latch on to
    uint32_t lfoGlobalCycle[kNumLfos] {};

    // mod matrix
    int   modSrc[kNumModSlots] {}, modDst[kNumModSlots] {};
    float modAmt[kNumModSlots] {};

    // voice
    int   unison = 1;
    float uniDetune = 0.0f, uniSpread = 0.0f;      // smoothed
    float velSens = 0.5f;
    float bend = 0.0f;                             // semitones, smoothed
    float modWheel = 0.0f;                         // 0..1, smoothed
};

// ---- one voice: 3 osc × up to 8 unison → drive → SVF → amp ------------------
class Voice
{
public:
    void prepare() noexcept;

    // fromPitch < 0 → start on the note; otherwise glide from it over glideSeconds.
    void start (int note, float velocity01, float fromPitch, float glideSeconds,
                const Shared& s, uint32_t seed, uint64_t age) noexcept;
    void retrigger (int note, float velocity01, float glideSeconds, const Shared& s, uint64_t age) noexcept;  // mono
    void glideTo (int note, float glideSeconds, const Shared& s) noexcept;                                     // legato
    void noteOff() noexcept;                // enter release
    void steal (float sampleRate) noexcept; // short fade, then free
    void kill() noexcept;

    // Adds n samples into outL / outR.
    void render (float* outL, float* outR, int n, const Shared& s) noexcept;

    bool  active = false;
    bool  fading = false;      // being stolen
    bool  keyDown = false;     // key physically held
    bool  sustained = false;   // held by the sustain pedal
    int   note = -1;
    uint64_t age = 0;
    float currentPitch() const noexcept { return pitch; }
    float envLevel() const noexcept     { return ampEnv.level; }
    bool  releasing() const noexcept    { return ampEnv.stage == Env::release || ampEnv.stage == Env::idle; }

private:
    void control (const Shared& s) noexcept;
    void renderChunk (float* outL, float* outR, int n, const Shared& s) noexcept;
    void setGlide (float target, float seconds, const Shared& s) noexcept;
    void resetFilter() noexcept;

    // note state
    float velocity = 1.0f, pitch = 60.0f, targetPitch = 60.0f, glideStep = 0.0f;
    float randVal = 0.0f;
    uint32_t seed = 1;
    Rng   rng;
    bool  firstTick = true;
    int   ctrlCountdown = 0;
    float fadeGain = 1.0f, fadeStep = 0.0f;

    // modulators
    Env   ampEnv, fltEnv;
    float pitchEnv = 0.0f;
    float lfoPhase[kNumLfos] {}, lfoVal[kNumLfos] {}, lfoRateMod[kNumLfos] {};
    uint32_t lfoCycle[kNumLfos] {};
    bool  lfoFree[kNumLfos] {};

    // oscillators
    float phase[kNumOscs][kMaxUnison] {};
    float ssPhase[kNumOscs][kMaxUnison][7] {};
    float inc[kNumOscs][kMaxUnison] {};
    float ssRatio[kNumOscs][7] {};
    const float* table[kNumOscs] {};
    bool  oscRun[kNumOscs] {};
    int   waveL[kNumOscs] {}, subWaveL = 0;
    int   oscUni[kNumOscs] {};
    float shape[kNumOscs] {};
    float noiseLp[kNumOscs] {};
    float subPhase = 0.0f, subInc = 0.0f;
    const float* subTable = nullptr;
    float fmDepth = 0.0f;

    // linear ramps (cur → tgt over one control period)
    struct Ramp
    {
        float cur = 0.0f, tgt = 0.0f, step = 0.0f;
        void set (float target, bool snap) noexcept
        {
            cur = snap ? target : tgt;          // settle exactly on the previous target first
            tgt = target;
            step = (tgt - cur) * (1.0f / (float) kCtrl);
        }
    };
    Ramp gainL[kNumOscs][kMaxUnison], gainR[kNumOscs][kMaxUnison];
    Ramp subGain, noiseGain, ampGain, fltG, fltK, fltIn;
    float driveGain = 1.0f, driveNorm = 1.0f, driveWet = 0.0f;

    // filter: [stage][channel]
    float ic1[2][2] {}, ic2[2][2] {};
    int   fltType = 0;
};
} // namespace wsho::dsp
