#include "PluginEditor.h"

// Palette
static const juce::Colour BG      { 0xff0b0b14 };
static const juce::Colour SURFACE { 0xff13131f };
static const juce::Colour BORDER  { 0xff222235 };
static const juce::Colour ACCENT  { 0xff5b8dee };
static const juce::Colour DIM     { 0xff55556e };
static const juce::Colour SUCCESS { 0xff3dba6f };
static const juce::Colour WARN    { 0xffe8a23a };
static const juce::Colour ERR     { 0xffe05555 };

//==============================================================================
void CollabSyncEditor::styleSectionLabel (juce::Label& l, const juce::String& text)
{
    l.setText (text, juce::dontSendNotification);
    l.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    l.setColour (juce::Label::textColourId, DIM);
}

void CollabSyncEditor::styleButton (juce::TextButton& b, juce::Colour fill, juce::Colour text)
{
    b.setColour (juce::TextButton::buttonColourId,   fill);
    b.setColour (juce::TextButton::buttonOnColourId, fill.brighter (0.1f));
    b.setColour (juce::TextButton::textColourOnId,   text);
    b.setColour (juce::TextButton::textColourOffId,  text);
}

//==============================================================================
CollabSyncEditor::CollabSyncEditor (CollabSyncProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setSize (380, 480);

    // ---- Header ----
    titleLabel.setText (juce::String ("CollabSync  ") + COLLABSYNC_VERSION,
                        juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, juce::Colour (0xffeeeef8));
    addAndMakeVisible (titleLabel);

    statusLabel.setFont (juce::FontOptions (11.0f));
    statusLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (statusLabel);

    latencyLabel.setFont (juce::FontOptions (9.5f));
    latencyLabel.setColour (juce::Label::textColourId, DIM);
    latencyLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (latencyLabel);

    // ---- Host a session ----
    styleSectionLabel (sessionSectionLabel, "HOST");
    addAndMakeVisible (sessionSectionLabel);

    sessionStatusLabel.setFont (juce::FontOptions (11.5f));
    sessionStatusLabel.setColour (juce::Label::textColourId, DIM);
    sessionStatusLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (sessionStatusLabel);

    tailscaleIPLabel.setFont (juce::FontOptions (13.0f));
    tailscaleIPLabel.setColour (juce::Label::textColourId, juce::Colour (0xffdde0f0));
    tailscaleIPLabel.setJustificationType (juce::Justification::centredLeft);

    styleButton (startSessionButton, ACCENT);
    styleButton (stopSessionButton,  juce::Colour (0xff1e1e30));
    styleButton (copyIPButton,       juce::Colour (0xff1a2a40));

    addAndMakeVisible (startSessionButton);
    addChildComponent (stopSessionButton);
    addChildComponent (tailscaleIPLabel);
    addChildComponent (copyIPButton);

    startSessionButton.onClick = [this] { proc.startSessionServer(); updateUI(); };
    stopSessionButton.onClick  = [this] { proc.stopSessionServer();  updateUI(); };
    copyIPButton.onClick       = [this] {
        auto ip = proc.getLocalTailscaleIP();
        if (ip.isNotEmpty()) juce::SystemClipboard::copyTextToClipboard (ip);
    };

    // ---- Join a session ----
    hostLabel.setText ("HOST IP", juce::dontSendNotification);
    hostLabel.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    hostLabel.setColour (juce::Label::textColourId, DIM);
    addAndMakeVisible (hostLabel);

    hostInput.setTextToShowWhenEmpty ("Tailscale IP of host", juce::Colour (0xff44445a));
    hostInput.setColour (juce::TextEditor::backgroundColourId,     juce::Colour (0xff111120));
    hostInput.setColour (juce::TextEditor::textColourId,           juce::Colour (0xffdde0f0));
    hostInput.setColour (juce::TextEditor::outlineColourId,        BORDER);
    hostInput.setColour (juce::TextEditor::focusedOutlineColourId, ACCENT);
    hostInput.setFont (juce::FontOptions (12.5f));
    hostInput.setText (proc.signalingHost, juce::dontSendNotification);
    addAndMakeVisible (hostInput);

    styleButton (connectButton,    ACCENT);
    styleButton (disconnectButton, juce::Colour (0xff1e1e30));

    addAndMakeVisible (connectButton);
    addAndMakeVisible (disconnectButton);

    connectButton.onClick    = [this] {
        proc.connect ("SYNC", hostInput.getText().trim());
    };
    disconnectButton.onClick = [this] { proc.disconnect(); };

    // ---- Record ----
    styleButton (recordButton, juce::Colour (0xff8b1a1a));
    addAndMakeVisible (recordButton);
    recordButton.onClick = [this] {
        if (proc.isRecording())        proc.triggerStop();
        else if (! proc.isCountingDown()) proc.triggerRecord();
    };

    // ---- Diagnostics ----
    diagLabel.setFont (juce::FontOptions (9.0f));
    diagLabel.setColour (juce::Label::textColourId, juce::Colour (0xff383850));
    diagLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (diagLabel);

    // ---- Countdown overlay ----
    countdownLabel.setFont (juce::FontOptions (96.0f, juce::Font::bold));
    countdownLabel.setJustificationType (juce::Justification::centred);
    countdownLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    countdownLabel.setInterceptsMouseClicks (false, false);
    addChildComponent (countdownLabel);

    // ---- Session files ----
    styleSectionLabel (filesHeaderLabel, "LAST SESSION  -  drag to playlist");
    addChildComponent (filesHeaderLabel);

    styleButton (showInFinderButton, juce::Colour (0xff161622));
    showInFinderButton.setColour (juce::TextButton::textColourOffId, DIM);
    addChildComponent (showInFinderButton);
    showInFinderButton.onClick = [this] {
        if (proc.lastSessionDir.isDirectory()) proc.lastSessionDir.revealToUser();
    };

    proc.stateListeners.add (this);
    startTimerHz (8);
    updateUI();
}

CollabSyncEditor::~CollabSyncEditor()
{
    proc.stateListeners.remove (this);
    stopTimer();
}

//==============================================================================
void CollabSyncEditor::paint (juce::Graphics& g)
{
    g.fillAll (BG);

    // Header bar
    g.setColour (SURFACE);
    g.fillRect (0, 0, getWidth(), 52);

    // Bottom edge of header
    g.setColour (BORDER);
    g.drawHorizontalLine (52, 0.0f, (float) getWidth());

    // Countdown dim overlay
    if (proc.isCountingDown())
    {
        g.setColour (juce::Colours::black.withAlpha (0.85f));
        g.fillAll();
    }
}

//==============================================================================
void CollabSyncEditor::resized()
{
    const int pad  = 16;
    const int w    = getWidth() - pad * 2;
    int       y    = 0;

    // Header
    titleLabel.setBounds   (pad, 14, 180, 24);
    statusLabel.setBounds  (pad + 180, 10, w - 180, 20);
    latencyLabel.setBounds (pad + 180, 30, w - 180, 14);
    y = 64;

    bool hosting   = proc.isHostingSession();
    bool connected = proc.isConnected();

    // HOST section
    sessionSectionLabel.setBounds (pad, y, w, 14);
    y += 20;

    sessionStatusLabel.setBounds (pad, y, w, 18);
    y += 24;

    startSessionButton.setVisible (! hosting);
    stopSessionButton.setVisible  (hosting);
    tailscaleIPLabel.setVisible   (hosting);
    copyIPButton.setVisible       (hosting);

    if (hosting)
    {
        int ipW = w - 76;
        tailscaleIPLabel.setBounds (pad,           y, ipW, 28);
        copyIPButton.setBounds     (pad + ipW + 6, y, 70,  28);
        y += 34;
        stopSessionButton.setBounds (pad, y, w, 30);
        y += 36;
    }
    else
    {
        startSessionButton.setBounds (pad, y, w, 30);
        y += 36;
    }

    // Divider
    y += 4;

    // JOIN section — label acts as a visual separator
    hostLabel.setBounds (pad, y, w, 14);
    y += 20;

    hostInput.setBounds (pad, y, w, 32);
    y += 38;

    int btnW = (w - 8) / 2;
    connectButton.setBounds    (pad,           y, btnW, 30);
    disconnectButton.setBounds (pad + btnW + 8, y, btnW, 30);
    y += 38;

    // Record
    y += 4;
    recordButton.setBounds (pad, y, w, 38);
    y += 46;

    // Diagnostics
    diagLabel.setBounds (pad, y, w, 13);
    y += 20;

    // Session files
    bool hasSession = proc.lastSessionDir.isDirectory();
    filesHeaderLabel.setVisible (hasSession);
    showInFinderButton.setVisible (hasSession);

    if (hasSession)
    {
        filesHeaderLabel.setBounds (pad, y, w, 14);
        y += 20;

        int tileW = (w - 8) / 2;
        for (int i = 0; i < 4; ++i)
        {
            if (fileTiles[i])
            {
                fileTiles[i]->setBounds (pad + (i % 2) * (tileW + 8),
                                         y   + (i / 2) * 34,
                                         tileW, 28);
                fileTiles[i]->setVisible (true);
            }
        }
        y += 74;
        showInFinderButton.setBounds (pad, y, w, 26);
        y += 32;
    }
    else
    {
        for (auto& t : fileTiles)
            if (t) t->setVisible (false);
    }

    // Resize window to fit content
    int needed = y + 8;
    if (needed != getHeight())
        setSize (getWidth(), needed);

    // Countdown covers everything
    countdownLabel.setBounds (getLocalBounds());

    juce::ignoreUnused (connected);
}

//==============================================================================
void CollabSyncEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateUI();
}

void CollabSyncEditor::timerCallback()
{
    if (proc.isConnected())
    {
        latencyLabel.setText (juce::String (proc.getLatencyMs(), 1) + " ms",
                              juce::dontSendNotification);

        int bufLevel = proc.recvBufferLevel.load (std::memory_order_relaxed);
        int bufMs    = (int) (bufLevel / 2.0 / proc.getSampleRate() * 1000.0);
        diagLabel.setText (
            "buf " + juce::String (bufMs) + "ms  "
            + "pkt " + juce::String (proc.packetsReceived.load()) + "  "
            + "underruns " + juce::String (proc.bufferUnderruns.load()),
            juce::dontSendNotification);
    }
    else
    {
        latencyLabel.setText ({}, juce::dontSendNotification);
        diagLabel.setText    ({}, juce::dontSendNotification);
    }

    bool counting = proc.isCountingDown();
    countdownLabel.setVisible (counting);
    if (counting)
    {
        countdownLabel.setText (juce::String (proc.getCountdownBeat()),
                                juce::dontSendNotification);
        repaint();
    }

    updateUI();
}

//==============================================================================
void CollabSyncEditor::updateUI()
{
    bool connected    = proc.isConnected();
    bool recording    = proc.isRecording();
    bool countingDown = proc.isCountingDown();
    bool hosting      = proc.isHostingSession();
    bool hasSession   = proc.lastSessionDir.isDirectory();

    // Main status (header)
    juce::String statusText;
    juce::Colour statusCol = DIM;

    if      (connected && recording)    { statusText = "Recording";    statusCol = ERR;     }
    else if (connected && countingDown) { statusText = "Get ready..."; statusCol = WARN;    }
    else if (connected)                 { statusText = "Connected";    statusCol = SUCCESS;  }
    else if (proc.currentStatus.startsWith ("ERROR")) { statusText = proc.currentStatus; statusCol = ERR;  }
    else if (proc.currentStatus.startsWith ("Wait"))  { statusText = proc.currentStatus; statusCol = WARN; }
    else if (proc.currentStatus.isNotEmpty())         { statusText = proc.currentStatus; }
    else                                               { statusText = "Not connected";   }

    statusLabel.setText   (statusText, juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, statusCol);

    // Buttons
    connectButton.setEnabled    (! connected && ! countingDown && ! hosting);
    disconnectButton.setEnabled (connected && ! recording && ! countingDown);
    recordButton.setEnabled     (connected && ! countingDown);
    startSessionButton.setEnabled (! connected && ! hosting);

    recordButton.setButtonText (recording ? "Stop" : "Record");
    styleButton (recordButton, recording ? juce::Colour (0xff6a1010) : juce::Colour (0xff8b1a1a));

    // Session hosting status
    auto    sessionErr = proc.getSessionErrorMessage();
    auto    tsIP       = proc.getLocalTailscaleIP();
    bool    hasTSIP    = tsIP.isNotEmpty();

    if (! sessionErr.isEmpty())
    {
        sessionStatusLabel.setText  (sessionErr, juce::dontSendNotification);
        sessionStatusLabel.setColour (juce::Label::textColourId, ERR);
    }
    else if (hosting && connected)
    {
        sessionStatusLabel.setText  ("Friend connected", juce::dontSendNotification);
        sessionStatusLabel.setColour (juce::Label::textColourId, SUCCESS);
        tailscaleIPLabel.setText    (hasTSIP ? tsIP : "No Tailscale IP", juce::dontSendNotification);
        tailscaleIPLabel.setColour  (juce::Label::textColourId, hasTSIP ? juce::Colour (0xffdde0f0) : ERR);
        copyIPButton.setEnabled (hasTSIP);
    }
    else if (hosting)
    {
        sessionStatusLabel.setText  ("Hosting - waiting for friend", juce::dontSendNotification);
        sessionStatusLabel.setColour (juce::Label::textColourId, WARN);
        tailscaleIPLabel.setText    (hasTSIP ? tsIP : "No Tailscale IP", juce::dontSendNotification);
        tailscaleIPLabel.setColour  (juce::Label::textColourId, hasTSIP ? juce::Colour (0xffdde0f0) : ERR);
        copyIPButton.setEnabled (hasTSIP);
    }
    else
    {
        sessionStatusLabel.setText  (hasTSIP ? "Start a session for your friend to join"
                                             : "Tailscale required - see README",
                                     juce::dontSendNotification);
        sessionStatusLabel.setColour (juce::Label::textColourId, hasTSIP ? DIM : ERR);
    }

    if (hasSession) rebuildFileTiles();
    resized();
    repaint();
}

//==============================================================================
void CollabSyncEditor::rebuildFileTiles()
{
    static const char* names[]  = { "local.wav", "remote.wav", "local.mid", "remote.mid" };

    for (int i = 0; i < 4; ++i)
    {
        auto file = proc.lastSessionDir.getChildFile (names[i]);
        if (file.existsAsFile() && (! fileTiles[i] || fileTiles[i]->getName() != names[i]))
        {
            fileTiles[i] = std::make_unique<FileTile> (file, names[i]);
            fileTiles[i]->setName (names[i]);
            addAndMakeVisible (*fileTiles[i]);
        }
    }
}
