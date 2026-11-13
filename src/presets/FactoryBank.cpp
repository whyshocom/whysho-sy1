#include "FactoryBank.h"

// ===========================================================================
//  Factory sounds. Each preset lists only what differs from the parameter
//  defaults (Parameters.cpp), as raw values: Hz, seconds, choice index, 0 / 1.
//
//  Reminders while editing:
//    * osc shape  : Square = pulse width, SuperSaw = detune, others = wavefold
//    * flt_env    : +-1 = +-10 octaves        * mod -> Cutoff : +-1 = +-5 octaves
//    * mod -> Pitch : +-1 = +-24 semitones (0.005 ~ 12 cents of vibrato)
//    * pitch_amt  : semitones, decaying to 0 over pitch_dec — the 808 knock;
//                   negative + long decay = a riser
//    * FM is osc 2 -> osc 1; presets that use osc 2 only as a modulator keep
//      its level low rather than 0.
//  Never rename a shipped preset: favourites and DAW sessions find it by name.
// ===========================================================================
namespace wsho
{
namespace
{
using V = std::pair<P, float>;

constexpr float on = 1.0f, off = 0.0f;
enum Div { d1_1, d1_2, d1_4, d1_8, d1_16, d1_32, d1_4T, d1_8T, d1_16T, d1_4D, d1_8D, d1_16D };

void add (Bank& b, const char* name, const char* category, const char* notes, std::initializer_list<V> values)
{
    Preset p;
    p.name = name;
    p.category = category;
    p.author = b.author;
    p.notes = notes;
    for (const auto& v : values)
        p.values.emplace_back (pid (v.first), v.second);
    b.presets.push_back (std::move (p));
}
} // namespace

Bank makeFactoryBank()
{
    Bank b;
    b.name = "Factory";
    b.author = "whysho";
    b.factory = true;

    // =======================================================================
    //  808
    // =======================================================================
    add (b, "Long Sub", "808", "Clean, long sine 808 with a soft knock. The default trap low end.",
    {
        { P::osc1Wave, wSine }, { P::osc1Level, 1.0f },
        { P::pitchAmt, 12.0f }, { P::pitchDec, 0.045f },
        { P::fltCutoff, 8000.0f },
        { P::ampA, 0.001f }, { P::ampD, 4.0f }, { P::ampS, 0.5f }, { P::ampR, 0.45f },
        { P::voiceMode, vmMono }, { P::bendRange, 12.0f }, { P::velSens, 0.2f },
        { P::drvOn, on }, { P::drvType, dSoft }, { P::drvAmt, 0.12f },
    });

    add (b, "Burnt 808", "808", "Hard-clipped 808 that cuts through on small speakers.",
    {
        { P::osc1Wave, wSine }, { P::osc1Level, 1.0f }, { P::osc1Shape, 0.12f },
        { P::pitchAmt, 24.0f }, { P::pitchDec, 0.055f },
        { P::fltCutoff, 3200.0f }, { P::fltDrive, 0.3f },
        { P::ampA, 0.001f }, { P::ampD, 3.0f }, { P::ampS, 0.45f }, { P::ampR, 0.3f },
        { P::voiceMode, vmMono }, { P::bendRange, 12.0f }, { P::velSens, 0.15f },
        { P::drvOn, on }, { P::drvType, dHard }, { P::drvAmt, 0.62f }, { P::drvMix, 0.85f },
        { P::masterGain, -3.0f },
    });

    add (b, "Knock", "808", "Short, punchy 808 hit with a big pitch drop. For fast patterns.",
    {
        { P::osc1Wave, wSine }, { P::osc1Level, 1.0f },
        { P::pitchAmt, 36.0f }, { P::pitchDec, 0.03f },
        { P::ampA, 0.001f }, { P::ampD, 0.5f }, { P::ampS, 0.0f }, { P::ampR, 0.12f },
        { P::voiceMode, vmMono }, { P::bendRange, 12.0f }, { P::velSens, 0.3f },
        { P::drvOn, on }, { P::drvType, dSoft }, { P::drvAmt, 0.3f },
    });

    add (b, "Slide 808", "808", "Legato 808: overlap notes to glide between them.",
    {
        { P::osc1Wave, wSine }, { P::osc1Level, 1.0f },
        { P::pitchAmt, 18.0f }, { P::pitchDec, 0.05f },
        { P::fltCutoff, 6000.0f },
        { P::ampA, 0.001f }, { P::ampD, 3.5f }, { P::ampS, 0.55f }, { P::ampR, 0.35f },
        { P::voiceMode, vmLegato }, { P::glide, 0.11f }, { P::bendRange, 12.0f }, { P::velSens, 0.2f },
        { P::drvOn, on }, { P::drvType, dTube }, { P::drvAmt, 0.4f },
    });

    add (b, "Warm Tube", "808", "Round triangle-and-sine 808 through tube drive.",
    {
        { P::osc1Wave, wTriangle }, { P::osc1Level, 0.7f },
        { P::osc2On, on }, { P::osc2Wave, wSine }, { P::osc2Level, 0.8f },
        { P::pitchAmt, 14.0f }, { P::pitchDec, 0.06f },
        { P::fltType, fLP12 }, { P::fltCutoff, 1200.0f }, { P::fltKey, 0.5f },
        { P::ampA, 0.001f }, { P::ampD, 2.8f }, { P::ampS, 0.5f }, { P::ampR, 0.4f },
        { P::voiceMode, vmMono }, { P::bendRange, 12.0f }, { P::velSens, 0.2f },
        { P::drvOn, on }, { P::drvType, dTube }, { P::drvAmt, 0.5f },
    });

    add (b, "Growl 808", "808", "Wavefolded 808 whose harmonics bloom on the hit and die away.",
    {
        { P::osc1Wave, wSine }, { P::osc1Level, 1.0f }, { P::osc1Shape, 0.2f },
        { P::pitchAmt, 24.0f }, { P::pitchDec, 0.05f },
        { P::fltCutoff, 5000.0f },
        { P::ampA, 0.001f }, { P::ampD, 2.6f }, { P::ampS, 0.4f }, { P::ampR, 0.3f },
        { P::fltD, 0.6f }, { P::fltS, 0.0f },
        { P::mod1Src, msFltEnv }, { P::mod1Dst, mdShape }, { P::mod1Amt, 0.35f },
        { P::voiceMode, vmMono }, { P::bendRange, 12.0f }, { P::velSens, 0.2f },
        { P::drvOn, on }, { P::drvType, dFold }, { P::drvAmt, 0.35f }, { P::drvMix, 0.6f },
        { P::masterGain, -2.0f },
    });

    add (b, "Bit Dust 808", "808", "Lo-fi, bit-crushed 808 for plugg and dusty beats.",
    {
        { P::osc1Wave, wSine }, { P::osc1Level, 1.0f },
        { P::pitchAmt, 20.0f }, { P::pitchDec, 0.04f },
        { P::fltCutoff, 4000.0f },
        { P::ampA, 0.001f }, { P::ampD, 1.6f }, { P::ampS, 0.3f }, { P::ampR, 0.25f },
        { P::voiceMode, vmMono }, { P::bendRange, 12.0f }, { P::velSens, 0.2f },
        { P::drvOn, on }, { P::drvType, dCrush }, { P::drvAmt, 0.45f }, { P::drvMix, 0.5f },
    });

    add (b, "Drill Slide", "808", "Gritty drill 808 with long legato slides and a mid-range edge.",
    {
        { P::osc1Wave, wSine }, { P::osc1Level, 1.0f },
        { P::osc2On, on }, { P::osc2Wave, wTriangle }, { P::osc2Oct, 1.0f }, { P::osc2Level, 0.15f },
        { P::pitchAmt, 12.0f }, { P::pitchDec, 0.04f },
        { P::fltCutoff, 2200.0f }, { P::fltEnv, 0.15f }, { P::fltD, 0.5f },
        { P::ampA, 0.001f }, { P::ampD, 3.0f }, { P::ampS, 0.6f }, { P::ampR, 0.25f },
        { P::voiceMode, vmLegato }, { P::glide, 0.2f }, { P::bendRange, 12.0f }, { P::velSens, 0.15f },
        { P::drvOn, on }, { P::drvType, dHard }, { P::drvAmt, 0.5f },
        { P::masterGain, -2.0f },
    });

    // =======================================================================
    //  Bass
    // =======================================================================
    add (b, "Reese Wall", "Bass", "Two detuned saws beating against each other over a sine sub.",
    {
        { P::osc1Fine, -14.0f },
        { P::osc2On, on }, { P::osc2Fine, 14.0f }, { P::osc2Level, 0.8f },
        { P::subLevel, 0.5f },
        { P::fltCutoff, 420.0f }, { P::fltReso, 0.2f }, { P::fltDrive, 0.35f }, { P::fltKey, 0.3f },
        { P::ampA, 0.005f }, { P::ampS, 1.0f }, { P::ampR, 0.2f },
        { P::lfo2Rate, 0.18f }, { P::lfo2Retrig, off },
        { P::mod1Src, msLfo2 }, { P::mod1Dst, mdCutoff }, { P::mod1Amt, 0.12f },
        { P::voiceMode, vmLegato }, { P::glide, 0.05f }, { P::velSens, 0.3f },
        { P::choOn, on }, { P::choRate, 0.3f }, { P::choDepth, 0.4f }, { P::choMix, 0.25f },
    });

    add (b, "Sub Floor", "Bass", "Pure sine sub. Layer it under anything.",
    {
        { P::osc1Wave, wSine }, { P::osc1Oct, -1.0f }, { P::osc1Level, 1.0f },
        { P::ampA, 0.004f }, { P::ampS, 1.0f }, { P::ampR, 0.12f },
        { P::voiceMode, vmMono }, { P::velSens, 0.2f },
        { P::drvOn, on }, { P::drvType, dSoft }, { P::drvAmt, 0.15f },
    });

    add (b, "Rubber Pluck", "Bass", "Bouncy square bass with a snappy filter.",
    {
        { P::osc1Wave, wSquare }, { P::osc1Oct, -1.0f }, { P::osc1Shape, 0.3f },
        { P::subLevel, 0.4f },
        { P::fltCutoff, 180.0f }, { P::fltReso, 0.35f }, { P::fltEnv, 0.42f }, { P::fltKey, 0.4f },
        { P::ampD, 0.35f }, { P::ampS, 0.3f }, { P::ampR, 0.12f },
        { P::fltD, 0.16f }, { P::fltS, 0.1f },
        { P::voiceMode, vmMono },
    });

    add (b, "Dirty Square", "Bass", "Overdriven square bass with a square sub. Glides when played legato.",
    {
        { P::osc1Wave, wSquare }, { P::osc1Oct, -1.0f },
        { P::subLevel, 0.6f }, { P::subWave, swSquare },
        { P::fltCutoff, 900.0f }, { P::fltReso, 0.25f }, { P::fltEnv, 0.2f },
        { P::ampS, 1.0f },
        { P::fltD, 0.25f },
        { P::voiceMode, vmLegato }, { P::glide, 0.06f }, { P::velSens, 0.3f },
        { P::drvOn, on }, { P::drvType, dHard }, { P::drvAmt, 0.55f },
        { P::masterGain, -3.0f },
    });

    add (b, "Fat Mono", "Bass", "Classic analog mono bass: saw plus square into a driven low-pass.",
    {
        { P::osc1Oct, -1.0f },
        { P::osc2On, on }, { P::osc2Wave, wSquare }, { P::osc2Oct, -1.0f }, { P::osc2Fine, 7.0f }, { P::osc2Shape, 0.2f },
        { P::subLevel, 0.45f },
        { P::fltCutoff, 300.0f }, { P::fltReso, 0.3f }, { P::fltDrive, 0.4f }, { P::fltEnv, 0.35f }, { P::fltKey, 0.5f },
        { P::fltD, 0.3f }, { P::fltS, 0.35f },
        { P::voiceMode, vmLegato }, { P::glide, 0.07f },
        { P::drvOn, on }, { P::drvType, dTube }, { P::drvAmt, 0.3f },
    });

    // =======================================================================
    //  Lead
    // =======================================================================
    add (b, "Rage Stack", "Lead", "Wide, clipped supersaw stack for rage beats.",
    {
        { P::osc1Wave, wSuperSaw }, { P::osc1Shape, 0.55f },
        { P::osc2On, on }, { P::osc2Wave, wSuperSaw }, { P::osc2Oct, 1.0f }, { P::osc2Level, 0.45f }, { P::osc2Shape, 0.6f },
        { P::osc3On, on }, { P::osc3Wave, wSquare }, { P::osc3Oct, -1.0f }, { P::osc3Level, 0.4f }, { P::osc3Shape, 0.1f },
        { P::fltType, fLP12 }, { P::fltCutoff, 9000.0f }, { P::fltReso, 0.15f },
        { P::ampA, 0.003f }, { P::ampD, 0.4f }, { P::ampS, 0.85f }, { P::ampR, 0.18f },
        { P::unison, 2.0f }, { P::uniDetune, 0.3f }, { P::uniSpread, 0.8f },
        { P::drvOn, on }, { P::drvType, dHard }, { P::drvAmt, 0.45f }, { P::drvMix, 0.7f },
        { P::choOn, on }, { P::choMix, 0.3f },
        { P::revOn, on }, { P::revSize, 0.35f }, { P::revMix, 0.14f },
        { P::masterGain, -4.0f },
    });

    add (b, "Hyper Rage", "Lead", "Brighter, thinner rage lead with a fifth on top and an eighth-note echo.",
    {
        { P::osc1Wave, wSuperSaw }, { P::osc1Shape, 0.7f },
        { P::osc2On, on }, { P::osc2Wave, wSuperSaw }, { P::osc2Semi, 7.0f }, { P::osc2Level, 0.35f }, { P::osc2Shape, 0.6f },
        { P::fltType, fHP12 }, { P::fltCutoff, 180.0f },
        { P::ampA, 0.003f }, { P::ampS, 0.9f }, { P::ampR, 0.16f },
        { P::unison, 3.0f }, { P::uniDetune, 0.35f }, { P::uniSpread, 0.9f },
        { P::drvOn, on }, { P::drvType, dHard }, { P::drvAmt, 0.6f }, { P::drvMix, 0.75f },
        { P::choOn, on }, { P::choMix, 0.3f },
        { P::dlyOn, on }, { P::dlyDiv, d1_8 }, { P::dlyFb, 0.3f }, { P::dlyMix, 0.15f },
        { P::masterGain, -5.0f },
    });

    add (b, "Dark Flute", "Lead", "Breathy, hollow flute for dark melodies. Legato with vibrato.",
    {
        { P::osc1Wave, wTriangle },
        { P::osc2On, on }, { P::osc2Wave, wSine }, { P::osc2Oct, 1.0f }, { P::osc2Level, 0.2f },
        { P::noiseLevel, 0.07f },
        { P::fltType, fLP12 }, { P::fltCutoff, 2200.0f }, { P::fltReso, 0.2f }, { P::fltEnv, 0.1f }, { P::fltKey, 0.6f },
        { P::ampA, 0.045f }, { P::ampD, 0.2f }, { P::ampS, 0.85f }, { P::ampR, 0.22f },
        { P::fltA, 0.05f }, { P::fltD, 0.3f }, { P::fltS, 0.5f },
        { P::lfo1Rate, 5.2f }, { P::lfo1Retrig, off },
        { P::mod1Src, msLfo1 }, { P::mod1Dst, mdPitch }, { P::mod1Amt, 0.006f },
        { P::mod2Src, msVelocity }, { P::mod2Dst, mdCutoff }, { P::mod2Amt, 0.15f },
        { P::voiceMode, vmLegato }, { P::glide, 0.035f },
        { P::dlyOn, on }, { P::dlyDiv, d1_4 }, { P::dlyMix, 0.12f },
        { P::revOn, on }, { P::revSize, 0.6f }, { P::revDamp, 0.6f }, { P::revMix, 0.28f },
    });

    add (b, "Night Whistle", "Lead", "Eerie sine whistle that scoops into each note.",
    {
        { P::osc1Wave, wSine }, { P::osc1Level, 0.85f },
        { P::noiseLevel, 0.04f },
        { P::fltType, fLP12 }, { P::fltCutoff, 6000.0f },
        { P::ampA, 0.03f }, { P::ampS, 0.9f }, { P::ampR, 0.2f },
        { P::pitchAmt, -2.0f }, { P::pitchDec, 0.06f },
        { P::lfo1Rate, 5.8f }, { P::lfo1Retrig, off },
        { P::mod1Src, msLfo1 }, { P::mod1Dst, mdPitch }, { P::mod1Amt, 0.008f },
        { P::voiceMode, vmLegato }, { P::glide, 0.06f },
        { P::dlyOn, on }, { P::dlyMix, 0.2f },
        { P::revOn, on }, { P::revSize, 0.55f }, { P::revMix, 0.3f },
    });

    add (b, "Block Siren", "Lead", "Slow pitch-sweeping siren. Hold a note and let it wail.",
    {
        { P::osc2On, on }, { P::osc2Wave, wSquare }, { P::osc2Fine, 6.0f }, { P::osc2Level, 0.5f },
        { P::fltType, fLP12 }, { P::fltCutoff, 5000.0f }, { P::fltReso, 0.3f },
        { P::ampA, 0.01f }, { P::ampS, 1.0f }, { P::ampR, 0.4f },
        { P::lfo1Wave, lTriangle }, { P::lfo1Rate, 0.45f },
        { P::mod1Src, msLfo1 }, { P::mod1Dst, mdPitch }, { P::mod1Amt, 0.25f },
        { P::voiceMode, vmMono }, { P::velSens, 0.2f },
        { P::drvOn, on }, { P::drvType, dSoft }, { P::drvAmt, 0.3f },
        { P::dlyOn, on }, { P::dlyDiv, d1_4 }, { P::dlyFb, 0.45f }, { P::dlyMix, 0.3f },
        { P::revOn, on }, { P::revMix, 0.2f },
    });

    add (b, "Glide Saws", "Lead", "Detuned saw lead that slides between overlapping notes.",
    {
        { P::osc1Fine, -9.0f },
        { P::osc2On, on }, { P::osc2Fine, 9.0f }, { P::osc2Level, 0.75f },
        { P::fltType, fLP12 }, { P::fltCutoff, 3800.0f }, { P::fltReso, 0.2f }, { P::fltEnv, 0.2f }, { P::fltKey, 0.5f },
        { P::ampR, 0.25f },
        { P::fltD, 0.5f }, { P::fltS, 0.4f },
        { P::voiceMode, vmLegato }, { P::glide, 0.09f }, { P::unison, 2.0f }, { P::uniDetune, 0.25f },
        { P::choOn, on }, { P::choMix, 0.25f },
        { P::dlyOn, on }, { P::dlyMix, 0.22f },
        { P::revOn, on }, { P::revSize, 0.4f }, { P::revMix, 0.15f },
        { P::masterGain, -2.0f },
    });

    add (b, "Pixel Lead", "Lead", "Simple, slightly crunchy square lead for plugg melodies.",
    {
        { P::osc1Wave, wSquare },
        { P::fltType, fLP12 }, { P::fltCutoff, 7000.0f },
        { P::ampD, 0.25f }, { P::ampS, 0.7f }, { P::ampR, 0.12f },
        { P::lfo1Rate, 6.0f }, { P::lfo1Retrig, off },
        { P::mod1Src, msLfo1 }, { P::mod1Dst, mdPitch }, { P::mod1Amt, 0.004f },
        { P::drvOn, on }, { P::drvType, dCrush }, { P::drvAmt, 0.2f }, { P::drvMix, 0.3f },
        { P::dlyOn, on }, { P::dlyDiv, d1_8 }, { P::dlyFb, 0.3f }, { P::dlyMix, 0.2f },
    });

    add (b, "Soft Sine", "Lead", "Mellow FM-tinted sine lead with glide, for melodic hooks.",
    {
        { P::osc1Wave, wSine },
        { P::osc2On, on }, { P::osc2Wave, wSine }, { P::osc2Oct, 1.0f }, { P::osc2Level, 0.12f },
        { P::oscFm, 0.18f },
        { P::ampA, 0.01f }, { P::ampS, 0.85f }, { P::ampR, 0.3f },
        { P::lfo1Rate, 5.0f }, { P::lfo1Retrig, off },
        { P::mod1Src, msLfo1 }, { P::mod1Dst, mdPitch }, { P::mod1Amt, 0.005f },
        { P::voiceMode, vmLegato }, { P::glide, 0.08f },
        { P::choOn, on }, { P::choMix, 0.3f },
        { P::dlyOn, on }, { P::dlyDiv, d1_4D }, { P::dlyMix, 0.15f },
        { P::revOn, on }, { P::revMix, 0.25f },
    });

    // =======================================================================
    //  Pluck
    // =======================================================================
    add (b, "Glass Pluck", "Pluck", "Bright, snappy pluck with a dotted echo.",
    {
        { P::osc2On, on }, { P::osc2Wave, wSquare }, { P::osc2Oct, 1.0f }, { P::osc2Level, 0.35f }, { P::osc2Shape, 0.25f },
        { P::fltCutoff, 350.0f }, { P::fltReso, 0.3f }, { P::fltEnv, 0.5f }, { P::fltKey, 0.6f },
        { P::ampD, 0.45f }, { P::ampS, 0.0f }, { P::ampR, 0.35f },
        { P::fltD, 0.2f }, { P::fltS, 0.0f },
        { P::velSens, 0.6f },
        { P::dlyOn, on }, { P::dlyMix, 0.25f },
        { P::revOn, on }, { P::revMix, 0.2f },
    });

    add (b, "Cloud Pluck", "Pluck", "Soft, washed-out pluck that melts into reverb.",
    {
        { P::osc1Wave, wTriangle },
        { P::osc2On, on }, { P::osc2Wave, wSine }, { P::osc2Oct, 1.0f }, { P::osc2Level, 0.4f },
        { P::fltType, fLP12 }, { P::fltCutoff, 900.0f }, { P::fltEnv, 0.3f },
        { P::ampA, 0.004f }, { P::ampD, 0.7f }, { P::ampS, 0.0f }, { P::ampR, 0.6f },
        { P::fltD, 0.35f }, { P::fltS, 0.0f }, { P::fltR, 0.5f },
        { P::choOn, on }, { P::choDepth, 0.6f }, { P::choMix, 0.45f },
        { P::dlyOn, on }, { P::dlyMix, 0.18f },
        { P::revOn, on }, { P::revSize, 0.75f }, { P::revMix, 0.35f },
    });

    add (b, "Hyper Pluck", "Pluck", "Short supersaw stab for rage chords and arps.",
    {
        { P::osc1Wave, wSuperSaw }, { P::osc1Shape, 0.5f },
        { P::osc2On, on }, { P::osc2Wave, wSuperSaw }, { P::osc2Oct, 1.0f }, { P::osc2Level, 0.4f }, { P::osc2Shape, 0.55f },
        { P::fltCutoff, 600.0f }, { P::fltEnv, 0.45f },
        { P::ampD, 0.4f }, { P::ampS, 0.0f }, { P::ampR, 0.25f },
        { P::fltD, 0.22f }, { P::fltS, 0.05f },
        { P::unison, 2.0f }, { P::uniSpread, 0.8f },
        { P::dlyOn, on }, { P::dlyDiv, d1_8 }, { P::dlyMix, 0.15f },
        { P::revOn, on }, { P::revSize, 0.4f }, { P::revMix, 0.18f },
        { P::masterGain, -3.0f },
    });

    add (b, "Wood Pluck", "Pluck", "Marimba-like FM mallet; harder hits are brighter.",
    {
        { P::osc1Wave, wSine },
        { P::osc2On, on }, { P::osc2Wave, wSine }, { P::osc2Oct, 2.0f }, { P::osc2Level, 0.1f },
        { P::oscFm, 0.1f },
        { P::ampD, 0.5f }, { P::ampS, 0.0f }, { P::ampR, 0.3f },
        { P::fltD, 0.12f }, { P::fltS, 0.0f },
        { P::mod1Src, msFltEnv }, { P::mod1Dst, mdFm }, { P::mod1Amt, 0.5f },
        { P::mod2Src, msVelocity }, { P::mod2Dst, mdFm }, { P::mod2Amt, 0.2f },
        { P::velSens, 0.6f },
        { P::revOn, on }, { P::revSize, 0.3f }, { P::revMix, 0.15f },
    });

    add (b, "Pick Wire", "Pluck", "Thin, nasal plucked string with a fast echo.",
    {
        { P::osc1Wave, wSquare }, { P::osc1Shape, 0.8f },
        { P::osc2On, on }, { P::osc2Fine, 5.0f }, { P::osc2Level, 0.3f },
        { P::fltType, fLP12 }, { P::fltCutoff, 1200.0f }, { P::fltReso, 0.35f }, { P::fltEnv, 0.35f }, { P::fltKey, 0.7f },
        { P::ampD, 0.3f }, { P::ampS, 0.0f }, { P::ampR, 0.2f },
        { P::fltD, 0.12f }, { P::fltS, 0.0f },
        { P::velSens, 0.6f },
        { P::dlyOn, on }, { P::dlyDiv, d1_16 }, { P::dlyFb, 0.3f }, { P::dlyMix, 0.15f },
        { P::revOn, on }, { P::revSize, 0.3f }, { P::revMix, 0.12f },
    });

    // =======================================================================
    //  Keys
    // =======================================================================
    add (b, "Dusty EP", "Keys", "Worn electric piano with tape wobble and a little grit.",
    {
        { P::osc1Wave, wSine }, { P::osc1Level, 0.85f },
        { P::osc2On, on }, { P::osc2Wave, wSine }, { P::osc2Oct, 2.0f }, { P::osc2Level, 0.08f },
        { P::osc3On, on }, { P::osc3Wave, wTriangle }, { P::osc3Level, 0.25f },
        { P::noiseLevel, 0.02f }, { P::oscFm, 0.08f },
        { P::fltType, fLP12 }, { P::fltCutoff, 3200.0f },
        { P::ampA, 0.003f }, { P::ampD, 1.6f }, { P::ampS, 0.25f }, { P::ampR, 0.35f },
        { P::fltD, 0.25f }, { P::fltS, 0.0f },
        { P::lfo2Rate, 0.7f }, { P::lfo2Retrig, off },
        { P::mod1Src, msFltEnv }, { P::mod1Dst, mdFm }, { P::mod1Amt, 0.3f },
        { P::mod2Src, msVelocity }, { P::mod2Dst, mdFm }, { P::mod2Amt, 0.25f },
        { P::mod3Src, msLfo2 }, { P::mod3Dst, mdPitch }, { P::mod3Amt, 0.0025f },
        { P::velSens, 0.7f },
        { P::drvOn, on }, { P::drvType, dTube }, { P::drvAmt, 0.35f }, { P::drvMix, 0.6f },
        { P::choOn, on }, { P::choRate, 0.35f }, { P::choDepth, 0.55f }, { P::choMix, 0.35f },
        { P::revOn, on }, { P::revSize, 0.35f }, { P::revMix, 0.12f },
    });

    add (b, "Glass Keys", "Keys", "Clean, glassy digital keys with a bright attack.",
    {
        { P::osc1Wave, wSine },
        { P::osc2On, on }, { P::osc2Wave, wSine }, { P::osc2Oct, 1.0f }, { P::osc2Semi, 7.0f }, { P::osc2Level, 0.15f },
        { P::osc3On, on }, { P::osc3Wave, wTriangle }, { P::osc3Oct, 1.0f }, { P::osc3Level, 0.2f },
        { P::oscFm, 0.2f },
        { P::ampD, 1.2f }, { P::ampS, 0.3f }, { P::ampR, 0.5f },
        { P::fltD, 0.4f }, { P::fltS, 0.1f },
        { P::mod1Src, msFltEnv }, { P::mod1Dst, mdFm }, { P::mod1Amt, 0.35f },
        { P::velSens, 0.6f },
        { P::choOn, on },
        { P::dlyOn, on }, { P::dlyMix, 0.12f },
        { P::revOn, on }, { P::revMix, 0.22f },
    });

    add (b, "Sunday Organ", "Keys", "Drawbar-style organ: three sines, a sub and a fast chorus.",
    {
        { P::osc1Wave, wSine },
        { P::osc2On, on }, { P::osc2Wave, wSine }, { P::osc2Oct, 1.0f }, { P::osc2Level, 0.5f },
        { P::osc3On, on }, { P::osc3Wave, wSine }, { P::osc3Oct, 1.0f }, { P::osc3Semi, 7.0f }, { P::osc3Level, 0.3f },
        { P::subLevel, 0.3f },
        { P::ampA, 0.005f }, { P::ampD, 0.1f }, { P::ampS, 1.0f }, { P::ampR, 0.08f },
        { P::velSens, 0.1f },
        { P::drvOn, on }, { P::drvType, dTube }, { P::drvAmt, 0.25f },
        { P::choOn, on }, { P::choRate, 6.0f }, { P::choDepth, 0.3f }, { P::choMix, 0.4f },
        { P::revOn, on }, { P::revSize, 0.45f }, { P::revMix, 0.18f },
    });

    // =======================================================================
    //  Bell
    // =======================================================================
    add (b, "Evil Bell", "Bell", "Dark, dissonant FM bell with a long tail. Play it low and slow.",
    {
        { P::osc1Wave, wSine },
        { P::osc2On, on }, { P::osc2Wave, wSine }, { P::osc2Oct, 1.0f }, { P::osc2Semi, 6.0f }, { P::osc2Level, 0.15f },
        { P::osc3On, on }, { P::osc3Wave, wSine }, { P::osc3Oct, -1.0f }, { P::osc3Level, 0.3f },
        { P::oscFm, 0.35f },
        { P::fltType, fLP12 }, { P::fltCutoff, 4500.0f },
        { P::ampA, 0.001f }, { P::ampD, 2.2f }, { P::ampS, 0.0f }, { P::ampR, 1.8f },
        { P::fltD, 1.2f }, { P::fltS, 0.0f }, { P::fltR, 1.2f },
        { P::mod1Src, msFltEnv }, { P::mod1Dst, mdFm }, { P::mod1Amt, 0.3f },
        { P::velSens, 0.6f },
        { P::dlyOn, on }, { P::dlyDiv, d1_4D }, { P::dlyMix, 0.15f },
        { P::revOn, on }, { P::revSize, 0.8f }, { P::revDamp, 0.7f }, { P::revMix, 0.35f },
    });

    add (b, "Music Box", "Bell", "Tiny, delicate music-box tines. Sweetest in the upper octaves.",
    {
        { P::osc1Wave, wSine }, { P::osc1Oct, 1.0f },
        { P::osc2On, on }, { P::osc2Wave, wSine }, { P::osc2Oct, 3.0f }, { P::osc2Level, 0.12f },
        { P::oscFm, 0.15f },
        { P::ampA, 0.001f }, { P::ampD, 0.9f }, { P::ampS, 0.0f }, { P::ampR, 0.8f },
        { P::fltD, 0.15f }, { P::fltS, 0.0f },
        { P::mod1Src, msFltEnv }, { P::mod1Dst, mdFm }, { P::mod1Amt, 0.25f },
        { P::velSens, 0.7f },
        { P::dlyOn, on }, { P::dlyMix, 0.15f },
        { P::revOn, on }, { P::revMix, 0.25f },
    });

    add (b, "Ice Bell", "Bell", "Cold, shimmering bell with a wide chorus.",
    {
        { P::osc1Wave, wSine },
        { P::osc2On, on }, { P::osc2Wave, wSine }, { P::osc2Oct, 1.0f }, { P::osc2Semi, 7.0f }, { P::osc2Level, 0.25f },
        { P::osc3On, on }, { P::osc3Wave, wTriangle }, { P::osc3Oct, 2.0f }, { P::osc3Fine, 8.0f }, { P::osc3Level, 0.15f },
        { P::oscFm, 0.22f },
        { P::fltType, fHP12 }, { P::fltCutoff, 250.0f },
        { P::ampA, 0.001f }, { P::ampD, 1.6f }, { P::ampS, 0.0f }, { P::ampR, 1.4f },
        { P::velSens, 0.6f },
        { P::choOn, on },
        { P::dlyOn, on }, { P::dlyMix, 0.18f },
        { P::revOn, on }, { P::revSize, 0.7f }, { P::revDamp, 0.25f }, { P::revMix, 0.35f },
    });

    add (b, "Chapel Bell", "Bell", "Big, slow tubular bell for intros and breakdowns.",
    {
        { P::osc1Wave, wSine },
        { P::osc2On, on }, { P::osc2Wave, wSine }, { P::osc2Oct, 1.0f }, { P::osc2Semi, 5.0f }, { P::osc2Level, 0.2f },
        { P::oscFm, 0.5f },
        { P::fltType, fLP12 }, { P::fltCutoff, 6000.0f },
        { P::ampA, 0.001f }, { P::ampD, 3.5f }, { P::ampS, 0.0f }, { P::ampR, 3.0f },
        { P::fltD, 2.5f }, { P::fltS, 0.0f }, { P::fltR, 2.5f },
        { P::mod1Src, msFltEnv }, { P::mod1Dst, mdFm }, { P::mod1Amt, 0.25f },
        { P::velSens, 0.6f },
        { P::revOn, on }, { P::revSize, 0.85f }, { P::revMix, 0.4f },
    });

    // =======================================================================
    //  Pad
    // =======================================================================
    add (b, "Dark Matter", "Pad", "Slow, heavy low-pass pad that breathes under a beat.",
    {
        { P::osc1Fine, -11.0f },
        { P::osc2On, on }, { P::osc2Oct, -1.0f }, { P::osc2Fine, 11.0f }, { P::osc2Level, 0.7f },
        { P::subLevel, 0.3f },
        { P::fltCutoff, 380.0f }, { P::fltReso, 0.2f }, { P::fltEnv, 0.12f },
        { P::ampA, 0.9f }, { P::ampD, 1.0f }, { P::ampS, 0.9f }, { P::ampR, 2.8f },
        { P::fltA, 1.5f }, { P::fltD, 2.0f }, { P::fltS, 0.6f }, { P::fltR, 2.0f },
        { P::lfo2Rate, 0.12f }, { P::lfo2Retrig, off },
        { P::mod1Src, msLfo2 }, { P::mod1Dst, mdCutoff }, { P::mod1Amt, 0.1f },
        { P::unison, 2.0f }, { P::uniDetune, 0.3f }, { P::uniSpread, 0.9f }, { P::velSens, 0.2f },
        { P::choOn, on }, { P::choRate, 0.25f }, { P::choDepth, 0.6f }, { P::choMix, 0.45f },
        { P::revOn, on }, { P::revSize, 0.85f }, { P::revDamp, 0.65f }, { P::revMix, 0.4f },
    });

    add (b, "Air Choir", "Pad", "Breathy, vowel-like choir pad with slow pulse-width motion.",
    {
        { P::osc1Wave, wSquare }, { P::osc1Shape, 0.35f },
        { P::osc2On, on }, { P::osc2Fine, 8.0f }, { P::osc2Level, 0.45f },
        { P::noiseLevel, 0.06f },
        { P::fltType, fBP12 }, { P::fltCutoff, 950.0f }, { P::fltReso, 0.4f }, { P::fltKey, 0.3f },
        { P::ampA, 0.5f }, { P::ampD, 0.8f }, { P::ampS, 0.85f }, { P::ampR, 1.6f },
        { P::lfo1Rate, 0.35f }, { P::lfo1Retrig, off },
        { P::lfo2Rate, 5.0f }, { P::lfo2Retrig, off },
        { P::mod1Src, msLfo1 }, { P::mod1Dst, mdShape }, { P::mod1Amt, 0.2f },
        { P::mod2Src, msLfo2 }, { P::mod2Dst, mdPitch }, { P::mod2Amt, 0.003f },
        { P::unison, 3.0f }, { P::uniDetune, 0.25f }, { P::uniSpread, 0.9f }, { P::velSens, 0.2f },
        { P::choOn, on }, { P::choMix, 0.5f },
        { P::revOn, on }, { P::revSize, 0.8f }, { P::revMix, 0.42f },
    });

    add (b, "Thin Air", "Pad", "High, airy supersaw haze with no low end. Sits above the 808.",
    {
        { P::osc1Wave, wSuperSaw }, { P::osc1Oct, 1.0f }, { P::osc1Level, 0.6f }, { P::osc1Shape, 0.45f },
        { P::noiseLevel, 0.1f },
        { P::fltType, fHP12 }, { P::fltCutoff, 700.0f }, { P::fltReso, 0.15f },
        { P::ampA, 1.2f }, { P::ampS, 0.9f }, { P::ampR, 3.0f },
        { P::lfo2Rate, 0.2f }, { P::lfo2Retrig, off },
        { P::mod1Src, msLfo2 }, { P::mod1Dst, mdPan }, { P::mod1Amt, 0.35f },
        { P::velSens, 0.2f },
        { P::choOn, on },
        { P::dlyOn, on }, { P::dlyMix, 0.15f },
        { P::revOn, on }, { P::revSize, 0.9f }, { P::revDamp, 0.2f }, { P::revMix, 0.5f },
    });

    add (b, "Warm Blanket", "Pad", "Soft, rounded analog pad for chords.",
    {
        { P::osc1Wave, wTriangle }, { P::osc1Shape, 0.15f },
        { P::osc2On, on }, { P::osc2Fine, 6.0f }, { P::osc2Level, 0.35f },
        { P::subLevel, 0.25f },
        { P::fltType, fLP12 }, { P::fltCutoff, 1400.0f }, { P::fltEnv, 0.1f }, { P::fltKey, 0.4f },
        { P::ampA, 0.35f }, { P::ampD, 0.8f }, { P::ampS, 0.9f }, { P::ampR, 1.4f },
        { P::fltA, 0.6f }, { P::fltS, 0.7f }, { P::fltR, 1.0f },
        { P::unison, 2.0f }, { P::uniDetune, 0.15f }, { P::velSens, 0.3f },
        { P::choOn, on }, { P::choRate, 0.5f },
        { P::revOn, on }, { P::revSize, 0.6f }, { P::revMix, 0.3f },
    });

    add (b, "Ghost Tape", "Pad", "Wobbly, degraded tape pad with wow, flutter and hiss.",
    {
        { P::osc1Wave, wSine },
        { P::osc2On, on }, { P::osc2Wave, wTriangle }, { P::osc2Fine, 12.0f }, { P::osc2Level, 0.5f },
        { P::noiseLevel, 0.04f },
        { P::fltType, fLP12 }, { P::fltCutoff, 2400.0f },
        { P::ampA, 0.6f }, { P::ampS, 0.85f }, { P::ampR, 2.0f },
        { P::lfo1Wave, lSampleHold }, { P::lfo1Rate, 7.0f }, { P::lfo1Retrig, off },
        { P::lfo2Rate, 0.6f }, { P::lfo2Retrig, off },
        { P::mod1Src, msLfo2 }, { P::mod1Dst, mdPitch }, { P::mod1Amt, 0.004f },
        { P::mod2Src, msLfo1 }, { P::mod2Dst, mdPitch }, { P::mod2Amt, 0.0012f },
        { P::velSens, 0.3f },
        { P::drvOn, on }, { P::drvType, dCrush }, { P::drvAmt, 0.3f }, { P::drvMix, 0.35f },
        { P::choOn, on },
        { P::revOn, on }, { P::revSize, 0.7f }, { P::revMix, 0.38f },
    });

    // =======================================================================
    //  Brass
    // =======================================================================
    add (b, "Stab Brass", "Brass", "Punchy synth brass stab; velocity opens the filter.",
    {
        { P::osc2On, on }, { P::osc2Fine, 10.0f }, { P::osc2Level, 0.7f },
        { P::osc3On, on }, { P::osc3Oct, -1.0f }, { P::osc3Level, 0.4f },
        { P::fltCutoff, 450.0f }, { P::fltReso, 0.15f }, { P::fltEnv, 0.42f }, { P::fltKey, 0.5f },
        { P::ampA, 0.012f }, { P::ampS, 0.8f }, { P::ampR, 0.14f },
        { P::fltA, 0.025f }, { P::fltD, 0.28f }, { P::fltS, 0.45f }, { P::fltR, 0.15f },
        { P::mod1Src, msVelocity }, { P::mod1Dst, mdCutoff }, { P::mod1Amt, 0.2f },
        { P::unison, 2.0f }, { P::velSens, 0.6f },
        { P::drvOn, on }, { P::drvType, dTube }, { P::drvAmt, 0.3f },
        { P::revOn, on }, { P::revSize, 0.35f }, { P::revMix, 0.14f },
        { P::masterGain, -2.0f },
    });

    add (b, "War Horns", "Brass", "Low, cinematic horn section that swells into each note.",
    {
        { P::osc1Oct, -1.0f },
        { P::osc2On, on }, { P::osc2Oct, -1.0f }, { P::osc2Fine, 8.0f }, { P::osc2Level, 0.7f },
        { P::osc3On, on }, { P::osc3Wave, wSquare }, { P::osc3Oct, -2.0f }, { P::osc3Level, 0.35f }, { P::osc3Shape, 0.2f },
        { P::fltCutoff, 280.0f }, { P::fltEnv, 0.38f }, { P::fltKey, 0.4f },
        { P::ampA, 0.07f }, { P::ampD, 0.5f }, { P::ampS, 0.9f }, { P::ampR, 0.45f },
        { P::fltA, 0.12f }, { P::fltD, 0.6f }, { P::fltS, 0.6f }, { P::fltR, 0.4f },
        { P::unison, 2.0f }, { P::uniSpread, 0.7f },
        { P::drvOn, on }, { P::drvType, dTube }, { P::drvAmt, 0.35f },
        { P::revOn, on }, { P::revSize, 0.8f }, { P::revMix, 0.32f },
        { P::masterGain, -2.0f },
    });

    add (b, "Gold Brass", "Brass", "Smooth, chorused poly brass for chords.",
    {
        { P::osc2On, on }, { P::osc2Wave, wSquare }, { P::osc2Fine, -6.0f }, { P::osc2Level, 0.4f }, { P::osc2Shape, 0.3f },
        { P::fltType, fLP12 }, { P::fltCutoff, 900.0f }, { P::fltEnv, 0.3f }, { P::fltKey, 0.4f },
        { P::ampA, 0.03f }, { P::ampS, 0.85f }, { P::ampR, 0.3f },
        { P::fltA, 0.05f }, { P::fltD, 0.5f }, { P::fltS, 0.55f }, { P::fltR, 0.3f },
        { P::lfo1Rate, 5.0f }, { P::lfo1Retrig, off },
        { P::mod1Src, msLfo1 }, { P::mod1Dst, mdPitch }, { P::mod1Amt, 0.003f },
        { P::choOn, on }, { P::choMix, 0.3f },
        { P::revOn, on }, { P::revMix, 0.2f },
    });

    // =======================================================================
    //  Strings
    // =======================================================================
    add (b, "Pizzi", "Strings", "Plucked, pizzicato-style strings in a small room.",
    {
        { P::osc2On, on }, { P::osc2Oct, 1.0f }, { P::osc2Fine, 4.0f }, { P::osc2Level, 0.3f },
        { P::fltCutoff, 500.0f }, { P::fltReso, 0.15f }, { P::fltEnv, 0.35f }, { P::fltKey, 0.6f },
        { P::ampA, 0.001f }, { P::ampD, 0.22f }, { P::ampS, 0.0f }, { P::ampR, 0.18f },
        { P::fltD, 0.1f }, { P::fltS, 0.0f },
        { P::velSens, 0.7f },
        { P::revOn, on }, { P::revSize, 0.4f }, { P::revDamp, 0.4f }, { P::revMix, 0.22f },
    });

    add (b, "Slow Strings", "Strings", "Lush, slow-bowed string ensemble.",
    {
        { P::osc1Fine, -8.0f },
        { P::osc2On, on }, { P::osc2Fine, 8.0f }, { P::osc2Level, 0.75f },
        { P::osc3On, on }, { P::osc3Oct, 1.0f }, { P::osc3Level, 0.3f },
        { P::fltType, fLP12 }, { P::fltCutoff, 3200.0f }, { P::fltEnv, 0.08f }, { P::fltKey, 0.4f },
        { P::ampA, 0.35f }, { P::ampD, 0.6f }, { P::ampS, 0.9f }, { P::ampR, 0.9f },
        { P::fltA, 0.5f }, { P::fltS, 0.7f },
        { P::lfo1Rate, 5.3f }, { P::lfo1Retrig, off },
        { P::mod1Src, msLfo1 }, { P::mod1Dst, mdPitch }, { P::mod1Amt, 0.0025f },
        { P::unison, 3.0f }, { P::uniDetune, 0.22f }, { P::uniSpread, 0.85f }, { P::velSens, 0.3f },
        { P::choOn, on }, { P::choRate, 0.6f },
        { P::revOn, on }, { P::revSize, 0.65f }, { P::revMix, 0.3f },
        { P::masterGain, -2.0f },
    });

    add (b, "Spiccato", "Strings", "Short, bouncing string hits for tense, orchestral patterns.",
    {
        { P::osc1Fine, -7.0f },
        { P::osc2On, on }, { P::osc2Fine, 7.0f }, { P::osc2Level, 0.7f },
        { P::fltType, fLP12 }, { P::fltCutoff, 1800.0f }, { P::fltEnv, 0.25f }, { P::fltKey, 0.5f },
        { P::ampA, 0.008f }, { P::ampD, 0.28f }, { P::ampS, 0.25f }, { P::ampR, 0.2f },
        { P::fltD, 0.15f }, { P::fltS, 0.2f },
        { P::unison, 2.0f }, { P::velSens, 0.7f },
        { P::revOn, on }, { P::revMix, 0.25f },
    });

    // =======================================================================
    //  FX
    // =======================================================================
    add (b, "Rise Up", "FX", "Four-second riser: hold a note, pitch and filter climb into the drop.",
    {
        { P::osc1Wave, wSuperSaw }, { P::osc1Shape, 0.6f },
        { P::noiseLevel, 0.35f },
        { P::fltCutoff, 200.0f }, { P::fltReso, 0.3f }, { P::fltEnv, 0.65f },
        { P::ampA, 2.5f }, { P::ampS, 1.0f }, { P::ampR, 0.25f },
        { P::fltA, 4.0f }, { P::fltD, 1.0f }, { P::fltS, 1.0f }, { P::fltR, 0.3f },
        { P::pitchAmt, -24.0f }, { P::pitchDec, 4.0f },
        { P::lfo1Rate, 1.0f },
        { P::mod1Src, msFltEnv }, { P::mod1Dst, mdLfo1Rate }, { P::mod1Amt, 0.8f },
        { P::mod2Src, msLfo1 }, { P::mod2Dst, mdPan }, { P::mod2Amt, 0.5f },
        { P::voiceMode, vmMono }, { P::velSens, 0.0f },
        { P::choOn, on },
        { P::revOn, on }, { P::revSize, 0.7f }, { P::revMix, 0.3f },
        { P::masterGain, -3.0f },
    });

    add (b, "Fall Off", "FX", "Downlifter: pitch and filter dive after the hit.",
    {
        { P::osc2On, on }, { P::osc2Wave, wSquare }, { P::osc2Fine, 12.0f }, { P::osc2Level, 0.5f },
        { P::noiseLevel, 0.15f },
        { P::fltCutoff, 6000.0f }, { P::fltEnv, -0.5f },
        { P::ampD, 2.5f }, { P::ampS, 0.0f }, { P::ampR, 0.6f },
        { P::fltA, 2.2f }, { P::fltD, 0.5f }, { P::fltS, 1.0f }, { P::fltR, 0.6f },
        { P::pitchAmt, 36.0f }, { P::pitchDec, 2.2f },
        { P::voiceMode, vmMono }, { P::velSens, 0.0f },
        { P::dlyOn, on }, { P::dlyDiv, d1_4 }, { P::dlyMix, 0.25f },
        { P::revOn, on }, { P::revSize, 0.8f }, { P::revMix, 0.4f },
        { P::masterGain, -3.0f },
    });

    add (b, "White Wash", "FX", "Filtered noise swell for transitions.",
    {
        { P::osc1On, off },
        { P::noiseLevel, 0.9f },
        { P::fltType, fBP12 }, { P::fltCutoff, 400.0f }, { P::fltReso, 0.45f }, { P::fltEnv, 0.5f },
        { P::ampA, 1.2f }, { P::ampD, 2.0f }, { P::ampS, 0.6f }, { P::ampR, 2.0f },
        { P::fltA, 2.0f }, { P::fltD, 2.0f }, { P::fltS, 0.0f }, { P::fltR, 1.5f },
        { P::lfo2Rate, 0.25f }, { P::lfo2Retrig, off },
        { P::mod1Src, msLfo2 }, { P::mod1Dst, mdPan }, { P::mod1Amt, 0.5f },
        { P::velSens, 0.0f },
        { P::dlyOn, on }, { P::dlyMix, 0.2f },
        { P::revOn, on }, { P::revSize, 0.85f }, { P::revMix, 0.45f },
    });

    add (b, "Zap", "FX", "Laser zap one-shot with a fast echo.",
    {
        { P::fltType, fLP12 }, { P::fltCutoff, 8000.0f },
        { P::ampD, 0.25f }, { P::ampS, 0.0f }, { P::ampR, 0.1f },
        { P::pitchAmt, 48.0f }, { P::pitchDec, 0.09f },
        { P::voiceMode, vmMono }, { P::velSens, 0.3f },
        { P::drvOn, on }, { P::drvType, dHard }, { P::drvAmt, 0.4f },
        { P::dlyOn, on }, { P::dlyDiv, d1_16 }, { P::dlyFb, 0.4f }, { P::dlyMix, 0.2f },
    });

    return b;
}
} // namespace wsho
