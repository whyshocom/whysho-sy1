#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "../Parameters.h"

namespace wsho::dsp
{
// The whole instrument: voices (osc → filter → amp), modulation, effects.
// Real-time safe: no allocation / locks in process().
// PUBLIC API IS A CONTRACT with PluginProcessor — extend, don't break.
class SynthEngine
{
public:
    SynthEngine();
    ~SynthEngine();

    void prepare (double sampleRate, int maxBlockSize);
    void reset();   // kill all voices, clear effect tails

    // Renders `out` (stereo, overwritten). `values` = kNumParams raw parameter
    // values indexed by `P`. `bpm` drives tempo-synced LFOs / delay.
    void process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi,
                  const float* values, double bpm);

    int activeVoices() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace wsho::dsp
