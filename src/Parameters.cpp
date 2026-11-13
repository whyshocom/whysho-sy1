#include "Parameters.h"

namespace wsho
{
namespace
{
using K = ParamDef::Kind;
#define WAVES "Saw|Square|Triangle|Sine|SuperSaw|Noise"
#define LFOW  "Sine|Triangle|Saw|Square|S&H"
#define DIVS  "1/1|1/2|1/4|1/8|1/16|1/32|1/4T|1/8T|1/16T|1/4D|1/8D|1/16D"
#define MSRC  "Off|LFO 1|LFO 2|Filter Env|Amp Env|Velocity|Mod Wheel|Key Track|Random"
#define MDST  "Off|Pitch|Osc 1 Pitch|Osc 2 Pitch|Osc 3 Pitch|Shape|Cutoff|Resonance|Level|Pan|LFO 1 Rate|LFO 2 Rate|FM"

// Shape: Square = pulse width, SuperSaw = detune, Saw/Triangle/Sine = wavefold warp.
#define OSC(n, on, lvl) \
    { "osc" #n "_on",    "Osc " #n " On",     K::Bool,   0, 1, on, 0, "", nullptr }, \
    { "osc" #n "_wave",  "Osc " #n " Wave",   K::Choice, 0, 5, 0, 0, "", WAVES }, \
    { "osc" #n "_oct",   "Osc " #n " Octave", K::Int,   -3, 3, 0, 0, "", nullptr }, \
    { "osc" #n "_semi",  "Osc " #n " Semi",   K::Int,  -12, 12, 0, 0, "st", nullptr }, \
    { "osc" #n "_fine",  "Osc " #n " Fine",   K::Float, -100, 100, 0, 0, "ct", nullptr }, \
    { "osc" #n "_level", "Osc " #n " Level",  K::Float, 0, 1, lvl, 0, "%", nullptr }, \
    { "osc" #n "_pan",   "Osc " #n " Pan",    K::Float, -1, 1, 0, 0, "", nullptr }, \
    { "osc" #n "_shape", "Osc " #n " Shape",  K::Float, 0, 1, 0, 0, "%", nullptr }

#define LFO(n, rate) \
    { "lfo" #n "_wave",   "LFO " #n " Wave",   K::Choice, 0, 4, 0, 0, "", LFOW }, \
    { "lfo" #n "_rate",   "LFO " #n " Rate",   K::Float, 0.01f, 30, rate, 2, "Hz", nullptr }, \
    { "lfo" #n "_sync",   "LFO " #n " Sync",   K::Bool,  0, 1, 0, 0, "", nullptr }, \
    { "lfo" #n "_div",    "LFO " #n " Division", K::Choice, 0, 11, 2, 0, "", DIVS }, \
    { "lfo" #n "_retrig", "LFO " #n " Retrigger", K::Bool, 0, 1, 1, 0, "", nullptr }

#define MOD(n) \
    { "mod" #n "_src", "Mod " #n " Source", K::Choice, 0, 8, 0, 0, "", MSRC }, \
    { "mod" #n "_dst", "Mod " #n " Target", K::Choice, 0, 12, 0, 0, "", MDST }, \
    { "mod" #n "_amt", "Mod " #n " Amount", K::Float, -1, 1, 0, 0, "%", nullptr }

const ParamDef kDefs[] = {
    OSC (1, 1, 0.8f), OSC (2, 0, 0.6f), OSC (3, 0, 0.6f),
    { "sub_level",   "Sub Level",   K::Float, 0, 1, 0, 0, "%", nullptr },
    { "sub_wave",    "Sub Wave",    K::Choice, 0, 2, 0, 0, "", "Sine|Square|Triangle" },
    { "sub_oct",     "Sub Octave",  K::Choice, 0, 1, 0, 0, "", "-1 Oct|-2 Oct" },
    { "noise_level", "Noise Level", K::Float, 0, 1, 0, 0, "%", nullptr },
    { "osc_fm",      "FM 2>1",      K::Float, 0, 1, 0, 0, "%", nullptr },

    { "flt_type",   "Filter Type",   K::Choice, 0, 5, 0, 0, "", "LP 24|LP 12|BP 12|HP 12|HP 24|Notch" },
    { "flt_cutoff", "Cutoff",        K::Float, 20, 20000, 20000, 1000, "Hz", nullptr },
    { "flt_reso",   "Resonance",     K::Float, 0, 1, 0.1f, 0, "%", nullptr },
    { "flt_drive",  "Filter Drive",  K::Float, 0, 1, 0, 0, "%", nullptr },
    { "flt_env",    "Filter Env Amt",K::Float, -1, 1, 0, 0, "%", nullptr },   // ±1 = ±10 octaves
    { "flt_key",    "Key Track",     K::Float, 0, 1, 0, 0, "%", nullptr },

    { "amp_a", "Amp Attack",  K::Float, 0.0005f, 10, 0.002f, 0.3f, "s", nullptr },
    { "amp_d", "Amp Decay",   K::Float, 0.001f, 10, 0.3f, 0.5f, "s", nullptr },
    { "amp_s", "Amp Sustain", K::Float, 0, 1, 0.8f, 0, "%", nullptr },
    { "amp_r", "Amp Release", K::Float, 0.001f, 10, 0.15f, 0.5f, "s", nullptr },
    { "flt_a", "Filter Attack",  K::Float, 0.0005f, 10, 0.002f, 0.3f, "s", nullptr },
    { "flt_d", "Filter Decay",   K::Float, 0.001f, 10, 0.4f, 0.5f, "s", nullptr },
    { "flt_s", "Filter Sustain", K::Float, 0, 1, 0.3f, 0, "%", nullptr },
    { "flt_r", "Filter Release", K::Float, 0.001f, 10, 0.2f, 0.5f, "s", nullptr },
    { "pitch_amt", "Pitch Env Amt",   K::Float, -48, 48, 0, 0, "st", nullptr },
    { "pitch_dec", "Pitch Env Decay", K::Float, 0.001f, 5, 0.08f, 0.2f, "s", nullptr },

    LFO (1, 5.0f), LFO (2, 0.5f),
    MOD (1), MOD (2), MOD (3), MOD (4),

    { "voice_mode", "Voice Mode",     K::Choice, 0, 2, 0, 0, "", "Poly|Mono|Legato" },
    { "glide",      "Glide",          K::Float, 0, 2, 0, 0.2f, "s", nullptr },
    { "unison",     "Unison Voices",  K::Int,   1, 8, 1, 0, "", nullptr },
    { "uni_detune", "Unison Detune",  K::Float, 0, 1, 0.2f, 0, "%", nullptr },
    { "uni_spread", "Unison Spread",  K::Float, 0, 1, 0.5f, 0, "%", nullptr },
    { "bend_range", "Bend Range",     K::Int,   0, 24, 2, 0, "st", nullptr },
    { "vel_sens",   "Velocity Sens",  K::Float, 0, 1, 0.5f, 0, "%", nullptr },

    { "drv_on",   "Drive On",     K::Bool,   0, 1, 0, 0, "", nullptr },
    { "drv_type", "Drive Type",   K::Choice, 0, 4, 0, 0, "", "Soft|Hard|Tube|Fold|Crush" },
    { "drv_amt",  "Drive Amount", K::Float,  0, 1, 0.3f, 0, "%", nullptr },
    { "drv_mix",  "Drive Mix",    K::Float,  0, 1, 1, 0, "%", nullptr },
    { "cho_on",    "Chorus On",    K::Bool,  0, 1, 0, 0, "", nullptr },
    { "cho_rate",  "Chorus Rate",  K::Float, 0.05f, 8, 0.8f, 1, "Hz", nullptr },
    { "cho_depth", "Chorus Depth", K::Float, 0, 1, 0.5f, 0, "%", nullptr },
    { "cho_mix",   "Chorus Mix",   K::Float, 0, 1, 0.4f, 0, "%", nullptr },
    { "dly_on",   "Delay On",        K::Bool,  0, 1, 0, 0, "", nullptr },
    { "dly_sync", "Delay Sync",      K::Bool,  0, 1, 1, 0, "", nullptr },
    { "dly_time", "Delay Time",      K::Float, 0.001f, 2, 0.3f, 0.3f, "s", nullptr },
    { "dly_div",  "Delay Division",  K::Choice, 0, 11, 10, 0, "", DIVS },
    { "dly_fb",   "Delay Feedback",  K::Float, 0, 0.95f, 0.35f, 0, "%", nullptr },
    { "dly_ping", "Delay Ping-Pong", K::Bool,  0, 1, 1, 0, "", nullptr },
    { "dly_mix",  "Delay Mix",       K::Float, 0, 1, 0.25f, 0, "%", nullptr },
    { "rev_on",   "Reverb On",   K::Bool,  0, 1, 0, 0, "", nullptr },
    { "rev_size", "Reverb Size", K::Float, 0, 1, 0.5f, 0, "%", nullptr },
    { "rev_damp", "Reverb Damp", K::Float, 0, 1, 0.5f, 0, "%", nullptr },
    { "rev_mix",  "Reverb Mix",  K::Float, 0, 1, 0.25f, 0, "%", nullptr },

    { "master_gain", "Master", K::Float, -24, 12, 0, 0, "dB", nullptr },
};
static_assert (sizeof (kDefs) / sizeof (kDefs[0]) == (size_t) kNumParams, "param table / enum P mismatch");
} // namespace

const ParamDef& paramDef (P p) { return kDefs[(int) p]; }

int paramIndex (const juce::String& id)
{
    for (int i = 0; i < kNumParams; ++i)
        if (id == kDefs[i].id) return i;
    return -1;
}

juce::StringArray paramChoices (P p)
{
    juce::StringArray a;
    if (auto* c = paramDef (p).choices) a.addTokens (c, "|", "");
    return a;
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (int i = 0; i < kNumParams; ++i)
    {
        const auto& d = kDefs[i];
        const juce::ParameterID id { d.id, 1 };
        switch (d.kind)
        {
            case K::Bool:
                layout.add (std::make_unique<juce::AudioParameterBool> (id, d.name, d.def > 0.5f));
                break;
            case K::Choice:
                layout.add (std::make_unique<juce::AudioParameterChoice> (id, d.name, paramChoices ((P) i), (int) d.def));
                break;
            case K::Int:
                layout.add (std::make_unique<juce::AudioParameterInt> (id, d.name, (int) d.min, (int) d.max, (int) d.def));
                break;
            case K::Float:
            {
                juce::NormalisableRange<float> range (d.min, d.max);
                if (d.centre > 0.0f) range.setSkewForCentre (d.centre);
                layout.add (std::make_unique<juce::AudioParameterFloat> (
                    id, d.name, range, d.def, juce::AudioParameterFloatAttributes().withLabel (d.unit)));
                break;
            }
        }
    }
    return layout;
}

} // namespace wsho
