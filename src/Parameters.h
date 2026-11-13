#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// ===========================================================================
//  whysho SY1 — the parameter contract.
//  ONE table (Parameters.cpp) defines every parameter: id, name, range,
//  default. Everything else is derived from it:
//    * the host parameter layout (APVTS)
//    * the DSP engine reads a flat `float[P::count]` of raw values
//    * presets / banks store { id : raw value }
//    * the GUI attaches controls by `pid (P::x)`
//  To add a parameter: add an enum entry here + one row in the table (same
//  order). Never reorder/remove shipped ids — presets depend on them.
// ===========================================================================
namespace wsho
{
enum class P : int
{
    // ---- oscillators (3 identical blocks; osc N = osc1On + N * kOscStride)
    osc1On, osc1Wave, osc1Oct, osc1Semi, osc1Fine, osc1Level, osc1Pan, osc1Shape,
    osc2On, osc2Wave, osc2Oct, osc2Semi, osc2Fine, osc2Level, osc2Pan, osc2Shape,
    osc3On, osc3Wave, osc3Oct, osc3Semi, osc3Fine, osc3Level, osc3Pan, osc3Shape,
    subLevel, subWave, subOct, noiseLevel, oscFm,
    // ---- filter
    fltType, fltCutoff, fltReso, fltDrive, fltEnv, fltKey,
    // ---- envelopes (seconds; sustain 0..1)
    ampA, ampD, ampS, ampR,
    fltA, fltD, fltS, fltR,
    pitchAmt, pitchDec,
    // ---- LFOs (2 identical blocks; stride kLfoStride)
    lfo1Wave, lfo1Rate, lfo1Sync, lfo1Div, lfo1Retrig,
    lfo2Wave, lfo2Rate, lfo2Sync, lfo2Div, lfo2Retrig,
    // ---- mod matrix (4 identical slots; stride kModStride)
    mod1Src, mod1Dst, mod1Amt,
    mod2Src, mod2Dst, mod2Amt,
    mod3Src, mod3Dst, mod3Amt,
    mod4Src, mod4Dst, mod4Amt,
    // ---- voice
    voiceMode, glide, unison, uniDetune, uniSpread, bendRange, velSens,
    // ---- effects
    drvOn, drvType, drvAmt, drvMix,
    choOn, choRate, choDepth, choMix,
    dlyOn, dlySync, dlyTime, dlyDiv, dlyFb, dlyPing, dlyMix,
    revOn, revSize, revDamp, revMix,
    // ---- master
    masterGain,
    count
};

constexpr int kNumParams  = (int) P::count;
constexpr int kNumOscs    = 3, kOscStride = 8;
constexpr int kNumLfos    = 2, kLfoStride = 5;
constexpr int kNumModSlots = 4, kModStride = 3;
constexpr int kMaxVoices  = 16, kMaxUnison = 8;

// Choice orderings (index == raw parameter value). Keep in sync with the table.
enum Wave      { wSaw, wSquare, wTriangle, wSine, wSuperSaw, wNoise };
enum SubWave   { swSine, swSquare, swTriangle };
enum FilterType{ fLP24, fLP12, fBP12, fHP12, fHP24, fNotch };
enum LfoWave   { lSine, lTriangle, lSaw, lSquare, lSampleHold };
enum ModSrc    { msOff, msLfo1, msLfo2, msFltEnv, msAmpEnv, msVelocity, msModWheel, msKeyTrack, msRandom };
enum ModDst    { mdOff, mdPitch, mdOsc1Pitch, mdOsc2Pitch, mdOsc3Pitch, mdShape, mdCutoff, mdReso,
                 mdLevel, mdPan, mdLfo1Rate, mdLfo2Rate, mdFm };
enum VoiceMode { vmPoly, vmMono, vmLegato };
enum DriveType { dSoft, dHard, dTube, dFold, dCrush };
// Tempo divisions for lfoNDiv / dlyDiv — length in quarter-note beats.
inline constexpr float kDivBeats[] = { 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f,
                                       2.0f / 3.0f, 1.0f / 3.0f, 1.0f / 6.0f,   // triplets
                                       1.5f, 0.75f, 0.375f };                   // dotted
constexpr int kNumDivs = 12;

struct ParamDef
{
    enum Kind { Float, Int, Bool, Choice };
    const char* id;
    const char* name;
    Kind  kind;
    float min, max, def;
    float centre;          // >0 → skewed so `centre` sits mid-travel (Float only)
    const char* unit;      // "", "Hz", "s", "dB", "%", "ct", "st"
    const char* choices;   // '|'-separated (Choice only)
};

const ParamDef& paramDef (P p);
inline const ParamDef& paramDef (int i) { return paramDef ((P) i); }
inline juce::String pid (P p)           { return paramDef (p).id; }
inline juce::String pid (int i)         { return paramDef (i).id; }
int  paramIndex (const juce::String& id);            // -1 if unknown
juce::StringArray paramChoices (P p);

inline P oscParam (int osc, P base1) { return (P) ((int) base1 + osc * kOscStride); }   // base1 = P::osc1Wave ...
inline P lfoParam (int lfo, P base1) { return (P) ((int) base1 + lfo * kLfoStride); }   // base1 = P::lfo1Rate ...
inline P modParam (int slot, P base1){ return (P) ((int) base1 + slot * kModStride); }  // base1 = P::mod1Src ...

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

} // namespace wsho
