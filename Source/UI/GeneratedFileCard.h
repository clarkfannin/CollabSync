#pragma once
#include <JuceHeader.h>
#include "CollabSyncLookAndFeel.h"
#include "CollabSyncTheme.h"

//==============================================================================
// Generated-files preview cards. Both render REAL data (decoded audio peaks /
// parsed MIDI notes) — only the visual style (colour, bar/note height,
// spacing, radius) is taken from the design prototype; the shapes themselves
// are never faked. Both are drag sources for VST file-export drag-and-drop,
// same mechanism the pre-redesign FileTile/WaveformTile used.

//======================================================================
class AudioFileCard : public juce::Component
{
public:
    AudioFileCard (CollabSyncLookAndFeel& lf, const juce::File& file,
                    const juce::String& ownerLabel, juce::Colour waveColour);

    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    void loadPeaks();
    static juce::String formatDuration (double seconds);

    CollabSyncLookAndFeel& laf;
    juce::File file;
    juce::String ownerLabel;
    juce::Colour waveColour;
    juce::String durationText;
    std::vector<float> peaks;
};

//======================================================================
class MidiFileCard : public juce::Component
{
public:
    MidiFileCard (CollabSyncLookAndFeel& lf, const juce::File& file,
                   const juce::String& ownerLabel, juce::Colour noteColour);

    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    struct Note
    {
        double startNorm = 0.0;   // 0..1 across the roll's time span
        double lengthNorm = 0.02; // 0..1
        int    row = 0;           // 0..rows-1, 0 = highest pitch
    };

    void loadNotes();

    CollabSyncLookAndFeel& laf;
    juce::File file;
    juce::String ownerLabel;
    juce::Colour noteColour;
    juce::String noteCountText;
    std::vector<Note> notes;
    static constexpr int numRows = 6;
};
