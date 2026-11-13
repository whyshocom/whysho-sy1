#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace wsho
{
WhyshoSynthAudioProcessor::WhyshoSynthAudioProcessor()
    : juce::AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "WHYSHO_SY1", createParameterLayout())
{
    for (int i = 0; i < kNumParams; ++i)
        raw[(size_t) i] = apvts.getRawParameterValue (pid (i));
    refreshLicense();
}

void WhyshoSynthAudioProcessor::refreshLicense()
{
    licenseInfo = lic::resolveLicense (licMgr, account);
    guard.setLicensed (licenseInfo.isUsable());
}

bool WhyshoSynthAudioProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void WhyshoSynthAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    guard.prepare (sampleRate);
    keyboard.reset();
}

void WhyshoSynthAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();

    // merge on-screen keyboard notes, and light its keys from host MIDI
    keyboard.processNextMidiBuffer (midi, 0, n, true);

    for (int i = 0; i < kNumParams; ++i)
        values[(size_t) i] = raw[(size_t) i]->load (std::memory_order_relaxed);

    double bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm()) bpm = *b;

    engine.process (buffer, midi, values.data(), bpm);

    // licensing gate (deterrent degradation when unlicensed)
    guard.applyDegradation (buffer);
    if (++blockCounter >= 500) { blockCounter = 0; guard.setLicensed (licenseInfo.isUsable()); }

    // scope + meters
    const float* l = buffer.getReadPointer (0);
    const float* r = buffer.getReadPointer (buffer.getNumChannels() > 1 ? 1 : 0);
    int w = scopeWrite.load (std::memory_order_relaxed);
    float pk[2] = { 0.0f, 0.0f };
    for (int i = 0; i < n; ++i)
    {
        scope[(size_t) w] = 0.5f * (l[i] + r[i]);
        w = (w + 1) & (kScopeSize - 1);
        pk[0] = juce::jmax (pk[0], std::abs (l[i]));
        pk[1] = juce::jmax (pk[1], std::abs (r[i]));
    }
    scopeWrite.store (w, std::memory_order_relaxed);
    const float decay = std::exp (-(float) n / (0.3f * (float) getSampleRate()));
    for (int c = 0; c < 2; ++c)
        peak[(size_t) c].store (juce::jmax (pk[c], peak[(size_t) c].load() * decay));
}

void WhyshoSynthAudioProcessor::readScope (float* dest, int n) const noexcept
{
    n = juce::jlimit (0, (int) kScopeSize, n);
    int r = (scopeWrite.load (std::memory_order_relaxed) - n) & (kScopeSize - 1);
    for (int i = 0; i < n; ++i) { dest[i] = scope[(size_t) r]; r = (r + 1) & (kScopeSize - 1); }
}

void WhyshoSynthAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    presets.saveSessionState (state);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void WhyshoSynthAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            presets.restoreSessionState (apvts.state);
        }
}

juce::AudioProcessorEditor* WhyshoSynthAudioProcessor::createEditor()
{
    return new WhyshoSynthAudioProcessorEditor (*this);
}
} // namespace wsho

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new wsho::WhyshoSynthAudioProcessor();
}
