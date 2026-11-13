// ===========================================================================
//  whysho SY1 — synth engine.
//
//  Signal flow (per voice):  3 osc × unison + sub + noise → drive → SVF → amp
//  then (global):  voice sum → Drive → Chorus → Delay → Reverb → master gain
//                  → safety limiter.
//
//  PARAMETER / MODULATION SCALING — the preset designer and GUI rely on this:
//
//  Oscillators
//    * level is linear amplitude (0.5 = -6 dB). Pan is constant-power.
//    * shape:  Square    pulse width 50 % (0) → 5 % (1)
//              SuperSaw  detune of the 7 saws: ±15 ct (0) → ±1.8 st (1), quadratic
//              Saw / Triangle / Sine   wavefold: drive ×1 (0, pure) → ×5 (1)
//              Noise     colour: white (0) → dark, ~150 Hz low-pass (1)
//    * osc_fm: osc 2 phase-modulates osc 1 (Saw/Square/Triangle/Sine carriers).
//      index = fm² × 2π rad. Osc 2 must be ON (its level may be 0 to use it as
//      a silent modulator); modulator is taken before osc 2's level / pan.
//    * sub: -1 / -2 octaves from the played note (follows glide, bend, pitch
//      env and Pitch mod, not osc 1's own tuning). Mono, centred.
//    * Saw / Square / Triangle are alias-free band-limited wavetables.
//
//  Filter
//    * resonance 0..1 → Q 0.71 (1.31 for the 24 dB types) … 40, exponential.
//      Input is attenuated up to -9 dB as resonance rises.
//    * flt_env ±1 = ±10 octaves × filter envelope; flt_key 1 = 100 % tracking
//      around C3 (MIDI 60); cutoff is clamped to 16 Hz … min (23 kHz, 0.45 fs).
//    * flt_drive: tanh saturation before the filter, up to ×8 (+18 dB).
//
//  Envelopes
//    * attack = time to reach full level (min 0.5 ms); decay / release = time
//      to fall 60 dB (so the audible part of a 1 s decay is ≈ 1 s).
//    * pitch env: pitch_amt semitones at note-on, falling 60 dB in pitch_dec
//      (i.e. to 1/1000 — the "knock" is over well before pitch_dec).
//    * amp sustain 0: the voice frees itself once the decay has finished.
//
//  Mod matrix (4 slots, contributions to one destination add up)
//    sources   LFO 1/2: -1..+1     Filter Env / Amp Env: 0..1
//              Velocity: 0..1      Mod Wheel (CC1): 0..1
//              Key Track: (note - 60) / 60, clamped ±1      Random: -1..+1 per note
//    destinations (for source = 1):
//              Pitch, Osc N Pitch   24 × amt × |amt| semitones  (0.1 → 24 ct,
//                                   0.2041 → 1 st, 0.5401 → 7 st, 0.7071 → 12 st)
//              Cutoff               amt × 5 octaves
//              Shape, Resonance, Pan, FM   amt added to the 0..1 (pan: -1..1) value
//              Level                gain × (1 + amt), clamped to 0..2
//              LFO 1/2 Rate         rate × 8^amt
//    Control rate is 16 samples; all results are ramped per sample.
//
//  Voices
//    * 16-voice poly. Unison (1..8) lives inside a voice: uni_detune 1.0 = ±50 ct
//      across the stack, gain 1/sqrt(n), uni_spread pans the stack.
//    * Mono: always retriggers; glide always applies. Legato: overlapping notes
//      glide without retriggering (last-note priority, falls back to the held
//      note on release); non-overlapping notes retrigger and start on pitch.
//      Poly: each new note glides from the previously played note.
//    * velocity gain = 1 - vel_sens × (1 - (vel/127)²).
//    * LFO retrigger off = every voice follows one free-running global phase.
//      Saw LFO falls +1 → -1; Square starts high; S&H draws a new value per cycle.
//
//  Levels: one default note at velocity 100 peaks ≈ -12 dBFS. The master stage
//  is transparent below -1 dBFS and limits / soft-clips above it.
//  Tempo divisions (LFO / delay) use kDivBeats; synced delay is capped at 2 s.
// ===========================================================================
#include "SynthEngine.h"
#include "Voice.h"
#include "Effects.h"

namespace wsho::dsp
{
namespace
{
constexpr int kSlots = kMaxVoices + 8;      // spare slots let stolen voices fade out while the new note starts
constexpr int kMaxHeld = 32;

inline float pv (const float* v, P p) noexcept { return v[(int) p]; }
}

struct SynthEngine::Impl
{
    // ---- configuration
    double sampleRate = 0.0;
    bool   prepared = false;
    float  pmin[kNumParams] {}, pmax[kNumParams] {}, pdef[kNumParams] {};
    bool   discrete[kNumParams] {};
    float  p[kNumParams] {}, lastP[kNumParams] {};
    double bpm = 120.0, lastBpm = -1.0;
    bool   derivedValid = false;

    // ---- voices
    Shared sh;
    Voice  voices[kSlots];
    uint64_t ageCounter = 0;
    Rng    noteRng;
    float  lastPitch = -1.0f;
    int    voiceMode = vmPoly;
    std::atomic<int> activeCount { 0 };

    // ---- MIDI state
    bool  pedal = false;
    float bendTarget = 0.0f, wheelTarget = 0.0f;
    struct Held { int note; float vel; bool sustained; };
    Held  stack[kMaxHeld] {};
    int   stackSize = 0, monoSlot = -1;

    // ---- smoothing (global control tick)
    struct Targets
    {
        float oscLevel[kNumOscs] {}, oscPan[kNumOscs] {}, oscShape[kNumOscs] {};
        float subLevel = 0, noiseLevel = 0, fm = 0, cutoffLog2 = 14, reso = 0, fltDrive = 0, fltEnv = 0, fltKey = 0;
        float uniDetune = 0, uniSpread = 0, bend = 0, modWheel = 0;
    } tg;
    int   tickCountdown = 0;
    float smoothCoef = 1.0f;
    bool  snapSmooth = true;

    // ---- effects
    Drive  drive;
    Chorus chorus;
    Delay  delay;
    Reverb reverb;
    Master master;
    std::vector<float> scratchL, scratchR;

    Impl()
    {
        WaveTables::get();      // build tables up front, never on the audio thread
        for (int i = 0; i < kNumParams; ++i)
        {
            const auto& d = paramDef (i);
            pmin[i] = d.min; pmax[i] = d.max; pdef[i] = d.def;
            discrete[i] = d.kind != ParamDef::Float;
        }
        noteRng.seed (0xC0FFEE11u);
    }

    // -----------------------------------------------------------------------
    void prepare (double sr, int maxBlock)
    {
        sampleRate = (sr > 1000.0 && sr < 1.0e6) ? sr : 48000.0;
        const float fs = (float) sampleRate;
        sh.sr = fs; sh.invSr = 1.0f / fs; sh.ctrlDt = (float) kCtrl / fs;
        smoothCoef = 1.0f - std::exp (-(float) kCtrl / (0.006f * fs));     // 6 ms
        drive.prepare (sampleRate); chorus.prepare (sampleRate);
        delay.prepare (sampleRate); reverb.prepare (sampleRate); master.prepare (sampleRate);
        const auto cap = (size_t) juce::jmax (16, maxBlock);
        scratchL.assign (cap, 0.0f); scratchR.assign (cap, 0.0f);
        prepared = true;
        reset();
    }

    void reset()
    {
        for (auto& v : voices) v.kill();
        drive.reset(); chorus.reset(); delay.reset(); reverb.reset(); master.reset();
        pedal = false; bendTarget = wheelTarget = 0.0f;
        stackSize = 0; monoSlot = -1; lastPitch = -1.0f;
        tickCountdown = 0; snapSmooth = true; derivedValid = false;
        for (int l = 0; l < kNumLfos; ++l) { sh.lfoGlobalPhase[l] = 0.0f; sh.lfoGlobalCycle[l] = 0u; }
        activeCount.store (0, std::memory_order_relaxed);
    }

    // -----------------------------------------------------------------------
    //  Parameters
    void readParams (const float* values, double hostBpm)
    {
        for (int i = 0; i < kNumParams; ++i)
        {
            float x = values != nullptr ? values[i] : pdef[i];
            if (! (x >= pmin[i])) x = std::isnan (x) ? pdef[i] : pmin[i];      // NaN → default
            if (x > pmax[i]) x = pmax[i];
            if (discrete[i]) x = (float) (int) std::floor (x + 0.5f);
            p[i] = x;
        }
        bpm = (hostBpm > 20.0 && hostBpm < 1000.0) ? hostBpm : 120.0;

        bool changed = ! derivedValid || differs (bpm, lastBpm);
        for (int i = 0; i < kNumParams && ! changed; ++i) changed = differs (p[i], lastP[i]);
        if (! changed) return;
        for (int i = 0; i < kNumParams; ++i) lastP[i] = p[i];
        lastBpm = bpm; derivedValid = true;

        const float fs = sh.sr;
        for (int o = 0; o < kNumOscs; ++o)
        {
            const float* b = p + o * kOscStride;
            sh.oscOn[o]   = pv (b, P::osc1On) > 0.5f;
            sh.oscWave[o] = (int) pv (b, P::osc1Wave);
            sh.oscTune[o] = 12.0f * pv (b, P::osc1Oct) + pv (b, P::osc1Semi) + 0.01f * pv (b, P::osc1Fine);
            tg.oscLevel[o] = pv (b, P::osc1Level);
            tg.oscPan[o]   = pv (b, P::osc1Pan);
            tg.oscShape[o] = pv (b, P::osc1Shape);
        }
        tg.subLevel = pv (p, P::subLevel); tg.noiseLevel = pv (p, P::noiseLevel); tg.fm = pv (p, P::oscFm);
        sh.subWave = (int) pv (p, P::subWave);
        sh.subTune = pv (p, P::subOct) > 0.5f ? -24.0f : -12.0f;

        sh.fltType = (int) pv (p, P::fltType);
        tg.cutoffLog2 = std::log2 (pv (p, P::fltCutoff));
        tg.reso = pv (p, P::fltReso); tg.fltDrive = pv (p, P::fltDrive);
        tg.fltEnv = pv (p, P::fltEnv); tg.fltKey = pv (p, P::fltKey);

        sh.ampEnv  = makeEnvCoefs (pv (p, P::ampA), pv (p, P::ampD), pv (p, P::ampS), pv (p, P::ampR), fs);
        sh.fltEnvC = makeEnvCoefs (pv (p, P::fltA), pv (p, P::fltD), pv (p, P::fltS), pv (p, P::fltR), fs / (float) kCtrl);
        sh.pitchAmt = pv (p, P::pitchAmt);
        sh.pitchCoef = std::exp (-6.9078f / std::max (0.25f, pv (p, P::pitchDec) * fs / (float) kCtrl));

        for (int l = 0; l < kNumLfos; ++l)
        {
            const float* b = p + l * kLfoStride;
            sh.lfoWave[l] = (int) pv (b, P::lfo1Wave);
            sh.lfoRetrig[l] = pv (b, P::lfo1Retrig) > 0.5f;
            sh.lfoRate[l] = pv (b, P::lfo1Sync) > 0.5f
                              ? (float) (bpm / 60.0) / kDivBeats[juce::jlimit (0, kNumDivs - 1, (int) pv (b, P::lfo1Div))]
                              : pv (b, P::lfo1Rate);
        }
        for (int m = 0; m < kNumModSlots; ++m)
        {
            const float* b = p + m * kModStride;
            sh.modSrc[m] = (int) pv (b, P::mod1Src);
            sh.modDst[m] = (int) pv (b, P::mod1Dst);
            sh.modAmt[m] = pv (b, P::mod1Amt);
        }
        sh.unison = juce::jlimit (1, (int) kMaxUnison, (int) pv (p, P::unison));
        tg.uniDetune = pv (p, P::uniDetune); tg.uniSpread = pv (p, P::uniSpread);
        sh.velSens = pv (p, P::velSens);

        const int mode = (int) pv (p, P::voiceMode);
        if (mode != voiceMode)      // don't carry poly voices into mono (or a mono stack into poly)
        {
            voiceMode = mode;
            releaseAll();
        }
    }

    void smoothTick (float c) noexcept
    {
        tg.bend = bendTarget * pv (p, P::bendRange);
        tg.modWheel = wheelTarget;
        auto go = [c] (float& z, float t) { z += c * (t - z); };
        for (int o = 0; o < kNumOscs; ++o)
        {
            go (sh.oscLevel[o], tg.oscLevel[o]); go (sh.oscPan[o], tg.oscPan[o]); go (sh.oscShape[o], tg.oscShape[o]);
        }
        go (sh.subLevel, tg.subLevel); go (sh.noiseLevel, tg.noiseLevel); go (sh.fm, tg.fm);
        go (sh.cutoffLog2, tg.cutoffLog2); go (sh.reso, tg.reso); go (sh.fltDrive, tg.fltDrive);
        go (sh.fltEnv, tg.fltEnv); go (sh.fltKey, tg.fltKey);
        go (sh.uniDetune, tg.uniDetune); go (sh.uniSpread, tg.uniSpread);
        go (sh.bend, tg.bend); go (sh.modWheel, tg.modWheel);
    }

    // -----------------------------------------------------------------------
    //  Voice allocation
    int findFreeSlot() noexcept
    {
        for (int i = 0; i < kSlots; ++i)
            if (! voices[i].active) return i;
        // Every spare is still fading (a burst of steals inside 4 ms): cut the oldest fade short.
        int best = 0; uint64_t bestAge = ~(uint64_t) 0;
        for (int i = 0; i < kSlots; ++i)
            if (voices[i].fading && voices[i].age < bestAge) { bestAge = voices[i].age; best = i; }
        voices[best].kill();
        return best;
    }

    void stealOne() noexcept
    {
        // quietest releasing voice first, otherwise the oldest held one
        int victim = -1; float quietest = 1.0e9f;
        for (int i = 0; i < kSlots; ++i)
        {
            auto& v = voices[i];
            if (v.active && ! v.fading && ! v.keyDown && ! v.sustained && v.envLevel() < quietest)
            { quietest = v.envLevel(); victim = i; }
        }
        if (victim < 0)
        {
            uint64_t oldest = ~(uint64_t) 0;
            for (int i = 0; i < kSlots; ++i)
            {
                auto& v = voices[i];
                if (v.active && ! v.fading && v.age < oldest) { oldest = v.age; victim = i; }
            }
        }
        if (victim >= 0) voices[victim].steal (sh.sr);
    }

    Voice* monoVoice() noexcept
    {
        if (monoSlot < 0) return nullptr;
        auto& v = voices[monoSlot];
        return (v.active && ! v.fading) ? &v : nullptr;
    }

    void releaseAll() noexcept
    {
        for (auto& v : voices) if (v.active) v.noteOff();
        stackSize = 0; monoSlot = -1;
    }

    void noteOn (int note, float vel) noexcept
    {
        const float glide = pv (p, P::glide);
        if (voiceMode == vmPoly)
        {
            int playing = 0;
            for (auto& v : voices)
            {
                if (! v.active || v.fading) continue;
                if (v.note == note && (v.keyDown || v.sustained)) v.noteOff();      // re-struck key
                ++playing;
            }
            if (playing >= kMaxVoices) stealOne();
            const int slot = findFreeSlot();
            const float from = (glide > 0.001f && lastPitch >= 0.0f) ? lastPitch : -1.0f;
            voices[slot].start (note, vel, from, glide, sh, noteRng.nextInt(), ++ageCounter);
            lastPitch = (float) note;
            return;
        }

        // mono / legato: last-note priority stack
        for (int i = 0; i < stackSize; ++i)
            if (stack[i].note == note) { removeHeld (i); break; }
        if (stackSize == kMaxHeld) removeHeld (0);
        const bool overlapping = stackSize > 0;
        stack[stackSize++] = { note, vel, false };

        if (auto* v = monoVoice())
        {
            if (voiceMode == vmLegato && overlapping && v->keyDown) v->glideTo (note, glide, sh);
            else v->retrigger (note, vel, voiceMode == vmLegato ? 0.0f : glide, sh, ++ageCounter);
        }
        else
        {
            monoSlot = findFreeSlot();
            const float from = (voiceMode == vmMono && glide > 0.001f && lastPitch >= 0.0f) ? lastPitch : -1.0f;
            voices[monoSlot].start (note, vel, from, glide, sh, noteRng.nextInt(), ++ageCounter);
        }
        lastPitch = (float) note;
    }

    void removeHeld (int index) noexcept
    {
        for (int i = index; i < stackSize - 1; ++i) stack[i] = stack[i + 1];
        --stackSize;
    }

    // After the stack changed: follow the new top note, or release.
    void monoFollowStack() noexcept
    {
        auto* v = monoVoice();
        if (v == nullptr || ! v->keyDown) return;
        if (stackSize == 0) { v->noteOff(); return; }
        const auto& top = stack[stackSize - 1];
        if (top.note == v->note) return;
        const float glide = pv (p, P::glide);
        if (voiceMode == vmLegato) v->glideTo (top.note, glide, sh);
        else v->retrigger (top.note, top.vel, glide, sh, ++ageCounter);
        lastPitch = (float) top.note;
    }

    void noteOff (int note) noexcept
    {
        if (voiceMode == vmPoly)
        {
            for (auto& v : voices)
                if (v.active && ! v.fading && v.note == note && v.keyDown)
                {
                    if (pedal) { v.keyDown = false; v.sustained = true; }
                    else v.noteOff();
                }
            return;
        }
        for (int i = 0; i < stackSize; ++i)
            if (stack[i].note == note)
            {
                if (pedal) stack[i].sustained = true;
                else { removeHeld (i); monoFollowStack(); }
                return;
            }
    }

    void setPedal (bool down) noexcept
    {
        if (down == pedal) return;
        pedal = down;
        if (down) return;
        if (voiceMode == vmPoly)
        {
            for (auto& v : voices) if (v.active && v.sustained) v.noteOff();
            return;
        }
        for (int i = stackSize - 1; i >= 0; --i)
            if (stack[i].sustained) removeHeld (i);
        monoFollowStack();
    }

    void handleMidi (const juce::uint8* d, int numBytes) noexcept
    {
        if (numBytes < 2 || d == nullptr) return;
        const int status = d[0] & 0xF0, d1 = d[1] & 0x7F, d2 = numBytes > 2 ? (d[2] & 0x7F) : 0;
        switch (status)
        {
            case 0x90: if (d2 > 0) noteOn (d1, (float) d2 / 127.0f); else noteOff (d1); break;
            case 0x80: noteOff (d1); break;
            case 0xE0: bendTarget = (float) (((d2 << 7) | d1) - 8192) / 8192.0f; break;
            case 0xB0:
                if (d1 == 1)        wheelTarget = (float) d2 / 127.0f;
                else if (d1 == 64)  setPedal (d2 >= 64);
                else if (d1 == 120) { for (auto& v : voices) if (v.active) v.steal (sh.sr); stackSize = 0; monoSlot = -1; pedal = false; }
                else if (d1 == 121) { bendTarget = wheelTarget = 0.0f; setPedal (false); }
                else if (d1 >= 123) { pedal = false; releaseAll(); }
                break;
            default: break;
        }
    }

    // -----------------------------------------------------------------------
    //  Rendering
    void renderVoices (float* L, float* R, int n, juce::MidiBufferIterator& it,
                       const juce::MidiBufferIterator& end, int offset) noexcept
    {
        int pos = 0;
        while (pos < n)
        {
            while (it != end && (*it).samplePosition - offset <= pos)
            {
                const auto m = *it;
                handleMidi (m.data, m.numBytes);
                ++it;
            }
            int segEnd = n;
            if (it != end) segEnd = juce::jmin (n, (*it).samplePosition - offset);

            if (tickCountdown <= 0)
            {
                smoothTick (snapSmooth ? 1.0f : smoothCoef);
                snapSmooth = false;
                tickCountdown = kCtrl;
            }
            segEnd = juce::jmin (segEnd, pos + tickCountdown);
            const int m = segEnd - pos;

            std::fill (L + pos, L + segEnd, 0.0f);
            std::fill (R + pos, R + segEnd, 0.0f);
            for (auto& v : voices)
                if (v.active) v.render (L + pos, R + pos, m, sh);

            for (int l = 0; l < kNumLfos; ++l)
            {
                float ph = sh.lfoGlobalPhase[l] + sh.lfoRate[l] * sh.invSr * (float) m;
                while (ph >= 1.0f) { ph -= 1.0f; ++sh.lfoGlobalCycle[l]; }
                sh.lfoGlobalPhase[l] = ph;
            }
            tickCountdown -= m;
            pos = segEnd;
        }
    }

    static bool finite (const float* a, const float* b, int n) noexcept
    {
        float sum = 0.0f;
        for (int i = 0; i < n; ++i) sum += std::abs (a[i]) + std::abs (b[i]);
        return sum < 1.0e30f;      // false for NaN and Inf alike
    }

    void renderChunk (float* L, float* R, int n, juce::MidiBufferIterator& it,
                      const juce::MidiBufferIterator& end, int offset) noexcept
    {
        renderVoices (L, R, n, it, end, offset);

        if (! finite (L, R, n))      // should be unreachable; never let it reach the effects' feedback paths
        {
            for (auto& v : voices) v.kill();
            std::fill (L, L + n, 0.0f); std::fill (R, R + n, 0.0f);
        }

        drive.process (L, R, n, pv (p, P::drvOn) > 0.5f, (int) pv (p, P::drvType), pv (p, P::drvAmt), pv (p, P::drvMix));
        chorus.process (L, R, n, pv (p, P::choOn) > 0.5f, pv (p, P::choRate), pv (p, P::choDepth), pv (p, P::choMix));
        const float dlySec = pv (p, P::dlySync) > 0.5f
                               ? (float) (60.0 / bpm) * kDivBeats[juce::jlimit (0, kNumDivs - 1, (int) pv (p, P::dlyDiv))]
                               : pv (p, P::dlyTime);
        delay.process (L, R, n, pv (p, P::dlyOn) > 0.5f, juce::jmin (2.0f, dlySec), pv (p, P::dlyFb), pv (p, P::dlyPing) > 0.5f, pv (p, P::dlyMix));
        reverb.process (L, R, n, pv (p, P::revOn) > 0.5f, pv (p, P::revSize), pv (p, P::revDamp), pv (p, P::revMix));
        master.process (L, R, n, pv (p, P::masterGain));

        if (! finite (L, R, n))
        {
            drive.reset(); chorus.reset(); delay.reset(); reverb.reset(); master.reset();
            std::fill (L, L + n, 0.0f); std::fill (R, R + n, 0.0f);
        }
    }

    void process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, const float* values, double hostBpm)
    {
        juce::ScopedNoDenormals noDenormals;
        const int n = out.getNumSamples(), numCh = out.getNumChannels();
        if (! prepared || numCh < 1) { out.clear(); return; }

        readParams (values, hostBpm);
        auto it = midi.begin();
        const auto end = midi.end();

        if (numCh >= 2)
        {
            renderChunk (out.getWritePointer (0), out.getWritePointer (1), n, it, end, 0);
            for (int c = 2; c < numCh; ++c) out.clear (c, 0, n);
        }
        else
        {
            auto* dst = out.getWritePointer (0);
            const int cap = (int) scratchL.size();
            for (int done = 0; done < n;)
            {
                const int m = juce::jmin (cap, n - done);
                renderChunk (scratchL.data(), scratchR.data(), m, it, end, done);
                for (int i = 0; i < m; ++i) dst[done + i] = 0.5f * (scratchL[(size_t) i] + scratchR[(size_t) i]);
                done += m;
            }
        }

        for (; it != end; ++it)      // events stamped beyond the block still count
        {
            const auto m = *it;
            handleMidi (m.data, m.numBytes);
        }

        int count = 0;
        for (auto& v : voices) count += v.active ? 1 : 0;
        activeCount.store (juce::jmin (count, (int) kMaxVoices), std::memory_order_relaxed);
    }
};

// ---------------------------------------------------------------------------
SynthEngine::SynthEngine() : impl (std::make_unique<Impl>()) {}
SynthEngine::~SynthEngine() = default;

void SynthEngine::prepare (double sampleRate, int maxBlockSize) { impl->prepare (sampleRate, maxBlockSize); }
void SynthEngine::reset() { if (impl->prepared) impl->reset(); }

void SynthEngine::process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi,
                           const float* values, double bpm)
{
    impl->process (out, midi, values, bpm);
}

int SynthEngine::activeVoices() const noexcept { return impl->activeCount.load (std::memory_order_relaxed); }
} // namespace wsho::dsp
