#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
// Draggable file tile for session export
class FileTile : public juce::Component
{
public:
    FileTile (const juce::File& f, const juce::String& label)
        : file (f), labelText (label) {}

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (isMouseOver() ? juce::Colour (0xff1e2e48) : juce::Colour (0xff161e30));
        g.fillRoundedRectangle (b, 4.0f);
        g.setColour (juce::Colour (0xff2a3a56));
        g.drawRoundedRectangle (b.reduced (0.5f), 4.0f, 1.0f);

        g.setColour (juce::Colour (0xffb0b8cc));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (labelText, 10, 0, getWidth() - 24, getHeight(),
                    juce::Justification::centredLeft, true);

        g.setColour (juce::Colour (0xff4a6a9a));
        g.setFont (juce::FontOptions (10.0f));
        g.drawText ("drag", getWidth() - 32, 0, 28, getHeight(),
                    juce::Justification::centredRight, false);
    }

    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit  (const juce::MouseEvent&) override { repaint(); }
    void mouseDrag  (const juce::MouseEvent&) override
    {
        if (file.existsAsFile())
            juce::DragAndDropContainer::performExternalDragDropOfFiles (
                { file.getFullPathName() }, false, this);
    }

private:
    juce::File   file;
    juce::String labelText;
};

//==============================================================================
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

    // Header
    juce::Label titleLabel, statusLabel, latencyLabel;

    // Connection
    juce::Label      hostLabel;
    juce::TextEditor hostInput;
    juce::TextButton connectButton    { "Connect"    };
    juce::TextButton disconnectButton { "Disconnect" };

    // Session hosting
    juce::Label      sessionSectionLabel;
    juce::TextButton startSessionButton { "Start Session" };
    juce::TextButton stopSessionButton  { "Stop Session"  };
    juce::Label      sessionStatusLabel;
    juce::Label      tailscaleIPLabel;
    juce::TextButton copyIPButton       { "Copy IP" };

    // Record
    juce::TextButton recordButton { "Record" };

    // Diagnostics
    juce::Label diagLabel;

    // Countdown
    juce::Label countdownLabel;

    // Session files
    juce::Label      filesHeaderLabel;
    juce::TextButton showInFinderButton { "Show in Finder" };
    std::array<std::unique_ptr<FileTile>, 4> fileTiles;

    void updateUI();
    void rebuildFileTiles();

    static void styleButton       (juce::TextButton& b, juce::Colour fill, juce::Colour text = juce::Colours::white);
    static void styleSectionLabel (juce::Label& l, const juce::String& text);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CollabSyncEditor)
};
