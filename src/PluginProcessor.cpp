#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace wsho
{
namespace
{
const juce::Identifier kUiTheme ("uiTheme"), kUiWidth ("uiW"), kUiHeight ("uiH"), kSessionNode ("PRESET");
}

WhyshoSynthAudioProcessor::WhyshoSynthAudioProcessor()
    : juce::AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "WHYSHO_SY1", createParameterLayout())
{
    for (int i = 0; i < kNumParams; ++i)
        raw[(size_t) i] = apvts.getRawParameterValue (pid (i));
    refreshLicense();
    startTimer (60 * 1000);
}

void WhyshoSynthAudioProcessor::refreshLicense()
{
    licenseInfo = lic::resolveLicense (licMgr, account);
    usable.store (licenseInfo.isUsable());
    guard.setLicensed (usable.load());
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
        {
            if (auto b = pos->getBpm()) bpm = *b;
            // bar-locks synced, non-retriggered LFOs to the host timeline
            if (auto ppq = pos->getPpqPosition()) engine.setTransport (*ppq, pos->getIsPlaying());
        }

    engine.process (buffer, midi, values.data(), bpm);

    // licensing gate (deterrent degradation when unlicensed)
    guard.applyDegradation (buffer);
    if (++blockCounter >= 500) { blockCounter = 0; guard.setLicensed (usable.load (std::memory_order_relaxed)); }

    // scope + meters
    const float* l = buffer.getReadPointer (0);
    const float* r = buffer.getReadPointer (buffer.getNumChannels() > 1 ? 1 : 0);
    int w = scopeWrite.load (std::memory_order_relaxed);
    float pk[2] = { 0.0f, 0.0f };
    for (int i = 0; i < n; ++i)
    {
        scope[(size_t) w].store (0.5f * (l[i] + r[i]), std::memory_order_relaxed);
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
    for (int i = 0; i < n; ++i) { dest[i] = scope[(size_t) r].load (std::memory_order_relaxed); r = (r + 1) & (kScopeSize - 1); }
}

void WhyshoSynthAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree state;
    {
        const juce::ScopedLock l (sessionLock);
        state = apvts.copyState();
        // A state that arrived off the message thread may not have reached the
        // preset manager yet; the tree then still carries the node it was loaded with.
        if (! sessionPending) presets.saveSessionState (state);
    }

    if (const int t = ui.theme.load();  t >= 0) state.setProperty (kUiTheme, t, nullptr);
    if (const int w = ui.width.load();  w > 0)  state.setProperty (kUiWidth, w, nullptr);
    if (const int h = ui.height.load(); h > 0)  state.setProperty (kUiHeight, h, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void WhyshoSynthAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;

    const auto tree = juce::ValueTree::fromXml (*xml);
    if (! tree.isValid()) return;

    if (tree.hasProperty (kUiTheme))  ui.theme.store  (juce::jmax (0, (int) tree.getProperty (kUiTheme)));
    if (tree.hasProperty (kUiWidth))  ui.width.store  ((int) tree.getProperty (kUiWidth));
    if (tree.hasProperty (kUiHeight)) ui.height.store ((int) tree.getProperty (kUiHeight));

    // The state comes from a project file: never let a damaged or hand-edited
    // value (NaN, inf, out of range) through to the parameters and the engine.
    for (auto child : tree)
    {
        if (! child.hasType ("PARAM") || ! child.hasProperty ("value")) continue;
        const int index = paramIndex (child.getProperty ("id").toString());
        if (index < 0) continue;
        const double v = (double) child.getProperty ("value");
        const auto& d = paramDef (index);
        child.setProperty ("value", std::isnan (v) ? d.def : (float) juce::jlimit ((double) d.min, (double) d.max, v), nullptr);
    }

    // Our own copy of the preset meta: the tree belongs to the APVTS after this.
    juce::ValueTree session ("SESSION");
    if (const auto node = tree.getChildWithName (kSessionNode); node.isValid())
        session.appendChild (node.createCopy(), nullptr);

    const bool onMessageThread = juce::MessageManager::existsAndIsCurrentThread();
    {
        const juce::ScopedLock l (sessionLock);
        apvts.replaceState (tree);

        if (onMessageThread)
        {
            sessionPending = false;
            pendingSession = {};
            presets.restoreSessionState (session);
        }
        else
        {
            // Some hosts restore state from a worker thread. The preset manager (and
            // everything listening to it) is message-thread only, so hand it over.
            pendingSession = session;
            sessionPending = true;
        }
    }

    if (onMessageThread) cancelPendingUpdate();
    else                 triggerAsyncUpdate();
}

void WhyshoSynthAudioProcessor::handleAsyncUpdate()
{
    const juce::ScopedLock l (sessionLock);
    if (! sessionPending) return;
    const auto session = pendingSession;
    pendingSession = {};
    sessionPending = false;
    presets.restoreSessionState (session);
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
