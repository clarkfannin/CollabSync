#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
// Draggable file tile for MIDI session export (text-only)
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
// Draggable waveform tile for WAV session export
class WaveformTile : public juce::Component
{
public:
    WaveformTile (const juce::File& f, const juce::String& label)
        : file (f), labelText (label) { loadPeaks(); }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (isMouseOver() ? juce::Colour (0xff1e2e48) : juce::Colour (0xff161e30));
        g.fillRoundedRectangle (b, 4.0f);
        g.setColour (juce::Colour (0xff2a3a56));
        g.drawRoundedRectangle (b.reduced (0.5f), 4.0f, 1.0f);

        if (! peaks.empty())
        {
            float waveH = b.getHeight() - 16.0f;
            float midY  = 2.0f + waveH / 2.0f;
            float barW  = (b.getWidth() - 8.0f) / (float) peaks.size();

            g.setColour (juce::Colour (0xff5b8dee).withAlpha (0.6f));
            for (int i = 0; i < (int) peaks.size(); ++i)
            {
                float x = 4.0f + (float) i * barW;
                float h = peaks[(size_t) i] * waveH / 2.0f;
                if (h < 0.5f) h = 0.5f;
                g.fillRect (x, midY - h, std::max (1.0f, barW - 0.5f), h * 2.0f);
            }
        }

        // Label at bottom
        g.setColour (juce::Colour (0xffb0b8cc));
        g.setFont (juce::FontOptions (9.5f));
        g.drawText (labelText, 6, (int) b.getHeight() - 14, getWidth() - 36, 14,
                    juce::Justification::centredLeft, true);

        g.setColour (juce::Colour (0xff4a6a9a));
        g.setFont (juce::FontOptions (9.0f));
        g.drawText ("drag", getWidth() - 32, (int) b.getHeight() - 14, 28, 14,
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
    void loadPeaks()
    {
        juce::AudioFormatManager mgr;
        mgr.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (mgr.createReaderFor (file));
        if (! reader) return;

        int totalSamples = (int) reader->lengthInSamples;
        const int numPeaks = 150;
        int samplesPerPeak = std::max (1, totalSamples / numPeaks);

        juce::AudioBuffer<float> buf (1, samplesPerPeak);
        peaks.resize ((size_t) numPeaks, 0.0f);

        for (int i = 0; i < numPeaks; ++i)
        {
            int64_t start = (int64_t) i * samplesPerPeak;
            int toRead = std::min (samplesPerPeak, totalSamples - (int) start);
            if (toRead <= 0) break;

            buf.clear();
            reader->read (&buf, 0, toRead, start, true, false);

            float peak = 0.0f;
            auto* data = buf.getReadPointer (0);
            for (int s = 0; s < toRead; ++s)
                peak = std::max (peak, std::abs (data[s]));
            peaks[(size_t) i] = peak;
        }
    }

    juce::File   file;
    juce::String labelText;
    std::vector<float> peaks;
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

    // MIDI indicator
    juce::Label midiLabel;
    bool midiLightOn = false;

    // Diagnostics
    juce::Label diagLabel;

    // Countdown
    juce::Label countdownLabel;

    // Session files
    juce::Label      filesHeaderLabel;
    juce::TextButton showInFinderButton { "Show in Finder" };
    std::unique_ptr<WaveformTile> localWavTile, remoteWavTile;
    std::unique_ptr<FileTile>     localMidTile, remoteMidTile;

    void updateUI();
    void rebuildFileTiles();

    static void styleButton       (juce::TextButton& b, juce::Colour fill, juce::Colour text = juce::Colours::white);
    static void styleSectionLabel (juce::Label& l, const juce::String& text);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CollabSyncEditor)
};
