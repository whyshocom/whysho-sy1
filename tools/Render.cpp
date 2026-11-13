// Headless render tool (dev only). STUB — replaced by the real tool.
#include "PluginProcessor.h"
int main() { juce::ScopedJuceInitialiser_GUI gui; wsho::WhyshoSynthAudioProcessor p; p.prepareToPlay (48000.0, 512); std::cout << "ok " << wsho::kNumParams << " params\n"; return 0; }
