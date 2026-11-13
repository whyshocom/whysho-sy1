#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "Parameters.h"
#include "PresetManager.h"
#include "dsp/SynthEngine.h"
#include "licensing/License.h"
#include "licensing/Activation.h"
#include "licensing/Account.h"
#include "licensing/AntiTamper.h"

namespace wsho
{
class WhyshoSynthAudioProcessor : public juce::AudioProcessor
{
public:
    WhyshoSynthAudioProcessor();
    ~WhyshoSynthAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "whysho SY1"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return presets.currentName(); }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int sizeInBytes) override;

    // ---- editor-facing API (contract) ----
    juce::AudioProcessorValueTreeState& state() { return apvts; }
    PresetManager& presetManager() { return presets; }
    juce::MidiKeyboardState& keyboardState() { return keyboard; }   // on-screen keys ↔ engine

    // Latest mono output for the scope: copies the most recent `n` samples
    // (n <= kScopeSize), oldest first.
    static constexpr int kScopeSize = 2048;
    void readScope (float* dest, int n) const noexcept;
    float outputPeak (int channel) const noexcept { return peak[channel & 1].load(); }  // 0..1+, decays
    int   activeVoices() const noexcept { return engine.activeVoices(); }

    // ---- licensing ----
    lic::LicenseManager&   licenseManager()   { return licMgr; }
    lic::ActivationClient& activationClient() { return activation; }
    lic::AccountClient&    accountClient()    { return account; }
    void refreshLicense();            // re-resolve key / account / trial → licenseInfo + audio gate
    lic::LicenseInfo licenseInfo;

private:
    juce::AudioProcessorValueTreeState apvts;
    PresetManager presets { apvts };
    dsp::SynthEngine engine;
    juce::MidiKeyboardState keyboard;

    std::array<std::atomic<float>*, (size_t) kNumParams> raw {};
    std::array<float, (size_t) kNumParams> values {};

    std::array<float, kScopeSize> scope {};
    std::atomic<int> scopeWrite { 0 };
    std::array<std::atomic<float>, 2> peak {};

    lic::LicenseManager   licMgr;
    lic::ActivationClient activation;
    lic::AccountClient    account;
    lic::Guard            guard;
    int blockCounter = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WhyshoSynthAudioProcessor)
};
} // namespace wsho
