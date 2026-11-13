#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include "../PluginProcessor.h"

namespace wsho::gui
{
// Overlay panel for signing in and licensing: whysho.com account (email +
// password, or "Sign in with browser"), a license key as the secondary path,
// and the local trial. Give it the editor's full bounds; it dims the editor
// and centres its own panel.
//
// Every network call runs on a worker thread and reports back on the message
// thread, so the UI never freezes. Destroying the dialog mid-request is safe:
// the destructor aborts the request and joins the worker.
class LicenseDialog : public juce::Component,
                      private juce::Timer
{
public:
    explicit LicenseDialog (WhyshoSynthAudioProcessor& p);
    ~LicenseDialog() override;

    std::function<void()> onClose;

    // How links are opened (the "Sign in with browser" page, "Get SY1").
    // Defaults to the system browser; replaceable for tests.
    std::function<void (const juce::String& url)> openUrl;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void visibilityChanged() override;
    void lookAndFeelChanged() override;

    // Re-read the processor's license + account state into the UI.
    void refreshStatus();

private:
    enum class View { SignedOut, Browser, SignedIn };
    enum class Tone { Neutral, Good, Warn, Bad };

    // Theme-token button, so the panel looks right under any LookAndFeel.
    class PanelButton : public juce::Button
    {
    public:
        enum class Style { Primary, Secondary, Link };
        PanelButton (const juce::String& label, Style s) : juce::Button (label), style (s) { setButtonText (label); }
        void setStyle (Style s) { if (style != s) { style = s; repaint(); } }
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    private:
        Style style;
    };

    // Lets the message thread interrupt a waiting worker.
    struct Control
    {
        std::mutex lock;
        std::condition_variable wake;
        bool cancelled = false;

        void cancel();
        bool isCancelled();
        bool sleep (int milliseconds);   // false if cancelled while waiting
    };

    using Work = std::function<void (Control&, juce::Component::SafePointer<LicenseDialog>)>;
    void launch (Work);
    void stopWorker();
    static void post (juce::Component::SafePointer<LicenseDialog>, std::function<void (LicenseDialog&)>);

    void signIn();
    void startBrowserSignIn();
    void cancelBrowserSignIn();
    void refreshAccount (bool quiet);
    void signOut();
    void activateKey();
    void deactivateKey();
    void startTrial();

    void setBusy (bool, const juce::String& what = {});
    void setAccountMessage (const juce::String&, Tone = Tone::Neutral);
    void setKeyMessage (const juce::String&, Tone = Tone::Neutral);
    void setKeySectionOpen (bool);
    void applyTheme();
    void updateView();
    void timerCallback() override;
    juce::Colour toneColour (Tone) const;
    int contentHeight() const;

    WhyshoSynthAudioProcessor& proc;

    juce::Label title, status, detail, accountCaption, accountMessage,
                signedInAs, ownership, lastChecked,
                codeHint, code, codeWait, keyMessage;
    juce::TextEditor emailField, passwordField, keyField;
    PanelButton signInBtn      { "Sign in",              PanelButton::Style::Primary },
                browserBtn     { "Sign in with browser", PanelButton::Style::Secondary },
                reopenBtn      { "Open browser again",   PanelButton::Style::Secondary },
                cancelBtn      { "Cancel",               PanelButton::Style::Secondary },
                refreshBtn     { "Refresh",              PanelButton::Style::Secondary },
                signOutBtn     { "Sign out",             PanelButton::Style::Secondary },
                getBtn         { "Get SY1",              PanelButton::Style::Link },
                keyToggleBtn   { "Have a license key?",  PanelButton::Style::Link },
                activateBtn    { "Activate",             PanelButton::Style::Secondary },
                deactivateBtn  { "Deactivate",           PanelButton::Style::Secondary },
                trialBtn       { "Start 14-day trial",   PanelButton::Style::Secondary },
                closeBtn       { "Close",                PanelButton::Style::Secondary };

    View view = View::SignedOut;
    bool busy = false, keyOpen = false;
    Tone statusTone = Tone::Neutral, accountTone = Tone::Neutral, keyTone = Tone::Neutral, ownershipTone = Tone::Neutral;
    juce::String appliedTheme, browserUrl;
    juce::int64 browserDeadlineMs = 0;

    juce::Rectangle<int> panelBounds, codeBox, statusDot;
    juce::Array<int> separators;

    std::thread worker;
    std::shared_ptr<Control> control;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LicenseDialog)
};
} // namespace wsho::gui
