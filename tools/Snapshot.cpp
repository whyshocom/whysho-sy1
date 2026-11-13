// Offscreen GUI renderer: instantiates the real plugin editor and writes PNGs.
// Used to preview the UI on headless machines (run under xvfb-run). Not shipped.
#include "PluginProcessor.h"
#include "PluginEditor.h"

static void renderShot (int w, int h, const juce::File& out, int themeIndex)
{
    wsho::WhyshoSynthAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    proc.state().state.setProperty ("uiTheme", themeIndex, nullptr);

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    ed->setBounds (0, 0, w, h);
    ed->setVisible (true);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (500);

    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), false, 2.0f);
    out.deleteFile();
    juce::FileOutputStream os (out);
    if (os.openedOk()) { juce::PNGImageFormat png; png.writeImageToStream (img, os); }
    std::cout << "wrote " << out.getFullPathName() << "\n";
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File dir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    dir.createDirectory();
    renderShot (1100, 700, dir.getChildFile ("whysho-sy1-paper.png"), 0);
    return 0;
}
