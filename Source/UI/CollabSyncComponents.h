#pragma once
#include <JuceHeader.h>
#include "CollabSyncLookAndFeel.h"
#include "CollabSyncTheme.h"

//==============================================================================
/** A label that can render a coloured "glow" behind its text (a blurred copy
    of the text tinted with a glow colour), approximating the handoff's
    text-shadow glow states (mint/amber/red). The glow image is cached and
    only rebuilt when the text/style actually changes. */
class GlowLabel : public juce::Component
{
public:
    void setStyle (juce::Font font, juce::Colour textColour, juce::Colour glowColour,
                    bool hasGlow, juce::Justification justification = juce::Justification::centredLeft);
    void setText (const juce::String& newText);

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void rebuildGlow();

    juce::String text;
    juce::Font font { juce::FontOptions { 12.0f } };
    juce::Colour textColour { CST::cream }, glowColour { CST::mint };
    bool hasGlow = false;
    juce::Justification justification { juce::Justification::centredLeft };
    bool dirty = true;
    juce::Image glowImage;
};

//==============================================================================
/** Section label: DM Mono 11px, letterspace .2em, uppercase, muted — e.g. "HOST". */
class SectionLabel : public juce::Component
{
public:
    SectionLabel (CollabSyncLookAndFeel& lf, const juce::String& text);
    void paint (juce::Graphics&) override;

private:
    CollabSyncLookAndFeel& laf;
    juce::String text;
};

//==============================================================================
/** Shared neumorphic button: one fill + one raised shadow (per the handoff,
    hierarchy comes from text weight/colour only, never from a different
    fill). Handles hover/down state by nudging the fill brightness, and a
    dimmed look when disabled. */
class NeumorphicButton : public juce::Button
{
public:
    NeumorphicButton (const juce::String& buttonText, CollabSyncLookAndFeel& lf,
                       int fontWeight, juce::Colour textColour, float fontSize = 15.0f);

    void paintButton (juce::Graphics&, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

private:
    CollabSyncLookAndFeel& laf;
    int   weight;
    juce::Colour textCol;
    float size;
};

//==============================================================================
/** Recessed well showing a static, non-editable value (e.g. the local IP
    readout, or the amber "no IP yet" fallback text). */
class ReadoutWell : public juce::Component
{
public:
    explicit ReadoutWell (CollabSyncLookAndFeel& lf);

    void setValue (const juce::String& text, juce::Colour textColour);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    CollabSyncLookAndFeel& laf;
    juce::Label valueLabel;
};

//==============================================================================
/** Recessed well containing an editable juce::TextEditor — used for the
    Host IP address field. */
class EditableWell : public juce::Component
{
public:
    explicit EditableWell (CollabSyncLookAndFeel& lf);

    juce::TextEditor& getEditor() { return editor; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    CollabSyncLookAndFeel& laf;
    juce::TextEditor editor;
};

//==============================================================================
/** The full-width record button. Three visual states per the handoff: Ready
    (raised, cream text, small static red dot), Disabled (recessed, dim
    text), Recording (dark red gradient, red text+dot, animated "recpulse"
    glow). Owns its own Timer so the pulse animation only runs while
    recording. */
class RecordButton : public juce::Button,
                      private juce::Timer
{
public:
    enum class State { ready, disabled, recording };

    RecordButton (CollabSyncLookAndFeel& lf);
    ~RecordButton() override;

    void setRecordState (State newState);

    void paintButton (juce::Graphics&, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

private:
    void timerCallback() override;

    CollabSyncLookAndFeel& laf;
    State state = State::disabled;
};

//==============================================================================
/** Dot + glow-label cluster, right-justified within its own bounds — used
    for the header's connection status ("* Connected" etc). Owns its own
    Timer so the amber/red pulse only runs while relevant. */
class ConnectionStatusView : public juce::Component,
                              private juce::Timer
{
public:
    explicit ConnectionStatusView (CollabSyncLookAndFeel& lf);
    ~ConnectionStatusView() override;

    void setStatus (const juce::String& text, juce::Colour dotColour, bool dotOn, bool pulse,
                     juce::Colour textColour, juce::Colour glowColour, bool hasGlow);

    // Dot + gap + text width — size the component to this and anchor it to
    // the row's right edge so the cluster hugs the edge with no dead space
    // (mirrors the prototype's inline-flex row, which sizes to content).
    int getPreferredWidth() const;

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    CollabSyncLookAndFeel& laf;
    GlowLabel label;
    juce::String currentText;
    juce::Font currentFont;
    juce::Colour dotColour { CST::mint };
    bool dotOn = false, pulse = false;
};

//==============================================================================
/** The five-indicator status strip (Conn / Sync / MIDI / Audio / Rec) inside
    a single recessed well. Owns a Timer that only runs while at least one
    indicator is pulsing. */
class IndicatorStrip : public juce::Component,
                        private juce::Timer
{
public:
    struct Indicator
    {
        juce::String label;
        bool on = false;
        bool pulse = false;
        juce::Colour colour = CST::mint;
    };

    explicit IndicatorStrip (CollabSyncLookAndFeel& lf);
    ~IndicatorStrip() override;

    // Exactly 5 entries, in order: Conn, Sync, MIDI, Audio, Rec.
    void setIndicators (std::array<Indicator, 5> newIndicators);

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    bool anyPulsing() const;

    CollabSyncLookAndFeel& laf;
    std::array<Indicator, 5> indicators;
};
