#include "PluginEditor.h"
#include <array>

//==============================================================================
CollabSyncEditor::CollabSyncEditor (CollabSyncProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    setResizable (true, true);

    // ---- Header ----
    titleLabel.setText ("CollabSync", juce::dontSendNotification);
    titleLabel.setColour (juce::Label::textColourId, CST::cream);
    titleLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (titleLabel);

    versionLabel.setText (COLLABSYNC_VERSION, juce::dontSendNotification);
    versionLabel.setColour (juce::Label::textColourId, CST::textMuted34);
    versionLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (versionLabel);

    addAndMakeVisible (connectionStatus);

    // ---- Session section ----
    addAndMakeVisible (sessionSectionLabel);
    addAndMakeVisible (sessionPeerStatus);

    // fromUTF8 + escapes for anything non-ASCII: juce::String(const char*) reads
    // bytes above 127 as Latin-1, so a literal em dash renders as mojibake.
    idleHelperLabel.setText (juce::String::fromUTF8 ("Hit Join \xE2\x80\x94 your friend hits Join too."),
                              juce::dontSendNotification);
    idleHelperLabel.setFont (lnf.sans (13.0f, 400));
    idleHelperLabel.setColour (juce::Label::textColourId, CST::textMuted42);
    idleHelperLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (idleHelperLabel);

    addAndMakeVisible (joinButton);
    addAndMakeVisible (sessionReadout);
    addAndMakeVisible (leaveButton);

    joinButton.onClick  = [this] { proc.joinSession();  updateUI(); };
    leaveButton.onClick = [this] { proc.leaveSession(); updateUI(); };

    // ---- Record ----
    addAndMakeVisible (recordButton);
    recordButton.onClick = [this]
    {
        if (proc.isRecording())            proc.triggerStop();
        else if (! proc.isCountingDown())  proc.triggerRecord();
    };

    // ---- Status strip ----
    addAndMakeVisible (indicatorStrip);

    // ---- Generated files ----
    addAndMakeVisible (filesSectionLabel);

    dragHintLabel.setText (juce::String::fromUTF8 ("drag any file into your DAW \xE2\x86\x97"),
                            juce::dontSendNotification);
    dragHintLabel.setFont (lnf.mono (10.0f, 400).withExtraKerningFactor (0.1f));
    dragHintLabel.setColour (juce::Label::textColourId, CST::textMuted32);
    dragHintLabel.setJustificationType (juce::Justification::centredRight);
    dragHintLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (dragHintLabel);

    // ---- Countdown overlay ----
    countdownLabel.setFont (lnf.sans (96.0f, 700));
    countdownLabel.setJustificationType (juce::Justification::centred);
    countdownLabel.setColour (juce::Label::textColourId, CST::cream);
    countdownLabel.setInterceptsMouseClicks (false, false);
    addChildComponent (countdownLabel);
    countdownLabel.toFront (false);

    proc.stateListeners.add (this);
    setSize (CST::widthStandard, 600);
    startTimerHz (20);
    updateUI();
}

CollabSyncEditor::~CollabSyncEditor()
{
    proc.stateListeners.remove (this);
    stopTimer();
    setLookAndFeel (nullptr);
}

//==============================================================================
void CollabSyncEditor::paintPanelBackground (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();

    float cx = b.getX() + b.getWidth() * 0.30f;
    float cy = b.getY() + b.getHeight() * 0.12f;

    float maxDist = 0.0f;
    for (auto corner : { b.getTopLeft(), b.getTopRight(), b.getBottomLeft(), b.getBottomRight() })
        maxDist = juce::jmax (maxDist, juce::Point<float> (cx, cy).getDistanceFrom (corner));
    maxDist = juce::jmax (1.0f, maxDist);

    juce::ColourGradient grad (CST::panelGradA, cx, cy, CST::panelGradC, cx + maxDist, cy, true);
    grad.addColour (0.55, CST::panelGradB);
    g.setGradientFill (grad);
    g.fillRect (b);

    // Panel edge bevel — "inset 6px 6px 16px rgba(0,0,0,.55)" plus
    // "inset -6px -6px 16px rgba(120,220,175,.04)". These hug the window border;
    // spreading them across the panel (as a linear vignette does) washes out the
    // radial gradient above and flattens the whole surface to one dead green.
    {
        juce::Graphics::ScopedSaveState save (g);

        juce::Path panel;
        panel.addRectangle (b);
        g.reduceClipRegion (panel);

        CollabSyncLookAndFeel::paintInsetShadow (g, panel, { 6.0f, 6.0f }, 16.0f,
                                                  juce::Colours::black.withAlpha (0.55f));
        CollabSyncLookAndFeel::paintInsetShadow (g, panel, { -6.0f, -6.0f }, 16.0f,
                                                  juce::Colour (0xff78dcaf).withAlpha (0.04f));
    }
}

void CollabSyncEditor::paintDividers (juce::Graphics& g)
{
    const int pad = CST::panelPadding;
    int contentW = juce::jmax (0, getWidth() - pad * 2);

    for (int dy : dividerYs)
    {
        juce::ColourGradient grad (juce::Colours::transparentBlack, (float) pad, (float) dy,
                                    juce::Colours::transparentBlack, (float) (pad + contentW), (float) dy, false);
        grad.addColour (0.2, juce::Colour (0xff000a07).withAlpha (0.6f));
        grad.addColour (0.8, CST::mint.withAlpha (0.05f));
        g.setGradientFill (grad);
        g.fillRect (pad, dy, contentW, 1);
    }
}

void CollabSyncEditor::paint (juce::Graphics& g)
{
    paintPanelBackground (g);
    paintDividers (g);

    if (proc.isCountingDown())
    {
        g.setColour (juce::Colours::black.withAlpha (0.85f));
        g.fillAll();
    }
}

//==============================================================================
void CollabSyncEditor::layoutGeneratedFilesGrid (int pad, int contentW, int& y)
{
    std::vector<juce::Component*> cards;
    if (localAudioCard)  cards.push_back (localAudioCard.get());
    if (localMidiCard)   cards.push_back (localMidiCard.get());
    if (remoteAudioCard) cards.push_back (remoteAudioCard.get());
    if (remoteMidiCard)  cards.push_back (remoteMidiCard.get());

    if (cards.empty())
        return;

    const int columns = contentW < CST::gridCollapseWidth ? 1 : 2;
    const int gap = CST::gridGap;
    const int colW = columns == 1 ? contentW : (contentW - gap) / 2;
    const int cardH = 120;

    for (size_t i = 0; i < cards.size(); ++i)
    {
        int col = (int) (i % (size_t) columns);
        int row = (int) (i / (size_t) columns);
        int x = pad + col * (colW + gap);
        int cardY = y + row * (cardH + gap);
        cards[i]->setBounds (CST::expandedForShadow ({ x, cardY, colW, cardH }));
        cards[i]->setVisible (true);
    }

    int rows = (int) ((cards.size() + (size_t) columns - 1) / (size_t) columns);
    y += rows * cardH + (rows - 1) * gap;
}

//==============================================================================
void CollabSyncEditor::resized()
{
    const int pad = CST::panelPadding;
    int contentW = juce::jmax (100, getWidth() - pad * 2);
    int y = pad;
    dividerYs.clear();

    auto placeDivider = [&]
    {
        y += CST::dividerMargin;
        dividerYs.push_back (y);
        y += 1;
        y += CST::dividerMargin;
    };

    // ---- Header ----
    {
        auto titleFont = lnf.sans (22.0f, 700).withExtraKerningFactor (-0.02f);
        titleLabel.setFont (titleFont);
        int titleW = titleFont.getStringWidth (titleLabel.getText()) + 4;
        titleLabel.setBounds (pad, y, titleW, 28);

        auto versionFont = lnf.mono (12.0f, 400);
        versionLabel.setFont (versionFont);
        int versionW = versionFont.getStringWidth (versionLabel.getText()) + 6;
        versionLabel.setBounds (pad + titleW + 12, y + 13, versionW, 16);

        int connW = connectionStatus.getPreferredWidth();
        connectionStatus.setBounds (pad + contentW - connW, y + 6, connW, 16);

        y += 28;
    }

    placeDivider();

    // ---- Session section ----
    bool inSession = proc.isInSession();
    {
        sessionSectionLabel.setBounds (pad, y, 100, 16);
        sessionPeerStatus.setBounds (pad, y, contentW, 16);
        y += 16 + 9;

        if (inSession)
        {
            idleHelperLabel.setVisible (false);
            joinButton.setVisible (false);
            sessionReadout.setVisible (true);
            leaveButton.setVisible (true);

            // Full width now: there is no IP left to copy, so no button beside it.
            sessionReadout.setBounds (CST::expandedForShadow ({ pad, y, contentW, 52 }));
            y += 52 + 14;

            leaveButton.setBounds (CST::expandedForShadow ({ pad, y, contentW, 52 }));
            y += 52;
        }
        else
        {
            idleHelperLabel.setVisible (true);
            joinButton.setVisible (true);
            sessionReadout.setVisible (false);
            leaveButton.setVisible (false);

            idleHelperLabel.setBounds (pad, y, contentW, 18);
            y += 18 + 14;

            joinButton.setBounds (CST::expandedForShadow ({ pad, y, contentW, 56 }));
            y += 56;
        }
    }

    y += 20; // spacer before the record button

    recordButton.setBounds (CST::expandedForShadow ({ pad, y, contentW, 56 }));
    y += 56;

    y += 20; // margin-top on the status strip

    const int stripH = 46;
    indicatorStrip.setBounds (CST::expandedForShadow ({ pad, y, contentW, stripH }));
    y += stripH;

    // ---- Generated files ----
    bool hasSession = proc.lastSessionDir.isDirectory();
    bool showGrid = hasSession && ! proc.isRecording();

    filesSectionLabel.setVisible (showGrid);
    dragHintLabel.setVisible (showGrid);

    if (showGrid)
    {
        y += 24;
        filesSectionLabel.setBounds (pad, y, 220, 16);
        dragHintLabel.setBounds (pad, y, contentW, 16);
        y += 16 + 12;

        layoutGeneratedFilesGrid (pad, contentW, y);
    }
    else
    {
        if (localAudioCard)  localAudioCard->setVisible (false);
        if (remoteAudioCard) remoteAudioCard->setVisible (false);
        if (localMidiCard)   localMidiCard->setVisible (false);
        if (remoteMidiCard)  remoteMidiCard->setVisible (false);
    }

    int needed = y + pad;
    setResizeLimits (CST::widthMin, needed, CST::widthMax, needed);
    if (needed != getHeight())
        setSize (getWidth(), needed);

    countdownLabel.setBounds (getLocalBounds());
}

//==============================================================================
void CollabSyncEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateUI();
}

void CollabSyncEditor::timerCallback()
{
    updateUI();
}

//==============================================================================
void CollabSyncEditor::updateUI()
{
    bool connected    = proc.isConnected();
    bool recording    = proc.isRecording();
    bool countingDown = proc.isCountingDown();
    bool inSession      = proc.isInSession();
    bool hasSession   = proc.lastSessionDir.isDirectory();

    // ---- Header connection status + Host peer status ----
    // (per DESIGN_HANDOFF.md's state table, "finished" is visually identical
    // to "connected" — it only affects whether the files grid shows, handled
    // separately below via hasSession.)
    juce::String headerText, hostText;
    juce::Colour headerDot, headerTextCol, headerGlowCol, hostTextCol, hostGlowCol;
    bool headerDotOn = false, headerPulse = false, headerGlow = false, hostGlow = false;

    if (recording)
    {
        headerText = juce::String::fromUTF8 ("Recording\xE2\x80\xA6");
        headerDot = CST::red; headerDotOn = true; headerPulse = true;
        headerTextCol = CST::redBright; headerGlowCol = CST::red; headerGlow = true;
        hostText = "Peer Connected";
        hostTextCol = CST::mintBright; hostGlowCol = CST::mint; hostGlow = true;
    }
    else if (connected)
    {
        headerText = "Connected";
        headerDot = CST::mint; headerDotOn = true; headerPulse = false;
        headerTextCol = CST::mintBright; headerGlowCol = CST::mint; headerGlow = true;
        hostText = "Peer Connected";
        hostTextCol = CST::mintBright; hostGlowCol = CST::mint; hostGlow = true;
    }
    else if (inSession)
    {
        headerText = juce::String::fromUTF8 ("Waiting for peer\xE2\x80\xA6");
        headerDot = CST::amber; headerDotOn = true; headerPulse = true;
        headerTextCol = CST::amberBright; headerGlowCol = CST::amber; headerGlow = true;
        hostText = juce::String::fromUTF8 ("Waiting \xE2\x80\x94 1 of 2");
        hostTextCol = CST::amberBright; hostGlowCol = CST::amber; hostGlow = true;
    }
    else
    {
        headerText = "Not connected";
        headerDot = CST::dotOffFill; headerDotOn = false; headerPulse = false;
        headerTextCol = CST::textMuted22; headerGlowCol = CST::mint; headerGlow = false;
        hostText = "Inactive";
        hostTextCol = CST::textMuted22; hostGlowCol = CST::mint; hostGlow = false;
    }

    connectionStatus.setStatus (headerText, headerDot, headerDotOn, headerPulse,
                                 headerTextCol, headerGlowCol, headerGlow);

    sessionPeerStatus.setStyle (lnf.mono (12.0f, 400), hostTextCol, hostGlowCol, hostGlow,
                              juce::Justification::centredRight);
    sessionPeerStatus.setText (hostText);

    // ---- Record button ----
    RecordButton::State rs = RecordButton::State::disabled;
    if (recording)
        rs = RecordButton::State::recording;
    else if (connected && ! countingDown)
        rs = RecordButton::State::ready;
    recordButton.setRecordState (rs);

    // ---- Status strip ----
    bool midiLive  = recording && proc.anyMidiNoteHeld();
    bool audioLive = recording && proc.getInputAudioLevel() > 0.02f;
    bool connPending = inSession && ! connected;

    std::array<IndicatorStrip::Indicator, 5> inds { {
        { "Conn",  connected || inSession, connPending, connPending ? CST::amber : CST::mint },
        { "Sync",  connected,            false,       CST::mint },
        { "MIDI",  midiLive,             false,       CST::mint },
        { "Audio", audioLive,            false,       CST::mint },
        { "Rec",   recording,            recording,   CST::red  },
    } };
    indicatorStrip.setIndicators (inds);

    // ---- Session section ----
    // The room-code model has no IP to share: the friend joins with the same
    // (baked-in) room code by clicking Connect, so there is nothing to copy.
    // Show the session state here instead of an IP readout.
    // Seats taken, so "nothing is happening yet" reads as progress rather than
    // a stall: you are in, your friend is not here yet.
    if (inSession)
        sessionReadout.setValue (connected ? juce::String::fromUTF8 ("Peer connected \xE2\x80\x94 2 of 2")
                                           : juce::String::fromUTF8 ("Waiting for peer \xE2\x80\x94 1 of 2"),
                                connected ? CST::mint : CST::amber);
    else
        sessionReadout.setValue ("Ready", CST::textMuted32);

    joinButton.setEnabled (! inSession && ! connected);
    leaveButton.setEnabled (! recording && ! countingDown);

    // ---- Generated files ----
    if (hasSession)
        rebuildFileCards();

    // ---- Countdown overlay ----
    countdownLabel.setVisible (countingDown);
    if (countingDown)
        countdownLabel.setText (juce::String (proc.getCountdownBeat()), juce::dontSendNotification);

    resized();
}

//==============================================================================
void CollabSyncEditor::rebuildFileCards()
{
    auto dir = proc.lastSessionDir;
    if (! dir.isDirectory() || dir == lastBuiltSessionDir)
        return;

    lastBuiltSessionDir = dir;

    localAudioCard.reset();
    remoteAudioCard.reset();
    localMidiCard.reset();
    remoteMidiCard.reset();

    auto localWav  = dir.getChildFile ("local.wav");
    auto remoteWav = dir.getChildFile ("remote.wav");
    auto localMid  = dir.getChildFile ("local.mid");
    auto remoteMid = dir.getChildFile ("remote.mid");

    if (localWav.existsAsFile())
    {
        localAudioCard = std::make_unique<AudioFileCard> (lnf, localWav, "You", CST::waveLocal);
        addAndMakeVisible (*localAudioCard);
    }
    if (remoteWav.existsAsFile())
    {
        remoteAudioCard = std::make_unique<AudioFileCard> (lnf, remoteWav, "Peer", CST::wavePeer);
        addAndMakeVisible (*remoteAudioCard);
    }
    if (localMid.existsAsFile())
    {
        localMidiCard = std::make_unique<MidiFileCard> (lnf, localMid, "You", CST::waveLocal);
        addAndMakeVisible (*localMidiCard);
    }
    if (remoteMid.existsAsFile())
    {
        remoteMidiCard = std::make_unique<MidiFileCard> (lnf, remoteMid, "Peer", CST::wavePeer);
        addAndMakeVisible (*remoteMidiCard);
    }
}
