#include "LicenseDialog.h"
#include "Theme.h"
#include "../licensing/ObfString.h"

using namespace wsho;
using namespace wsho::gui;

namespace
{
constexpr int kPanelWidth = 520, kPadX = 26, kPadY = 22, kAccountArea = 142;

juce::int64 nowSeconds() { return juce::Time::currentTimeMillis() / 1000; }

// Whichever of the theme's own text/background tokens reads best on `fill`.
juce::Colour inkOn (juce::Colour fill)
{
    const auto& t = theme();
    const auto contrast = [&] (juce::Colour c) { return std::abs (c.getPerceivedBrightness() - fill.getPerceivedBrightness()); };
    return contrast (t.bg) > contrast (t.text) ? t.bg : t.text;
}

juce::String agoText (juce::int64 unixSeconds)
{
    if (unixSeconds <= 0) return "Not checked yet";
    const auto age = nowSeconds() - unixSeconds;
    if (age < 90)        return "Checked just now";
    if (age < 3600)      return "Checked " + juce::String (age / 60) + " minutes ago";
    if (age < 2 * 3600)  return "Checked an hour ago";
    if (age < 86400)     return "Checked " + juce::String (age / 3600) + " hours ago";
    if (age < 2 * 86400) return "Checked yesterday";
    return "Checked " + juce::String (age / 86400) + " days ago";
}
} // namespace

// ---------------------------------------------------------------------------
void LicenseDialog::PanelButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto& t = theme();
    const auto b = getLocalBounds().toFloat().reduced (0.5f);
    const float alpha = isEnabled() ? 1.0f : 0.45f;

    if (style == Style::Link)
    {
        const auto c = (highlighted || down ? t.accent.brighter (0.1f) : t.accent).withMultipliedAlpha (alpha);
        const auto font = themeFont (13.0f);
        g.setColour (c);
        g.setFont (font);
        g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centredLeft, true);
        if (highlighted && isEnabled())
        {
            const float w = juce::jmin ((float) getWidth(), juce::GlyphArrangement::getStringWidth (font, getButtonText()));
            g.fillRect (0.0f, (float) getHeight() * 0.5f + font.getHeight() * 0.55f, w, 1.0f);
        }
        return;
    }

    auto fill = style == Style::Primary ? t.accent : t.panel2;
    if (down) fill = fill.darker (0.12f); else if (highlighted && isEnabled()) fill = fill.brighter (0.08f);

    g.setColour (fill.withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (b, 5.0f);
    g.setColour ((hasKeyboardFocus (false) ? t.accent : (style == Style::Primary ? t.accent : t.grid)).withMultipliedAlpha (alpha));
    g.drawRoundedRectangle (b, 5.0f, 1.0f);

    g.setColour ((style == Style::Primary ? inkOn (t.accent) : t.text).withMultipliedAlpha (alpha));
    g.setFont (themeFont (13.0f, style == Style::Primary));
    g.drawText (getButtonText(), getLocalBounds().reduced (6, 0), juce::Justification::centred, true);
}

// ---------------------------------------------------------------------------
void LicenseDialog::Control::cancel()
{
    { const std::lock_guard<std::mutex> g (lock); cancelled = true; }
    wake.notify_all();
}

bool LicenseDialog::Control::isCancelled()
{
    const std::lock_guard<std::mutex> g (lock);
    return cancelled;
}

bool LicenseDialog::Control::sleep (int milliseconds)
{
    std::unique_lock<std::mutex> l (lock);
    wake.wait_for (l, std::chrono::milliseconds (milliseconds), [this] { return cancelled; });
    return ! cancelled;
}

void LicenseDialog::launch (Work work)
{
    stopWorker();   // normally already finished; never leave two workers running
    control = std::make_shared<Control>();

    // The SafePointer is made here, on the message thread; the worker only
    // carries it back into post() and never dereferences it itself.
    juce::Component::SafePointer<LicenseDialog> safe (this);
    worker = std::thread ([c = control, safe, w = std::move (work)] { w (*c, safe); });
}

void LicenseDialog::stopWorker()
{
    if (! worker.joinable()) return;

    // Cancel the wait, cut any in-flight request short, then join: this
    // returns in milliseconds instead of sitting out a network timeout.
    if (control != nullptr) control->cancel();
    proc.accountClient().beginAbort();
    proc.activationClient().beginAbort();
    worker.join();
    proc.activationClient().endAbort();
    proc.accountClient().endAbort();
}

void LicenseDialog::post (juce::Component::SafePointer<LicenseDialog> safe, std::function<void (LicenseDialog&)> fn)
{
    juce::MessageManager::callAsync ([safe, fn = std::move (fn)]
    {
        if (auto* dialog = safe.getComponent()) fn (*dialog);   // dialog may be long gone
    });
}

// ---------------------------------------------------------------------------
LicenseDialog::LicenseDialog (WhyshoSynthAudioProcessor& p) : proc (p)
{
    setWantsKeyboardFocus (true);
    setInterceptsMouseClicks (true, true);   // swallow clicks so the editor underneath stays inert

    openUrl = [] (const juce::String& url) { juce::URL (url).launchInDefaultBrowser(); };

    title.setText ("whysho SY1 license", juce::dontSendNotification);
    accountCaption.setText (caps ("Account"), juce::dontSendNotification);
    codeHint.setText ("Approve this code in your browser at whysho.com/activate", juce::dontSendNotification);
    code.setJustificationType (juce::Justification::centred);

    for (auto* l : { &title, &status, &detail, &accountCaption, &accountMessage, &signedInAs, &ownership,
                     &lastChecked, &codeHint, &code, &codeWait, &keyMessage })
    {
        l->setInterceptsMouseClicks (false, false);
        l->setMinimumHorizontalScale (0.75f);
        l->setBorderSize ({});   // line text up with the fields' left edge
        addAndMakeVisible (l);
    }
    accountMessage.setJustificationType (juce::Justification::topLeft);

    emailField.setTextToShowWhenEmpty ("Email", theme().textDim);
    emailField.setInputRestrictions (254);
    emailField.onReturnKey = [this] { passwordField.grabKeyboardFocus(); };

    passwordField.setPasswordCharacter ((juce::juce_wchar) 0x2022);
    passwordField.setInputRestrictions (256);
    passwordField.onReturnKey = [this] { signIn(); };

    keyField.setInputRestrictions (64);
    keyField.onReturnKey = [this] { activateKey(); };

    for (auto* e : { &emailField, &passwordField, &keyField })
    {
        e->setSelectAllWhenFocused (true);
        e->setJustification (juce::Justification::centredLeft);
        e->setIndents (10, 0);
        e->onEscapeKey = [this] { if (onClose) onClose(); };
        addAndMakeVisible (e);
    }

    for (auto* b : { &signInBtn, &browserBtn, &reopenBtn, &cancelBtn, &refreshBtn, &signOutBtn, &getBtn,
                     &keyToggleBtn, &activateBtn, &deactivateBtn, &trialBtn, &closeBtn })
        addAndMakeVisible (b);

    signInBtn.onClick     = [this] { signIn(); };
    browserBtn.onClick    = [this] { startBrowserSignIn(); };
    reopenBtn.onClick     = [this] { if (browserUrl.isNotEmpty() && openUrl) openUrl (browserUrl); };
    cancelBtn.onClick     = [this] { cancelBrowserSignIn(); };
    refreshBtn.onClick    = [this] { refreshAccount (false); };
    signOutBtn.onClick    = [this] { signOut(); };
    getBtn.onClick        = [this] { if (openUrl) openUrl (WSHO_JS ("https://whysho.com/plugins/sy1")); };
    keyToggleBtn.onClick  = [this] { setKeySectionOpen (! keyOpen); };
    activateBtn.onClick   = [this] { activateKey(); };
    deactivateBtn.onClick = [this] { deactivateKey(); };
    trialBtn.onClick      = [this] { startTrial(); };
    closeBtn.onClick      = [this] { if (onClose) onClose(); };

    // Someone already using a key should see that section straight away.
    keyOpen = proc.licenseManager().loadToken().isNotEmpty();

    applyTheme();
    refreshStatus();
}

LicenseDialog::~LicenseDialog()
{
    stopTimer();
    stopWorker();
}

// ---------------------------------------------------------------------------
juce::Colour LicenseDialog::toneColour (Tone tone) const
{
    const auto& t = theme();
    switch (tone)
    {
        case Tone::Good:    return t.good;
        case Tone::Warn:    return t.warn;
        case Tone::Bad:     return t.bad;
        case Tone::Neutral: break;
    }
    return t.textDim;
}

void LicenseDialog::applyTheme()
{
    const auto& t = theme();
    appliedTheme = t.name;

    title.setFont (themeFont (18.0f, true));
    status.setFont (themeFont (14.0f, true));
    accountCaption.setFont (themeFont (11.0f, true));
    signedInAs.setFont (themeFont (14.0f, true));
    code.setFont (themeFont (32.0f, true).withExtraKerningFactor (0.12f));
    for (auto* l : { &detail, &accountMessage, &ownership, &lastChecked, &codeHint, &codeWait, &keyMessage })
        l->setFont (themeFont (12.5f));

    title.setColour (juce::Label::textColourId, t.text);
    signedInAs.setColour (juce::Label::textColourId, t.text);
    code.setColour (juce::Label::textColourId, t.text);
    for (auto* l : { &detail, &accountCaption, &lastChecked, &codeHint, &codeWait })
        l->setColour (juce::Label::textColourId, t.textDim);

    status.setColour (juce::Label::textColourId, statusTone == Tone::Neutral ? t.text : toneColour (statusTone));
    accountMessage.setColour (juce::Label::textColourId, toneColour (accountTone));
    keyMessage.setColour (juce::Label::textColourId, toneColour (keyTone));
    ownership.setColour (juce::Label::textColourId, toneColour (ownershipTone));

    emailField.setTextToShowWhenEmpty ("Email", t.textDim);
    passwordField.setTextToShowWhenEmpty ("Password", t.textDim);
    keyField.setTextToShowWhenEmpty ("WSHO-XXXXX-XXXXX-XXXXX-XXXXX", t.textDim);

    for (auto* e : { &emailField, &passwordField, &keyField })
    {
        e->setColour (juce::TextEditor::backgroundColourId, t.panel2);
        e->setColour (juce::TextEditor::textColourId, t.text);
        e->setColour (juce::TextEditor::outlineColourId, t.grid);
        e->setColour (juce::TextEditor::focusedOutlineColourId, t.accent);
        e->setColour (juce::TextEditor::highlightColourId, t.accent.withAlpha (0.3f));
        e->setColour (juce::TextEditor::highlightedTextColourId, t.text);
        e->setColour (juce::CaretComponent::caretColourId, t.text);
        e->setFont (themeFont (14.0f));
        e->applyFontToAllText (themeFont (14.0f));
        e->applyColourToAllText (t.text);
    }
    repaint();
}

void LicenseDialog::lookAndFeelChanged()  { applyTheme(); }

void LicenseDialog::visibilityChanged()
{
    if (! isVisible()) return;
    refreshStatus();

    // Opening the panel is a good moment to pick up a purchase made since
    // the last check — quietly, and only if the answer is a little old.
    auto& acct = proc.accountClient();
    if (! busy && acct.isSignedIn() && nowSeconds() - acct.lastCheckedAt() > 600)
        refreshAccount (true);
}

bool LicenseDialog::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey) { if (onClose) onClose(); return true; }
    return false;   // leave host shortcuts (transport etc.) alone
}

// ---------------------------------------------------------------------------
void LicenseDialog::setBusy (bool b, const juce::String& what)
{
    busy = b;
    if (b && what.isNotEmpty()) setAccountMessage (what);
    updateView();
}

void LicenseDialog::setAccountMessage (const juce::String& text, Tone tone)
{
    accountTone = tone;
    accountMessage.setText (text, juce::dontSendNotification);
    accountMessage.setColour (juce::Label::textColourId, toneColour (tone));
}

void LicenseDialog::setKeyMessage (const juce::String& text, Tone tone)
{
    keyTone = tone;
    keyMessage.setText (text, juce::dontSendNotification);
    keyMessage.setColour (juce::Label::textColourId, toneColour (tone));
}

void LicenseDialog::setKeySectionOpen (bool open)
{
    keyOpen = open;
    updateView();
    resized();
    repaint();
}

void LicenseDialog::updateView()
{
    if (view != View::Browser)
        view = proc.accountClient().isSignedIn() ? View::SignedIn : View::SignedOut;

    const bool out = view == View::SignedOut, browser = view == View::Browser, in = view == View::SignedIn;

    for (auto* c : std::initializer_list<juce::Component*> { &emailField, &passwordField, &signInBtn, &browserBtn })
        c->setVisible (out);
    for (auto* c : std::initializer_list<juce::Component*> { &codeHint, &code, &codeWait, &reopenBtn, &cancelBtn })
        c->setVisible (browser);
    for (auto* c : std::initializer_list<juce::Component*> { &signedInAs, &ownership, &lastChecked, &refreshBtn, &signOutBtn, &getBtn })
        c->setVisible (in);
    for (auto* c : std::initializer_list<juce::Component*> { &keyField, &activateBtn, &deactivateBtn, &keyMessage })
        c->setVisible (keyOpen);

    keyToggleBtn.setButtonText (keyOpen ? "Hide license key" : "Have a license key?");

    // One request at a time: everything that would start another is disabled
    // while busy. Cancel and Close always stay available.
    for (auto* c : std::initializer_list<juce::Component*> { &emailField, &passwordField, &signInBtn, &browserBtn, &refreshBtn,
                                                             &signOutBtn, &keyField, &activateBtn, &deactivateBtn, &trialBtn })
        c->setEnabled (! busy);

    const bool hasKeyToken = proc.licenseManager().loadToken().isNotEmpty();
    if (! busy)
    {
        deactivateBtn.setEnabled (hasKeyToken);
        trialBtn.setEnabled (! proc.licenseInfo.isUsable());
    }
    trialBtn.setVisible (proc.licenseInfo.state != lic::LicenseState::Licensed);
    trialBtn.setButtonText (proc.licenseInfo.state == lic::LicenseState::Trial ? "Trial running" : "Start 14-day trial");
}

void LicenseDialog::refreshStatus()
{
    if (appliedTheme != theme().name) applyTheme();

    const auto& i = proc.licenseInfo;
    auto& acct = proc.accountClient();
    const auto now = nowSeconds();
    juce::String s, d;

    switch (i.state)
    {
        case lic::LicenseState::Licensed:
            statusTone = Tone::Good;
            if (i.edition == "account")
            {
                s = "Licensed to " + i.email;
                d = "Unlocked by your whysho account. Works offline for up to "
                    + juce::String (lic::AccountClient::kOfflineGraceDays) + " days at a time.";
            }
            else
            {
                s = "Licensed (" + i.edition + ")" + (i.email.isNotEmpty() ? " to " + i.email : juce::String());
                d = "Unlocked by a license key on this computer.";
            }
            break;

        case lic::LicenseState::Trial:
        {
            const auto days = juce::jmax ((juce::int64) 0, (i.expiresAt - now + 86399) / 86400);
            statusTone = Tone::Warn;
            s = "Trial: " + juce::String (days) + (days == 1 ? " day left" : " days left");
            d = "Fully working until then. Sign in with an account that owns SY1 to keep it.";
            break;
        }

        case lic::LicenseState::Expired:
            statusTone = Tone::Bad;
            s = "Trial ended";
            d = "Sound is interrupted every few seconds until SY1 is unlocked.";
            break;

        case lic::LicenseState::MachineMismatch:
            statusTone = Tone::Bad;
            s = "License belongs to another computer";
            d = "Sign in or activate your key again on this one.";
            break;

        case lic::LicenseState::Tampered:
            statusTone = Tone::Bad;
            s = "License data is damaged";
            d = "Sign in or activate your key again to repair it.";
            break;

        case lic::LicenseState::Unlicensed:
            statusTone = Tone::Bad;
            s = "Not licensed";
            d = "Sound is interrupted every few seconds until SY1 is unlocked.";
            break;
    }

    status.setText (s, juce::dontSendNotification);
    status.setColour (juce::Label::textColourId, toneColour (statusTone));
    detail.setText (d, juce::dontSendNotification);

    // --- account block ---
    if (acct.isSignedIn())
    {
        const auto cached = acct.cachedLicense();
        signedInAs.setText ("Signed in as " + acct.email(), juce::dontSendNotification);

        juce::String o;
        if (cached.state == lic::LicenseState::Licensed)      { o = "This account owns SY1.";                 ownershipTone = Tone::Good; }
        else if (cached.state == lic::LicenseState::Expired)  { o = cached.reason;                             ownershipTone = Tone::Warn; }
        else if (acct.lastCheckedAt() == 0)                   { o = "Ownership not checked yet. Press Refresh."; ownershipTone = Tone::Neutral; }
        else                                                  { o = "This account doesn't own SY1 yet.";       ownershipTone = Tone::Warn; }

        ownership.setText (o, juce::dontSendNotification);
        ownership.setColour (juce::Label::textColourId, toneColour (ownershipTone));
        lastChecked.setText (agoText (acct.lastCheckedAt()), juce::dontSendNotification);

        const bool owned = cached.state == lic::LicenseState::Licensed;
        getBtn.setButtonText (owned ? "SY1 on whysho.com" : "Get SY1");
    }

    updateView();
    resized();
    repaint();
}

// ---------------------------------------------------------------------------
//  Account actions
// ---------------------------------------------------------------------------
void LicenseDialog::signIn()
{
    if (busy) return;
    const auto mail = emailField.getText().trim();
    const auto password = passwordField.getText();   // lives only in this call and the worker's closure
    if (mail.isEmpty() || password.isEmpty()) { setAccountMessage ("Enter your email and password.", Tone::Warn); return; }

    setBusy (true, "Signing in...");
    launch ([&acct = proc.accountClient(), mail, password] (Control&, auto safe)
    {
        const auto login = acct.login (mail, password);
        const auto check = login.ok ? acct.checkLicense() : lic::AccountClient::Result{};

        post (safe, [login, check] (LicenseDialog& d)
        {
            d.busy = false;
            if (login.ok)
            {
                d.passwordField.clear();
                d.proc.refreshLicense();
                d.setAccountMessage (check.ok ? juce::String() : check.error, check.ok ? Tone::Neutral : Tone::Warn);
            }
            else
            {
                d.setAccountMessage (login.error, Tone::Bad);
            }
            d.refreshStatus();
        });
    });
}

void LicenseDialog::startBrowserSignIn()
{
    if (busy) return;
    setBusy (true, "Contacting whysho.com...");

    launch ([&acct = proc.accountClient()] (Control& ctl, auto safe)
    {
        const auto start = acct.startBrowserLogin();
        if (! start.ok)
        {
            post (safe, [start] (LicenseDialog& d) { d.busy = false; d.setAccountMessage (start.error, Tone::Bad); d.refreshStatus(); });
            return;
        }

        post (safe, [start] (LicenseDialog& d)
        {
            d.view = View::Browser;
            d.browserUrl = start.verificationUrl;
            d.browserDeadlineMs = juce::Time::currentTimeMillis() + (juce::int64) start.expiresInSeconds * 1000;
            d.code.setText (start.userCode, juce::dontSendNotification);
            d.setAccountMessage ({});
            d.timerCallback();
            d.startTimer (1000);
            d.refreshStatus();
            if (d.openUrl) d.openUrl (start.verificationUrl);
        });

        const auto deadline = juce::Time::currentTimeMillis() + (juce::int64) start.expiresInSeconds * 1000;
        auto outcome = lic::AccountClient::Poll::Expired;

        while (juce::Time::currentTimeMillis() < deadline)
        {
            if (! ctl.sleep (start.intervalSeconds * 1000)) return;   // cancelled: the UI already moved on

            outcome = acct.pollBrowserLogin (start.deviceCode);
            if (ctl.isCancelled()) return;
            if (outcome == lic::AccountClient::Poll::Approved || outcome == lic::AccountClient::Poll::Expired) break;

            // Pending, or a hiccup: keep waiting — the code is still good.
            const bool hiccup = outcome == lic::AccountClient::Poll::Error;
            post (safe, [hiccup] (LicenseDialog& d)
            {
                if (d.view == View::Browser)
                    d.setAccountMessage (hiccup ? "Connection problem. Still trying..." : juce::String(), Tone::Warn);
            });
            outcome = lic::AccountClient::Poll::Expired;   // what it means if the deadline passes now
        }

        const bool approved = outcome == lic::AccountClient::Poll::Approved;
        const auto check = approved ? acct.checkLicense() : lic::AccountClient::Result{};

        post (safe, [approved, check] (LicenseDialog& d)
        {
            d.stopTimer();
            d.busy = false;
            d.view = View::SignedOut;   // updateView() promotes this to SignedIn when the session exists
            d.browserUrl.clear();
            if (approved)
            {
                d.proc.refreshLicense();
                d.setAccountMessage (check.ok ? juce::String() : check.error, check.ok ? Tone::Neutral : Tone::Warn);
            }
            else
            {
                d.setAccountMessage ("That code expired before it was approved. Try again.", Tone::Bad);
            }
            d.refreshStatus();
        });
    });
}

void LicenseDialog::cancelBrowserSignIn()
{
    stopTimer();
    stopWorker();
    busy = false;
    view = View::SignedOut;
    browserUrl.clear();
    setAccountMessage ({});
    proc.refreshLicense();   // in case approval landed in the same instant
    refreshStatus();
}

void LicenseDialog::timerCallback()
{
    if (view != View::Browser) { stopTimer(); return; }
    const auto left = juce::jmax ((juce::int64) 0, (browserDeadlineMs - juce::Time::currentTimeMillis()) / 1000);
    codeWait.setText ("Waiting for approval... " + juce::String (left / 60) + ":" + juce::String (left % 60).paddedLeft ('0', 2) + " left",
                      juce::dontSendNotification);
}

void LicenseDialog::refreshAccount (bool quiet)
{
    if (busy) return;
    setBusy (true, quiet ? juce::String() : juce::String ("Checking your account..."));

    launch ([&acct = proc.accountClient(), quiet] (Control&, auto safe)
    {
        const auto check = acct.checkLicense();
        post (safe, [check, quiet] (LicenseDialog& d)
        {
            d.busy = false;
            d.proc.refreshLicense();
            // A quiet check only speaks up when it changed something for the user (signed out).
            if (check.ok)                                   d.setAccountMessage ({});
            else if (! quiet || ! d.proc.accountClient().isSignedIn()) d.setAccountMessage (check.error, Tone::Warn);
            d.refreshStatus();
        });
    });
}

void LicenseDialog::signOut()
{
    if (busy) return;
    setBusy (true, "Signing out...");

    launch ([&acct = proc.accountClient()] (Control&, auto safe)
    {
        acct.logout();   // forgets locally first, then tells the server (best effort)
        post (safe, [] (LicenseDialog& d)
        {
            d.busy = false;
            d.proc.refreshLicense();
            d.setAccountMessage ("Signed out.");
            d.refreshStatus();
        });
    });
}

// ---------------------------------------------------------------------------
//  License key + trial
// ---------------------------------------------------------------------------
void LicenseDialog::activateKey()
{
    if (busy) return;
    const auto key = keyField.getText().trim();
    if (key.length() < 8) { setKeyMessage ("Enter the license key from your purchase email.", Tone::Warn); return; }

    setBusy (true);
    setKeyMessage ("Contacting the license server...");

    launch ([&client = proc.activationClient(), key] (Control&, auto safe)
    {
        const auto res = client.activate (key);
        post (safe, [res] (LicenseDialog& d)
        {
            d.busy = false;
            if (res.ok)
            {
                const auto info = d.proc.licenseManager().verifyToken (res.token);
                if (info.state == lic::LicenseState::Licensed)
                {
                    d.proc.licenseManager().saveToken (res.token);
                    d.proc.refreshLicense();
                    d.setKeyMessage ("Activated. Thank you!", Tone::Good);
                }
                else
                {
                    d.setKeyMessage ("The server's license was rejected: " + info.reason, Tone::Bad);
                }
            }
            else
            {
                d.setKeyMessage (res.error, Tone::Bad);
            }
            d.refreshStatus();
        });
    });
}

void LicenseDialog::deactivateKey()
{
    if (busy) return;
    const auto key = keyField.getText().trim();

    setBusy (true);
    setKeyMessage ("Deactivating...");

    launch ([&client = proc.activationClient(), key] (Control&, auto safe)
    {
        // Freeing the seat needs the key; without it only this computer forgets the license.
        const bool freed = key.isNotEmpty() && client.deactivate (key);
        post (safe, [freed] (LicenseDialog& d)
        {
            d.busy = false;
            d.proc.licenseManager().clearToken();
            d.proc.refreshLicense();
            d.setKeyMessage (freed ? "Deactivated. The seat is free again."
                                   : "Removed from this computer. Enter the key and deactivate to free its seat too.",
                             freed ? Tone::Good : Tone::Warn);
            d.refreshStatus();
        });
    });
}

void LicenseDialog::startTrial()
{
    proc.licenseManager().startOrCheckTrial();
    proc.refreshLicense();
    if (proc.licenseInfo.state == lic::LicenseState::Expired)
        setAccountMessage ("The trial on this computer has already been used.", Tone::Warn);
    refreshStatus();
}

// ---------------------------------------------------------------------------
int LicenseDialog::contentHeight() const
{
    return 2 * kPadY + 28 + 8 + 22 + 18 + 25 + 16 + 8 + kAccountArea + 34 + 19 + 24
           + (keyOpen ? 8 + 32 + 8 + 32 + 4 + 18 : 0) + 14 + 34;
}

void LicenseDialog::paint (juce::Graphics& g)
{
    // The editor may switch theme while we're open.
    if (appliedTheme != theme().name)
        post (this, [] (LicenseDialog& d) { d.applyTheme(); });

    const auto& t = theme();
    g.fillAll (t.bg.withAlpha (0.88f));

    const auto panel = panelBounds.toFloat();
    g.setColour (t.panel);
    g.fillRoundedRectangle (panel, 10.0f);
    g.setColour (t.grid);
    g.drawRoundedRectangle (panel.reduced (0.5f), 10.0f, 1.0f);

    g.setColour (toneColour (statusTone));
    g.fillEllipse (statusDot.toFloat());

    g.setColour (t.grid.withAlpha (0.8f));
    for (auto y : separators)
        g.fillRect (panelBounds.getX() + kPadX, y, panelBounds.getWidth() - 2 * kPadX, 1);

    if (view == View::Browser)
    {
        g.setColour (t.panel2);
        g.fillRoundedRectangle (codeBox.toFloat(), 6.0f);
        g.setColour (t.accent);
        g.drawRoundedRectangle (codeBox.toFloat().reduced (0.5f), 6.0f, 1.0f);
    }
}

void LicenseDialog::resized()
{
    separators.clearQuick();

    const int w = juce::jmax (280, juce::jmin (kPanelWidth, getWidth() - 24));
    const int h = juce::jmin (contentHeight(), juce::jmax (200, getHeight() - 16));
    panelBounds = getLocalBounds().withSizeKeepingCentre (w, h);

    auto r = panelBounds.reduced (kPadX, kPadY);
    const int gap = 8;

    title.setBounds (r.removeFromTop (28));
    r.removeFromTop (8);

    auto statusRow = r.removeFromTop (22);
    statusDot = statusRow.removeFromLeft (18).withSizeKeepingCentre (10, 10).withX (r.getX());
    status.setBounds (statusRow);
    detail.setBounds (r.removeFromTop (18));

    r.removeFromTop (12); separators.add (r.getY()); r.removeFromTop (13);

    accountCaption.setBounds (r.removeFromTop (16));
    r.removeFromTop (8);

    auto area = r.removeFromTop (kAccountArea);
    {
        auto a = area;   // signed out
        emailField.setBounds (a.removeFromTop (32));
        a.removeFromTop (gap);
        passwordField.setBounds (a.removeFromTop (32));
        a.removeFromTop (12);
        auto row = a.removeFromTop (34);
        signInBtn.setBounds (row.removeFromLeft ((row.getWidth() - gap) * 2 / 5));
        row.removeFromLeft (gap);
        browserBtn.setBounds (row);
    }
    {
        auto a = area;   // waiting for the browser
        codeHint.setBounds (a.removeFromTop (18));
        a.removeFromTop (6);
        codeBox = a.removeFromTop (52);
        code.setBounds (codeBox);
        a.removeFromTop (4);
        codeWait.setBounds (a.removeFromTop (18));
        auto row = a.removeFromBottom (34);
        reopenBtn.setBounds (row.removeFromLeft ((row.getWidth() - gap) * 3 / 5));
        row.removeFromLeft (gap);
        cancelBtn.setBounds (row);
    }
    {
        auto a = area;   // signed in
        signedInAs.setBounds (a.removeFromTop (24));
        a.removeFromTop (2);
        ownership.setBounds (a.removeFromTop (20));
        lastChecked.setBounds (a.removeFromTop (20));
        a.removeFromTop (12);
        auto row = a.removeFromTop (34);
        const int bw = juce::jmin (120, (row.getWidth() - 2 * gap) / 3);
        refreshBtn.setBounds (row.removeFromLeft (bw));
        row.removeFromLeft (gap);
        signOutBtn.setBounds (row.removeFromLeft (bw));
        row.removeFromLeft (gap + 8);
        getBtn.setBounds (row);
    }

    r.removeFromTop (6);
    accountMessage.setBounds (r.removeFromTop (28));

    r.removeFromTop (8); separators.add (r.getY()); r.removeFromTop (11);

    keyToggleBtn.setBounds (r.removeFromTop (24).removeFromLeft (200));
    if (keyOpen)
    {
        r.removeFromTop (gap);
        keyField.setBounds (r.removeFromTop (32));
        r.removeFromTop (gap);
        auto row = r.removeFromTop (32);
        activateBtn.setBounds (row.removeFromLeft ((row.getWidth() - gap) / 2));
        row.removeFromLeft (gap);
        deactivateBtn.setBounds (row);
        r.removeFromTop (4);
        keyMessage.setBounds (r.removeFromTop (18));
    }

    auto footer = r.removeFromBottom (34);
    trialBtn.setBounds (footer.removeFromLeft (juce::jmin (190, footer.getWidth() / 2 - gap)));
    closeBtn.setBounds (footer.removeFromRight (juce::jmin (110, footer.getWidth() - gap)));
}
