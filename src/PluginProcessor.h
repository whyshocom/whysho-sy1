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
class WhyshoSynthAudioProcessor : public juce::AudioProcessor,
                                  private juce::Timer,
                                  private juce::AsyncUpdater
{
public:
    WhyshoSynthAudioProcessor();
    ~WhyshoSynthAudioProcessor() override { stopTimer(); cancelPendingUpdate(); }

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
    double getTailLengthSeconds() const override { return 12.0; }  // longest reverb RT60

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

    // Per-instance editor state (theme index, window size), saved with the plugin
    // state. Atomics rather than properties on the APVTS tree: hosts may call
    // get/setStateInformation off the message thread while the editor is open.
    struct UiState { std::atomic<int> theme { -1 }, width { 0 }, height { 0 }; };   // -1 / 0 = never set
    UiState& uiState() noexcept { return ui; }

    // ---- licensing ----
    lic::LicenseManager&   licenseManager()   { return licMgr; }
    lic::ActivationClient& activationClient() { return activation; }
    lic::AccountClient&    accountClient()    { return account; }
    void refreshLicense();            // re-resolve key / account / trial → licenseInfo + audio gate
    lic::LicenseInfo licenseInfo;

private:
    // Picks up a purchase / revoke / trial expiry found by the background
    // account re-check without the user reopening the dialog.
    void timerCallback() override { refreshLicense(); }
    // Finishes a setStateInformation() that arrived off the message thread.
    void handleAsyncUpdate() override;

    juce::AudioProcessorValueTreeState apvts;
    PresetManager presets { apvts };
    dsp::SynthEngine engine;
    juce::MidiKeyboardState keyboard;

    std::array<std::atomic<float>*, (size_t) kNumParams> raw {};
    std::array<float, (size_t) kNumParams> values {};

    std::array<std::atomic<float>, kScopeSize> scope {};      // written by the audio thread, read by the UI
    std::atomic<int> scopeWrite { 0 };
    std::array<std::atomic<float>, 2> peak {};

    lic::LicenseManager   licMgr;
    lic::ActivationClient activation;
    lic::AccountClient    account;
    lic::Guard            guard;
    std::atomic<bool>     usable { false };   // audio-thread view of licenseInfo
    int blockCounter = 0;

    UiState ui;
    juce::CriticalSection sessionLock;        // guards the two below
    juce::ValueTree pendingSession;           // preset meta of a state loaded off the message thread
    bool sessionPending = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WhyshoSynthAudioProcessor)
};
} // namespace wsho
