#include "PluginEditor.h"

static const juce::Colour BG1    { 0xff0d0d1a };
static const juce::Colour BG2    { 0xff161625 };
static const juce::Colour PANEL  { 0xff1a1a2e };
static const juce::Colour ACCENT { 0xff4a90d9 };
static const juce::Colour DIV    { 0xff252538 };

//==============================================================================
void CollabSyncEditor::styleSectionLabel (juce::Label& l, const juce::String& text)
{
    l.setText (text, juce::dontSendNotification);
    l.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    l.setColour (juce::Label::textColourId, juce::Colour (0xff8888aa));
}

void CollabSyncEditor::styleButton (juce::TextButton& b, juce::Colour col)
{
    b.setColour (juce::TextButton::buttonColourId,  col);
    b.setColour (juce::TextButton::textColourOnId,  juce::Colours::white);
    b.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
}

//==============================================================================
CollabSyncEditor::CollabSyncEditor (CollabSyncProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setSize (380, 680);

    // ---- Header ----
    titleLabel.setText (juce::String ("CollabSync v") + COLLABSYNC_VERSION, juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (titleLabel);

    statusLabel.setFont (juce::FontOptions (11.0f));
    statusLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (statusLabel);

    latencyLabel.setFont (juce::FontOptions (10.0f));
    latencyLabel.setColour (juce::Label::textColourId, juce::Colour (0xff666688));
    latencyLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (latencyLabel);

    // ---- Room row ----
    roomLabel.setText ("Room", juce::dontSendNotification);
    roomLabel.setFont (juce::FontOptions (11.0f));
    roomLabel.setColour (juce::Label::textColourId, juce::Colour (0xff8888aa));
    addAndMakeVisible (roomLabel);

    roomInput.setTextToShowWhenEmpty ("room code", juce::Colours::darkgrey);
    roomInput.setInputRestrictions (8, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789");
    roomInput.setColour (juce::TextEditor::backgroundColourId,     juce::Colour (0xff1e1e35));
    roomInput.setColour (juce::TextEditor::textColourId,           juce::Colours::white);
    roomInput.setColour (juce::TextEditor::outlineColourId,        DIV);
    roomInput.setColour (juce::TextEditor::focusedOutlineColourId, ACCENT);
    roomInput.setFont (juce::FontOptions (13.0f));
    addAndMakeVisible (roomInput);

    hostLabel.setText ("Host", juce::dontSendNotification);
    hostLabel.setFont (juce::FontOptions (11.0f));
    hostLabel.setColour (juce::Label::textColourId, juce::Colour (0xff8888aa));
    addAndMakeVisible (hostLabel);

    hostInput.setTextToShowWhenEmpty ("Tailscale IP", juce::Colours::darkgrey);
    hostInput.setColour (juce::TextEditor::backgroundColourId,     juce::Colour (0xff1e1e35));
    hostInput.setColour (juce::TextEditor::textColourId,           juce::Colours::white);
    hostInput.setColour (juce::TextEditor::outlineColourId,        DIV);
    hostInput.setColour (juce::TextEditor::focusedOutlineColourId, ACCENT);
    hostInput.setFont (juce::FontOptions (12.0f));
    hostInput.setText (proc.signalingHost, juce::dontSendNotification);
    addAndMakeVisible (hostInput);

    // ---- Buttons ----
    styleButton (connectButton,      ACCENT);
    styleButton (disconnectButton,   juce::Colour (0xff383848));
    styleButton (recordButton,       juce::Colour (0xffbb2222));
    styleButton (showInFinderButton, juce::Colour (0xff2a3a4a));

    addAndMakeVisible (connectButton);
    addAndMakeVisible (disconnectButton);
    addAndMakeVisible (recordButton);
    addChildComponent (showInFinderButton);

    connectButton.onClick    = [this] {
        auto code = roomInput.getText().trim().toUpperCase();
        auto host = hostInput.getText().trim();
        if (code.isNotEmpty()) proc.connect (code, host);
    };
    disconnectButton.onClick = [this] { proc.disconnect(); };
    recordButton.onClick     = [this] {
        if (proc.isRecording())
            proc.triggerStop();
        else if (! proc.isCountingDown())
            proc.triggerRecord();
    };
    showInFinderButton.onClick = [this] {
        if (proc.lastSessionDir.isDirectory())
            proc.lastSessionDir.revealToUser();
    };

    // ---- Session hosting ----
    styleSectionLabel (sessionSectionLabel, "HOST A SESSION");
    addAndMakeVisible (sessionSectionLabel);

    styleButton (startSessionButton, juce::Colour (0xff2a6a3a));
    styleButton (stopSessionButton,  juce::Colour (0xff383848));
    styleButton (copyIPButton,       juce::Colour (0xff2a3a4a));
    addAndMakeVisible (startSessionButton);
    addChildComponent (stopSessionButton);
    addChildComponent (tailscaleIPLabel);
    addChildComponent (copyIPButton);

    sessionStatusLabel.setFont (juce::FontOptions (11.0f));
    sessionStatusLabel.setColour (juce::Label::textColourId, juce::Colour (0xff8888aa));
    sessionStatusLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (sessionStatusLabel);

    tailscaleIPLabel.setFont (juce::FontOptions (12.0f));
    tailscaleIPLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    tailscaleIPLabel.setJustificationType (juce::Justification::centredLeft);

    startSessionButton.onClick = [this] { proc.startSessionServer(); updateUI(); };
    stopSessionButton.onClick  = [this] { proc.stopSessionServer();  updateUI(); };
    copyIPButton.onClick       = [this] {
        auto ip = proc.getSessionTailscaleIP();
        if (ip.isNotEmpty())
            juce::SystemClipboard::copyTextToClipboard (ip);
    };

    // ---- Device section labels ----
    styleSectionLabel (midiSectionLabel,         "MIDI DEVICES");
    styleSectionLabel (audioInputSectionLabel,    "AUDIO INPUT");
    styleSectionLabel (audioOutputSectionLabel,   "AUDIO OUTPUT");
    addAndMakeVisible (midiSectionLabel);
    addAndMakeVisible (audioInputSectionLabel);
    addAndMakeVisible (audioOutputSectionLabel);

    midiEmptyLabel.setText ("No MIDI devices detected", juce::dontSendNotification);
    midiEmptyLabel.setFont (juce::FontOptions (11.0f));
    midiEmptyLabel.setColour (juce::Label::textColourId, juce::Colour (0xff555568));
    midiEmptyLabel.setJustificationType (juce::Justification::centredLeft);

    audioInputEmptyLabel.setText ("No audio inputs detected", juce::dontSendNotification);
    audioInputEmptyLabel.setFont (juce::FontOptions (11.0f));
    audioInputEmptyLabel.setColour (juce::Label::textColourId, juce::Colour (0xff555568));
    audioInputEmptyLabel.setJustificationType (juce::Justification::centredLeft);

    audioOutputEmptyLabel.setText ("No audio outputs detected", juce::dontSendNotification);
    audioOutputEmptyLabel.setFont (juce::FontOptions (11.0f));
    audioOutputEmptyLabel.setColour (juce::Label::textColourId, juce::Colour (0xff555568));
    audioOutputEmptyLabel.setJustificationType (juce::Justification::centredLeft);

    // ---- Diagnostics ----
    diagLabel.setFont (juce::FontOptions (9.5f));
    diagLabel.setColour (juce::Label::textColourId, juce::Colour (0xff666688));
    diagLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (diagLabel);

    // ---- Countdown overlay ----
    countdownLabel.setFont (juce::FontOptions (90.0f, juce::Font::bold));
    countdownLabel.setJustificationType (juce::Justification::centred);
    countdownLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    countdownLabel.setInterceptsMouseClicks (false, false);
    addChildComponent (countdownLabel);

    // ---- Files ----
    filesHeaderLabel.setText ("Last Session — drag to playlist",
                              juce::dontSendNotification);
    filesHeaderLabel.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    filesHeaderLabel.setColour (juce::Label::textColourId, juce::Colour (0xff8888aa));
    addChildComponent (filesHeaderLabel);
    addChildComponent (showInFinderButton);

    proc.stateListeners.add (this);
    startTimerHz (8);
    refreshDevices();
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
    g.fillAll (BG1);

    // Header bar
    g.setColour (BG2);
    g.fillRect (0, 0, getWidth(), 48);

    // Draw subtle panel background behind each device section
    auto paintSection = [&] (int y, int h) {
        g.setColour (PANEL);
        g.fillRect (12, y, getWidth() - 24, h);
        g.setColour (DIV);
        g.drawRect (12, y, getWidth() - 24, h, 1);
    };

    // We'll rely on resized() positions — just draw full-width dividers
    g.setColour (DIV);
    for (int lineY : { 48, 104, 148 })
        g.drawHorizontalLine (lineY, 0.0f, (float) getWidth());

    // Countdown overlay
    if (proc.isCountingDown())
    {
        g.setColour (juce::Colours::black.withAlpha (0.82f));
        g.fillAll();
    }

    juce::ignoreUnused (paintSection);
}

//==============================================================================
int CollabSyncEditor::layoutDeviceSection (juce::OwnedArray<DeviceRow>& rows,
                                            juce::Label& emptyLabel, int y)
{
    int rowH = 24;
    if (rows.isEmpty())
    {
        emptyLabel.setBounds (20, y, getWidth() - 40, rowH);
        addAndMakeVisible (emptyLabel);
        y += rowH + 4;
    }
    else
    {
        emptyLabel.setVisible (false);
        for (auto* row : rows)
        {
            row->setBounds (12, y, getWidth() - 24, rowH);
            addAndMakeVisible (*row);
            y += rowH + 2;
        }
    }
    return y;
}

void CollabSyncEditor::resized()
{
    int x = 12;
    int w = getWidth() - 24;

    // Header
    titleLabel.setBounds  (x, 12, 130, 24);
    statusLabel.setBounds (x + 130, 8,  w - 130, 22);
    latencyLabel.setBounds(x + 130, 28, w - 130, 16);

    int y = 56;

    // Host row
    hostLabel.setBounds (x, y, 38, 28);
    hostInput.setBounds (x + 42, y, w - 42, 28);
    y += 34;

    // Room row
    roomLabel.setBounds (x, y, 38, 28);
    disconnectButton.setBounds (x + w - 84,  y, 84, 28);
    connectButton.setBounds    (x + w - 172, y, 84, 28);
    roomInput.setBounds        (x + 42, y, w - 42 - 176, 28);
    y += 34;

    // Record button
    recordButton.setBounds (x, y, w, 34);
    y += 38;

    // Session hosting
    sessionSectionLabel.setBounds (x, y, w, 16);
    y += 20;

    bool hosting = proc.isHostingSession();

    startSessionButton.setVisible (! hosting);
    stopSessionButton.setVisible  (hosting);
    tailscaleIPLabel.setVisible   (hosting);
    copyIPButton.setVisible       (hosting);

    if (hosting)
    {
        sessionStatusLabel.setBounds (x, y, w, 18);
        y += 22;

        int ipW = w - 72;
        tailscaleIPLabel.setBounds (x,        y, ipW, 26);
        copyIPButton.setBounds     (x + ipW + 4, y, 68, 26);
        y += 30;
        stopSessionButton.setBounds (x, y, w, 28);
        y += 32;
    }
    else
    {
        sessionStatusLabel.setBounds (x, y, w, 18);
        y += 22;
        startSessionButton.setBounds (x, y, w, 28);
        y += 32;
    }

    // Diagnostics
    diagLabel.setBounds (x, y, w, 14);
    y += 18;

    // MIDI devices
    midiSectionLabel.setBounds (x, y, w, 16);
    y += 20;
    y = layoutDeviceSection (midiRows, midiEmptyLabel, y);
    y += 6;

    // Audio input devices
    audioInputSectionLabel.setBounds (x, y, w, 16);
    y += 20;
    y = layoutDeviceSection (audioInputRows, audioInputEmptyLabel, y);
    y += 6;

    // Audio output devices
    audioOutputSectionLabel.setBounds (x, y, w, 16);
    y += 20;
    y = layoutDeviceSection (audioOutputRows, audioOutputEmptyLabel, y);
    y += 6;

    // Session files
    bool hasSession = proc.lastSessionDir.isDirectory();
    filesHeaderLabel.setVisible (hasSession);
    showInFinderButton.setVisible (hasSession);

    if (hasSession)
    {
        filesHeaderLabel.setBounds (x, y, w, 16);
        y += 20;

        int tileW = (w - 4) / 2;
        for (int i = 0; i < 4; ++i)
        {
            if (fileTiles[i])
            {
                fileTiles[i]->setBounds (x + (i % 2) * (tileW + 4),
                                         y + (i / 2) * 32,
                                         tileW, 28);
                fileTiles[i]->setVisible (true);
            }
        }
        y += 68;
        showInFinderButton.setBounds (x, y, w, 26);
        y += 30;
    }
    else
    {
        for (auto& t : fileTiles)
            if (t) t->setVisible (false);
    }

    // Countdown full-cover
    countdownLabel.setBounds (getLocalBounds());
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

        // Diagnostics
        int bufLevel = proc.recvBufferLevel.load (std::memory_order_relaxed);
        int bufMs    = (int) (bufLevel / 2.0 / proc.getSampleRate() * 1000.0);
        juce::String diag = "pkt:" + juce::String (proc.packetsReceived.load())
            + "  dec:" + juce::String (proc.decodeSuccesses.load())
            + "/" + juce::String (proc.decodeFailures.load())
            + "  buf:" + juce::String (bufMs) + "ms"
            + "  underruns:" + juce::String (proc.bufferUnderruns.load());
        diagLabel.setText (diag, juce::dontSendNotification);
    }
    else
    {
        latencyLabel.setText ({}, juce::dontSendNotification);
        diagLabel.setText ({}, juce::dontSendNotification);
    }

    bool counting = proc.isCountingDown();
    countdownLabel.setVisible (counting);
    if (counting)
    {
        countdownLabel.setText (juce::String (proc.getCountdownBeat()),
                                juce::dontSendNotification);
        repaint();
    }

    // Refresh session hosting status every tick (peer count can change any time)
    if (proc.isHostingSession() || ! proc.getSessionErrorMessage().isEmpty())
        updateUI();

    static int devTick = 0;
    if (++devTick >= 32) { devTick = 0; refreshDevices(); }
}

//==============================================================================
void CollabSyncEditor::updateUI()
{
    bool connected    = proc.isConnected();
    bool recording    = proc.isRecording();
    bool countingDown = proc.isCountingDown();
    bool hasSession   = proc.lastSessionDir.isDirectory();

    juce::String statusText;
    juce::Colour statusCol = juce::Colour (0xff666688);

    if      (connected && recording)   { statusText = "● Recording";    statusCol = juce::Colours::red; }
    else if (connected && countingDown){ statusText = "Get ready...";   statusCol = juce::Colours::orange; }
    else if (connected)                { statusText = "● Connected";    statusCol = juce::Colours::limegreen; }
    else if (proc.currentStatus.startsWith ("ERROR")) { statusText = proc.currentStatus; statusCol = juce::Colours::orangered; }
    else if (proc.currentStatus.startsWith ("Wait"))  { statusText = proc.currentStatus; statusCol = juce::Colours::orange; }
    else if (proc.currentStatus.isNotEmpty())         { statusText = proc.currentStatus; }
    else                                               { statusText = "Not connected"; }

    statusLabel.setText  (statusText, juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, statusCol);

    connectButton.setEnabled    (! connected && ! countingDown);
    disconnectButton.setEnabled (connected && ! recording && ! countingDown);
    recordButton.setEnabled     (connected && ! countingDown);

    recordButton.setButtonText (recording ? "Stop" : "Record");
    styleButton (recordButton, recording ? juce::Colour (0xff881111) : juce::Colour (0xffbb2222));

    // Session hosting status
    bool hosting = proc.isHostingSession();
    auto sessionErr = proc.getSessionErrorMessage();

    if (! proc.getSessionErrorMessage().isEmpty())
    {
        sessionStatusLabel.setText (sessionErr.isEmpty() ? "Error starting server" : sessionErr,
                                    juce::dontSendNotification);
        sessionStatusLabel.setColour (juce::Label::textColourId, juce::Colours::orangered);
    }
    else if (hosting)
    {
        int joined = proc.getSessionPeerCount();
        juce::String statusText = joined == 0 ? "Waiting for peer..."
                                : joined == 1 ? "● Peer connected"
                                              : "● Session active";
        juce::Colour statusCol  = joined == 0 ? juce::Colour (0xff8888aa)
                                : joined >= 1 ? juce::Colours::limegreen
                                              : juce::Colours::limegreen;

        sessionStatusLabel.setText  (statusText, juce::dontSendNotification);
        sessionStatusLabel.setColour (juce::Label::textColourId, statusCol);

        auto ip = proc.getSessionTailscaleIP();
        tailscaleIPLabel.setText (ip.isEmpty() ? "Tailscale not detected" : ip,
                                  juce::dontSendNotification);
        tailscaleIPLabel.setColour (juce::Label::textColourId,
                                    ip.isEmpty() ? juce::Colours::orangered
                                                 : juce::Colours::white);
        copyIPButton.setEnabled (ip.isNotEmpty());
    }
    else
    {
        auto ip = proc.getSessionTailscaleIP();
        sessionStatusLabel.setText (ip.isEmpty() ? "Install Tailscale to host sessions"
                                                 : "Start a session for your friend to join",
                                    juce::dontSendNotification);
        sessionStatusLabel.setColour (juce::Label::textColourId,
                                      ip.isEmpty() ? juce::Colours::orangered
                                                   : juce::Colour (0xff8888aa));
        startSessionButton.setEnabled (true);
    }

    if (hasSession) rebuildFileTiles();
    resized();
    repaint();
}

//==============================================================================
void CollabSyncEditor::refreshDevices()
{
    // --- MIDI ---
    auto midiDevices = juce::MidiInput::getAvailableDevices();
    bool midiChanged = (midiDevices.size() != midiRows.size());
    if (! midiChanged)
        for (int i = 0; i < midiDevices.size(); ++i)
            if (midiDevices[i].name != midiRows[i]->deviceName) { midiChanged = true; break; }

    if (midiChanged)
    {
        midiRows.clear();
        for (auto& d : midiDevices)
            midiRows.add (new DeviceRow (d.name, DeviceRow::Status::available));
    }

    // --- Audio (separate input/output) ---
    std::unique_ptr<juce::AudioIODeviceType> coreAudio (
        juce::AudioIODeviceType::createAudioIODeviceType_CoreAudio());

    juce::StringArray inputNames, outputNames;
    if (coreAudio)
    {
        coreAudio->scanForDevices();
        inputNames  = coreAudio->getDeviceNames (true);   // input devices
        outputNames = coreAudio->getDeviceNames (false);   // output devices
    }

    // Determine which device is active (if running standalone with a device manager).
    // In plugin mode the DAW manages devices, so all show as "available".
    juce::String activeInputName, activeOutputName;
    bool isStandalone = (proc.wrapperType == juce::AudioProcessor::WrapperType::wrapperType_Standalone);

    // Note: In plugin mode (VST3/AU), the DAW manages devices.
    // Active device detection is only possible in standalone mode, which
    // would require StandalonePluginHolder access. For now, all detected
    // devices show as "available" (blue). The active device in your DAW
    // is whatever you've selected in the DAW's audio/MIDI settings.
    juce::ignoreUnused (isStandalone, activeInputName, activeOutputName);

    // Rebuild audio input rows
    bool inputChanged = (inputNames.size() != audioInputRows.size());
    if (! inputChanged)
        for (int i = 0; i < inputNames.size(); ++i)
            if (inputNames[i] != audioInputRows[i]->deviceName) { inputChanged = true; break; }

    if (inputChanged)
    {
        audioInputRows.clear();
        for (auto& name : inputNames)
        {
            auto status = (name == activeInputName) ? DeviceRow::Status::active
                                                    : DeviceRow::Status::available;
            audioInputRows.add (new DeviceRow (name, status));
        }
    }
    else if (! activeInputName.isEmpty())
    {
        // Update active status on existing rows
        for (auto* row : audioInputRows)
            row->status = (row->deviceName == activeInputName) ? DeviceRow::Status::active
                                                               : DeviceRow::Status::available;
    }

    // Rebuild audio output rows
    bool outputChanged = (outputNames.size() != audioOutputRows.size());
    if (! outputChanged)
        for (int i = 0; i < outputNames.size(); ++i)
            if (outputNames[i] != audioOutputRows[i]->deviceName) { outputChanged = true; break; }

    if (outputChanged)
    {
        audioOutputRows.clear();
        for (auto& name : outputNames)
        {
            auto status = (name == activeOutputName) ? DeviceRow::Status::active
                                                     : DeviceRow::Status::available;
            audioOutputRows.add (new DeviceRow (name, status));
        }
    }
    else if (! activeOutputName.isEmpty())
    {
        for (auto* row : audioOutputRows)
            row->status = (row->deviceName == activeOutputName) ? DeviceRow::Status::active
                                                                : DeviceRow::Status::available;
    }

    if (midiChanged || inputChanged || outputChanged)
    {
        resized();
        repaint();
    }
}

//==============================================================================
void CollabSyncEditor::rebuildFileTiles()
{
    static const char* names[]  = { "local.wav",  "remote.wav",  "local.mid",  "remote.mid" };
    static const char* labels[] = { "local.wav",  "remote.wav",  "local.mid",  "remote.mid" };

    for (int i = 0; i < 4; ++i)
    {
        auto file = proc.lastSessionDir.getChildFile (names[i]);
        if (file.existsAsFile() && (! fileTiles[i] || fileTiles[i]->getName() != names[i]))
        {
            fileTiles[i] = std::make_unique<FileTile> (file, labels[i]);
            fileTiles[i]->setName (names[i]);
            addAndMakeVisible (*fileTiles[i]);
        }
    }
}
