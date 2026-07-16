#include "GeneratedFileCard.h"

namespace
{
    constexpr int cardPadL = 15, cardPadR = 15, cardPadT = 15, cardPadB = 14;
    constexpr float bodyHeight = 52.0f;

    // Card chrome + header row (owner · type + drag grip) + footer row
    // (filename + metadata), shared between the audio and MIDI cards.
    // Returns the body rectangle the caller should draw the waveform/roll into.
    juce::Rectangle<float> paintCardFrame (juce::Graphics& g, CollabSyncLookAndFeel& laf,
                                            juce::Rectangle<int> full,
                                            const juce::String& ownerLabel, const juce::String& typeLabel,
                                            const juce::String& filename, const juce::String& metaText)
    {
        CollabSyncLookAndFeel::paintRecessed (g, full.toFloat(), CST::radiusCard, CST::recessedFill);

        auto content = full.reduced (0, 0);
        content.removeFromLeft (cardPadL);
        content.removeFromRight (cardPadR);
        content.removeFromTop (cardPadT);
        content.removeFromBottom (cardPadB);

        // Header
        auto header = content.removeFromTop (17);
        {
            auto ownerFont = laf.sans (13.0f, 600);
            auto typeFont  = laf.mono (11.0f, 400);
            int ownerW = ownerFont.getStringWidth (ownerLabel);
            g.setColour (CST::cream);
            g.setFont (ownerFont);
            g.drawText (ownerLabel, header.getX(), header.getY(), ownerW, header.getHeight(),
                        juce::Justification::centredLeft, false);

            juce::String typeText = " \xC2\xB7 " + typeLabel; // " · Audio" / " · MIDI"
            int typeW = typeFont.getStringWidth (typeText);
            g.setColour (CST::textMuted34.withAlpha (0.4f));
            g.setFont (typeFont);
            g.drawText (typeText, header.getX() + ownerW, header.getY(), typeW, header.getHeight(),
                        juce::Justification::centredLeft, false);

            // Drag grip glyph, right-aligned
            g.setColour (CST::textMuted32.withAlpha (0.3f));
            g.setFont (typeFont.withExtraKerningFactor (0.2f));
            g.drawText (juce::String::fromUTF8 ("\xE2\xA0\xBF"), header, juce::Justification::centredRight, false);
        }

        content.removeFromTop (2); // ~11px margin-bottom on header, minus the row's own leading
        auto body = content.removeFromTop ((int) bodyHeight).toFloat();

        content.removeFromTop (3); // ~11px margin-top before footer
        auto footer = content.removeFromTop (16);
        {
            auto monoFont = laf.mono (11.0f, 400);
            g.setColour (CST::textMuted50);
            g.setFont (monoFont);
            g.drawText (filename, footer, juce::Justification::centredLeft, false);

            g.setColour (CST::textMuted32);
            g.drawText (metaText, footer, juce::Justification::centredRight, false);
        }

        return body;
    }
}

//==============================================================================
// AudioFileCard
//==============================================================================
AudioFileCard::AudioFileCard (CollabSyncLookAndFeel& lf, const juce::File& f,
                               const juce::String& owner, juce::Colour waveCol)
    : laf (lf), file (f), ownerLabel (owner), waveColour (waveCol)
{
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    loadPeaks();
}

juce::String AudioFileCard::formatDuration (double seconds)
{
    int total = (int) std::round (seconds);
    int m = total / 60;
    int s = total % 60;
    return juce::String (m) + ":" + (s < 10 ? "0" : "") + juce::String (s);
}

void AudioFileCard::loadPeaks()
{
    juce::AudioFormatManager mgr;
    mgr.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (mgr.createReaderFor (file));
    if (! reader)
        return;

    durationText = formatDuration ((double) reader->lengthInSamples / juce::jmax (1.0, reader->sampleRate));

    // 46 bars, matching the prototype's bar count.
    const int numPeaks = 46;
    juce::int64 totalSamples = reader->lengthInSamples;
    juce::int64 samplesPerPeak = juce::jmax ((juce::int64) 1, totalSamples / numPeaks);

    juce::AudioBuffer<float> buf (1, (int) juce::jmin ((juce::int64) 1 << 20, samplesPerPeak));
    peaks.assign ((size_t) numPeaks, 0.0f);

    for (int i = 0; i < numPeaks; ++i)
    {
        juce::int64 start = (juce::int64) i * samplesPerPeak;
        int toRead = (int) juce::jmin (samplesPerPeak, totalSamples - start);
        if (toRead <= 0)
            break;

        buf.setSize (1, toRead, false, false, true);
        buf.clear();
        reader->read (&buf, 0, toRead, start, true, false);

        float peak = 0.0f;
        auto* data = buf.getReadPointer (0);
        for (int s = 0; s < toRead; ++s)
            peak = std::max (peak, std::abs (data[s]));
        peaks[(size_t) i] = peak;
    }
}

void AudioFileCard::paint (juce::Graphics& g)
{
    auto body = paintCardFrame (g, laf, getLocalBounds(), ownerLabel, "Audio",
                                 file.getFileName(), durationText);

    if (peaks.empty())
        return;

    const float gap = 2.0f;
    float barW = (body.getWidth() - gap * (float) (peaks.size() - 1)) / (float) peaks.size();
    barW = juce::jmax (2.0f, barW);
    float midY = body.getCentreY();

    g.setColour (waveColour);
    float x = body.getX();
    for (float peak : peaks)
    {
        float h = juce::jmax (2.0f, peak * body.getHeight());
        juce::Rectangle<float> bar (x, midY - h * 0.5f, barW, h);
        g.fillRoundedRectangle (bar, 2.0f);
        x += barW + gap;
    }
}

void AudioFileCard::mouseDrag (const juce::MouseEvent&)
{
    if (file.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
}

//==============================================================================
// MidiFileCard
//==============================================================================
MidiFileCard::MidiFileCard (CollabSyncLookAndFeel& lf, const juce::File& f,
                             const juce::String& owner, juce::Colour noteCol)
    : laf (lf), file (f), ownerLabel (owner), noteColour (noteCol)
{
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    loadNotes();
}

void MidiFileCard::loadNotes()
{
    auto stream = file.createInputStream();
    if (! stream)
        return;

    juce::MidiFile mf;
    if (! mf.readFrom (*stream))
        return;

    if (mf.getNumTracks() == 0)
        return;

    // Collect note-on events across every track (Recorder writes a single
    // track, but this stays correct if that ever changes).
    struct Raw { double time; int pitch; double length; };
    std::vector<Raw> raw;
    double minTime = std::numeric_limits<double>::max();
    double maxTime = std::numeric_limits<double>::lowest();
    int minPitch = 127, maxPitch = 0;

    for (int t = 0; t < mf.getNumTracks(); ++t)
    {
        auto* track = mf.getTrack (t);
        if (track == nullptr)
            continue;

        auto trackCopy = *track;
        trackCopy.updateMatchedPairs();

        for (auto* holder : trackCopy)
        {
            auto& msg = holder->message;
            if (! msg.isNoteOn())
                continue;

            double onTime = msg.getTimeStamp();
            double offTime = onTime + 1.0; // fallback length
            if (holder->noteOffObject != nullptr)
                offTime = holder->noteOffObject->message.getTimeStamp();

            int pitch = msg.getNoteNumber();
            raw.push_back ({ onTime, pitch, juce::jmax (0.05, offTime - onTime) });

            minTime = std::min (minTime, onTime);
            maxTime = std::max (maxTime, offTime);
            minPitch = std::min (minPitch, pitch);
            maxPitch = std::max (maxPitch, pitch);
        }
    }

    noteCountText = juce::String ((int) raw.size()) + (raw.size() == 1 ? " note" : " notes");

    if (raw.empty() || maxTime <= minTime)
        return;

    double span = maxTime - minTime;
    int pitchRange = juce::jmax (1, maxPitch - minPitch);

    notes.reserve (raw.size());
    for (auto& r : raw)
    {
        Note n;
        n.startNorm  = (r.time - minTime) / span;
        n.lengthNorm = juce::jlimit (0.01, 1.0, r.length / span);

        // Row 0 = highest pitch (top of the roll), matching typical piano-roll orientation.
        double frac = (double) (maxPitch - r.pitch) / (double) pitchRange; // 0..1
        n.row = juce::jlimit (0, numRows - 1, (int) std::round (frac * (double) (numRows - 1)));
        notes.push_back (n);
    }
}

void MidiFileCard::paint (juce::Graphics& g)
{
    auto body = paintCardFrame (g, laf, getLocalBounds(), ownerLabel, "MIDI",
                                 file.getFileName(), noteCountText);

    if (notes.empty() || body.isEmpty())
        return;

    float rowH = body.getHeight() / (float) numRows;
    g.setColour (noteColour);

    for (auto& n : notes)
    {
        float x = body.getX() + (float) n.startNorm * body.getWidth();
        float w = juce::jmax (3.0f, (float) n.lengthNorm * body.getWidth() - 1.5f);
        float y = body.getY() + (float) n.row * rowH + rowH * 0.12f;
        float h = juce::jmax (4.0f, rowH * 0.76f);

        g.fillRoundedRectangle (x, y, w, h, 3.0f);
    }
}

void MidiFileCard::mouseDrag (const juce::MouseEvent&)
{
    if (file.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
}
