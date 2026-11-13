#include "SynthEngine.h"
// STUB — replaced by the real engine.
namespace wsho::dsp
{
struct SynthEngine::Impl {};
SynthEngine::SynthEngine() : impl (std::make_unique<Impl>()) {}
SynthEngine::~SynthEngine() = default;
void SynthEngine::prepare (double, int) {}
void SynthEngine::reset() {}
void SynthEngine::process (juce::AudioBuffer<float>& out, const juce::MidiBuffer&, const float*, double) { out.clear(); }
int SynthEngine::activeVoices() const noexcept { return 0; }
}
