#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "UI/CollabSyncLookAndFeel.h"
#include "UI/CollabSyncComponents.h"
#include "UI/GeneratedFileCard.h"

//==============================================================================
// CollabSync editor — dark forest-green neumorphic redesign.
// Layout/state mirror design-reference/DESIGN_HANDOFF.md's vertical order:
//   header -> divider -> host section -> divider -> host-IP section ->
//   record button -> status strip -> generated-files grid.
//
// Reads processor state through existing accessors only (see the "hard
// constraints" in the task brief — PluginProcessor's public API is untouched
// except one additive getter, getInputAudioLevel()).
class CollabSyncEditor : public juce::AudioProcessorEditor,
                          public juce::ChangeListener,
                          public juce::Timer
{
public:
    CollabSyncEditor (CollabSyncProcessor&);
    ~CollabSyncEditor() override;

    void paint   (juce::Graphics&) override;
    void resized () override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

private:
    CollabSyncProcessor& proc;
    CollabSyncLookAndFeel lnf;

    //==================================================================
    // Header
    juce::Label titleLabel, versionLabel;
    ConnectionStatusView connectionStatus { lnf };

    //==================================================================
    // Host section (this instance hosting its own session)
    SectionLabel hostSectionLabel   { lnf, "Host" };
    GlowLabel    hostPeerStatus;

    juce::Label       idleHelperLabel;
    NeumorphicButton  startSessionButton  { "Start Session", lnf, 700, CST::cream, 16.0f };

    ReadoutWell       hostIPReadout       { lnf };
    NeumorphicButton  copyIPButton        { "Copy IP", lnf, 600, juce::Colour (0xffcfe6da), 13.0f };
    NeumorphicButton  stopSessionButton   { "Stop Session", lnf, 600, CST::creamAlpha (0.72f), 15.0f };

    //==================================================================
    // Host IP section (joining a remote host)
    SectionLabel      hostIPSectionLabel  { lnf, "Host IP" };
    EditableWell      hostIPInput         { lnf };
    NeumorphicButton  connectButton       { "Connect", lnf, 700, CST::cream, 15.0f };
    NeumorphicButton  disconnectButton    { "Disconnect", lnf, 600, CST::creamAlpha (0.55f), 15.0f };

    //==================================================================
    // Record + status strip
    RecordButton    recordButton { lnf };
    IndicatorStrip  indicatorStrip { lnf };

    //==================================================================
    // Generated files preview
    SectionLabel filesSectionLabel { lnf, "Generated Files" };
    juce::Label  dragHintLabel;

    std::unique_ptr<AudioFileCard> localAudioCard, remoteAudioCard;
    std::unique_ptr<MidiFileCard>  localMidiCard,  remoteMidiCard;
    juce::File lastBuiltSessionDir; // dirty-check so cards are rebuilt only when the session actually changes

    //==================================================================
    // Countdown overlay (pre-roll before recording starts — real feature,
    // not part of the visual handoff's state enum, so it's drawn as a modal
    // overlay on top of everything else rather than folded into the strip).
    juce::Label countdownLabel;

    //==================================================================
    // Divider hairlines: computed in resized(), drawn in paint().
    std::vector<int> dividerYs;

    void updateUI();
    void rebuildFileCards();
    void layoutGeneratedFilesGrid (int pad, int contentW, int& y);
    void paintPanelBackground (juce::Graphics&);
    void paintDividers (juce::Graphics&);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CollabSyncEditor)
};
