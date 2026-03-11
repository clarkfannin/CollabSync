#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
// A single device row: coloured dot + name
class DeviceRow : public juce::Component
{
public:
    enum class Status { active, available, unavailable, error };

    DeviceRow (const juce::String& name, Status s) : deviceName (name), status (s) {}

    void paint (juce::Graphics& g) override
    {
        // Dot: active=green, available=blue, unavailable=grey, error=red
        juce::Colour dotCol = status == Status::active      ? juce::Colours::limegreen :
                              status == Status::available    ? juce::Colour (0xff4a90d9) :
                              status == Status::error        ? juce::Colours::red :
                                                               juce::Colour (0xff444455);
        g.setColour (dotCol);
        g.fillEllipse ((float)(getWidth() - 14), (float)(getHeight() / 2 - 4), 8.0f, 8.0f);

        // Name — dim text for unavailable devices
        g.setColour (status == Status::unavailable ? juce::Colour (0xff555568)
                                                   : juce::Colours::lightgrey);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (deviceName, 8, 0, getWidth() - 26, getHeight(),
                    juce::Justification::centredLeft, true);
    }

    juce::String deviceName;
    Status       status;
};

//==============================================================================
// Draggable file tile for session export
class FileTile : public juce::Component
{
public:
    FileTile (const juce::File& f, const juce::String& label)
        : file (f), labelText (label) {}

    void paint (juce::Graphics& g) override
    {
        g.setColour (isMouseOver() ? juce::Colour (0xff2a4a7a) : juce::Colour (0xff1e3050));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 5.0f);
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (labelText, 8, 0, getWidth() - 36, getHeight(),
                    juce::Justification::centredLeft, true);
        g.setColour (juce::Colours::lightblue.withAlpha (0.6f));
        g.setFont (juce::FontOptions (10.0f));
        g.drawText (u8"\u2197", getWidth() - 20, 0, 16, getHeight(),
                    juce::Justification::centred, false);
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
    juce::Label         roomLabel, hostLabel;
    juce::TextEditor    roomInput, hostInput;
    juce::TextButton    connectButton    { "Connect" };
    juce::TextButton    disconnectButton { "Disconnect" };

    // Countdown
    juce::Label countdownLabel;

    // Record
    juce::TextButton recordButton { "Record" };

    // Device sections
    juce::Label midiSectionLabel, audioInputSectionLabel, audioOutputSectionLabel;
    juce::OwnedArray<DeviceRow> midiRows, audioInputRows, audioOutputRows;
    juce::Label midiEmptyLabel, audioInputEmptyLabel, audioOutputEmptyLabel;

    // Session hosting
    juce::Label      sessionSectionLabel;
    juce::TextButton startSessionButton  { "Start Session" };
    juce::TextButton stopSessionButton   { "Stop Session"  };
    juce::Label      sessionStatusLabel;
    juce::Label      tailscaleIPLabel;
    juce::TextButton copyIPButton        { "Copy IP" };

    // Diagnostics
    juce::Label diagLabel;

    // Session files
    juce::Label      filesHeaderLabel;
    juce::TextButton showInFinderButton { "Show in Finder" };
    std::array<std::unique_ptr<FileTile>, 4> fileTiles;

    void updateUI();
    void refreshDevices();
    void rebuildFileTiles();
    int  layoutDeviceSection (juce::OwnedArray<DeviceRow>& rows,
                              juce::Label& emptyLabel, int y);

    static void styleSectionLabel (juce::Label& l, const juce::String& text);
    static void styleButton (juce::TextButton& b, juce::Colour col);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CollabSyncEditor)
};
