#include "CollabSyncComponents.h"

namespace
{
    // The rectangle a shadow-casting component should actually draw its shape in.
    // Components are laid out CST::shadowMargin larger than their visible size on
    // every side (see CST::expandedForShadow) so drop shadows have somewhere to
    // land; without that margin JUCE clips them square at the component edge.
    // Anything positioned relative to the shape — child components, centred text —
    // must use this rather than getLocalBounds().
    juce::Rectangle<int> shapeBounds (const juce::Component& c)
    {
        return c.getLocalBounds().reduced (CST::shadowMargin);
    }

    // A lit dot's glow reaches ~14px past its edge at the pulse peak. Components
    // that draw a dot near their own edge inset it by this much for the same
    // reason shapeBounds exists — otherwise the halo is sliced off square.
    constexpr int dotGlowMargin = 14;
}

//==============================================================================
// GlowLabel
//==============================================================================
void GlowLabel::setStyle (juce::Font f, juce::Colour textCol, juce::Colour glowCol,
                           bool glow, juce::Justification j)
{
    if (font == f && textColour == textCol && glowColour == glowCol
        && hasGlow == glow && justification == j)
        return;

    font = f; textColour = textCol; glowColour = glowCol; hasGlow = glow; justification = j;
    dirty = true;
    repaint();
}

void GlowLabel::setText (const juce::String& newText)
{
    if (text == newText)
        return;
    text = newText;
    dirty = true;
    repaint();
}

void GlowLabel::resized()
{
    dirty = true;
}

void GlowLabel::rebuildGlow()
{
    if (! hasGlow || getWidth() <= 2 || getHeight() <= 2)
    {
        glowImage = {};
        return;
    }

    glowImage = juce::Image (juce::Image::ARGB, getWidth(), getHeight(), true);
    {
        juce::Graphics gg (glowImage);
        gg.setColour (glowColour);
        gg.setFont (font);
        gg.drawText (text, glowImage.getBounds(), justification, true);
    }

    juce::ImageConvolutionKernel kernel (7);
    kernel.createGaussianBlur (3.0f);
    kernel.applyToImage (glowImage, glowImage, glowImage.getBounds());
}

void GlowLabel::paint (juce::Graphics& g)
{
    if (dirty)
    {
        rebuildGlow();
        dirty = false;
    }

    if (hasGlow && glowImage.isValid())
    {
        g.setOpacity (1.0f);
        g.drawImageAt (glowImage, 0, 0);
    }

    g.setColour (textColour);
    g.setFont (font);
    g.drawText (text, getLocalBounds(), justification, true);
}

//==============================================================================
// SectionLabel
//==============================================================================
SectionLabel::SectionLabel (CollabSyncLookAndFeel& lf, const juce::String& t)
    : laf (lf), text (t)
{
    setInterceptsMouseClicks (false, false);
}

void SectionLabel::paint (juce::Graphics& g)
{
    g.setColour (CST::textMuted50);
    auto f = laf.mono (11.0f, 400).withExtraKerningFactor (0.2f);
    g.setFont (f);
    g.drawText (text.toUpperCase(), getLocalBounds(), juce::Justification::centredLeft, false);
}

//==============================================================================
// NeumorphicButton
//==============================================================================
NeumorphicButton::NeumorphicButton (const juce::String& buttonText, CollabSyncLookAndFeel& lf,
                                     int fontWeight, juce::Colour textColour, float fontSize)
    : juce::Button (buttonText), laf (lf), weight (fontWeight), textCol (textColour), size (fontSize)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void NeumorphicButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto bounds = shapeBounds (*this).toFloat();
    auto fill = CST::buttonFill;
    if (down)             fill = fill.brighter (0.06f);
    else if (highlighted) fill = fill.brighter (0.03f);

    float alphaMul = isEnabled() ? 1.0f : 0.5f;

    CollabSyncLookAndFeel::paintRaised (g, bounds, CST::radiusButton, fill, alphaMul);

    g.setColour (textCol.withMultipliedAlpha (alphaMul));
    g.setFont (laf.sans (size, weight).withExtraKerningFactor (0.01f));
    g.drawText (getButtonText(), bounds.toNearestInt(), juce::Justification::centred, false);
}

//==============================================================================
// ReadoutWell
//==============================================================================
ReadoutWell::ReadoutWell (CollabSyncLookAndFeel& lf) : laf (lf)
{
    valueLabel.setInterceptsMouseClicks (false, false);
    valueLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (valueLabel);
}

void ReadoutWell::setValue (const juce::String& text, juce::Colour textColour)
{
    valueLabel.setText (text, juce::dontSendNotification);
    valueLabel.setColour (juce::Label::textColourId, textColour);
    valueLabel.setFont (laf.mono (15.0f, 400));
}

void ReadoutWell::paint (juce::Graphics& g)
{
    CollabSyncLookAndFeel::paintRecessed (g, shapeBounds (*this).toFloat(), CST::radiusWell, CST::recessedFill);
}

void ReadoutWell::resized()
{
    valueLabel.setBounds (shapeBounds (*this).reduced (16, 0));
}

//==============================================================================
// EditableWell
//==============================================================================
EditableWell::EditableWell (CollabSyncLookAndFeel& lf) : laf (lf)
{
    editor.setLookAndFeel (&laf);
    editor.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    editor.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    editor.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    editor.setColour (juce::TextEditor::textColourId, CST::cream);
    editor.setFont (laf.mono (15.0f, 400));
    editor.setJustification (juce::Justification::centredLeft);
    editor.setBorder (juce::BorderSize<int> (0));
    addAndMakeVisible (editor);
}

void EditableWell::paint (juce::Graphics& g)
{
    CollabSyncLookAndFeel::paintRecessed (g, shapeBounds (*this).toFloat(), CST::radiusWell, CST::recessedFill);
}

void EditableWell::resized()
{
    editor.setBounds (shapeBounds (*this).reduced (16, 0));
}

//==============================================================================
// RecordButton
//==============================================================================
RecordButton::RecordButton (CollabSyncLookAndFeel& lf) : juce::Button ("Record"), laf (lf)
{
}

RecordButton::~RecordButton()
{
    stopTimer();
}

void RecordButton::setRecordState (State newState)
{
    if (state == newState)
        return;
    state = newState;
    setEnabled (state == State::ready || state == State::recording);

    if (state == State::recording)
        startTimerHz (30);
    else
        stopTimer();

    repaint();
}

void RecordButton::timerCallback()
{
    repaint();
}

void RecordButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto bounds = shapeBounds (*this).toFloat();
    juce::Path shape;
    shape.addRoundedRectangle (bounds, CST::radiusRecord);

    juce::String label = "Record";
    juce::Colour textColour = CST::cream;
    bool showDot = false;
    float dotRadius = 9.0f;

    if (state == State::recording)
    {
        label = "Recording";
        textColour = CST::red;
        showDot = true;
        dotRadius = 11.0f;

        // Dark red diagonal gradient background — "linear-gradient(145deg,#2a0e0b,#160705)"
        juce::ColourGradient grad (juce::Colour (0xff2a0e0b), bounds.getX(), bounds.getY(),
                                    juce::Colour (0xff160705), bounds.getRight(), bounds.getBottom(), false);
        g.setGradientFill (grad);
        g.fillPath (shape);

        // recpulse: inset + outer red glow oscillating over 1.4s.
        float pulse = CollabSyncLookAndFeel::pulsePhase (1.4);
        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (shape);
            juce::ColourGradient inset (juce::Colour (0xffe65a50).withAlpha (0.10f + pulse * 0.10f),
                                         bounds.getX(), bounds.getY(),
                                         juce::Colour (0xffe65a50).withAlpha (0.0f),
                                         bounds.getX() + bounds.getWidth() * 0.6f,
                                         bounds.getY() + bounds.getHeight() * 0.6f, false);
            g.setGradientFill (inset);
            g.fillRect (bounds);
        }
        juce::DropShadow outerGlow (CST::red.withAlpha (pulse * 0.26f), 22, { 0, 0 });
        outerGlow.drawForPath (g, shape);
    }
    else if (state == State::disabled)
    {
        label = "Record";
        textColour = CST::textMuted24;
        CollabSyncLookAndFeel::paintRecessed (g, bounds, CST::radiusRecord, CST::recessedFill);
    }
    else // ready
    {
        label = "Record";
        textColour = CST::cream;
        showDot = true;
        dotRadius = 9.0f;

        auto fill = CST::buttonFill;
        if (down)             fill = fill.brighter (0.06f);
        else if (highlighted) fill = fill.brighter (0.03f);
        CollabSyncLookAndFeel::paintRaised (g, bounds, CST::radiusRecord, fill);
    }

    // Label + dot, centred as a unit.
    auto font = laf.sans (16.0f, 700).withExtraKerningFactor (0.01f);
    g.setFont (font);
    int textW = font.getStringWidth (label);
    int dotSpace = showDot ? (int) (dotRadius * 2.0f + 11.0f) : 0;
    int totalW = textW + dotSpace;
    int startX = bounds.getCentreX() - totalW / 2;

    if (showDot)
    {
        float cx = (float) startX + dotRadius;
        float cy = bounds.getCentreY();
        g.setColour (CST::red);
        g.fillEllipse (juce::Rectangle<float> (dotRadius * 2.0f, dotRadius * 2.0f).withCentre ({ cx, cy }));
        // Static glow halo (no pulse on the dot itself per spec — only the button chrome pulses).
        g.setColour (CST::red.withAlpha (0.35f));
        g.fillEllipse (juce::Rectangle<float> (dotRadius * 2.0f + 6.0f, dotRadius * 2.0f + 6.0f).withCentre ({ cx, cy }));
        g.setColour (CST::red);
        g.fillEllipse (juce::Rectangle<float> (dotRadius * 2.0f, dotRadius * 2.0f).withCentre ({ cx, cy }));
    }

    g.setColour (textColour);
    g.drawText (label, startX + dotSpace, (int) bounds.getY(), textW, (int) bounds.getHeight(),
                juce::Justification::centredLeft, false);
}

//==============================================================================
// ConnectionStatusView
//==============================================================================
ConnectionStatusView::ConnectionStatusView (CollabSyncLookAndFeel& lf) : laf (lf)
{
    addAndMakeVisible (label);
}

ConnectionStatusView::~ConnectionStatusView()
{
    stopTimer();
}

void ConnectionStatusView::setStatus (const juce::String& text, juce::Colour dot, bool on, bool doPulse,
                                       juce::Colour textColour, juce::Colour glowColour, bool hasGlow)
{
    dotColour = dot;
    dotOn = on;
    pulse = doPulse;
    currentText = text;
    currentFont = laf.mono (12.0f, 400);

    label.setStyle (currentFont, textColour, glowColour, hasGlow, juce::Justification::centredLeft);
    label.setText (text);

    if (pulse && dotOn)
        startTimerHz (30);
    else
        stopTimer();

    resized();
    repaint();
}

int ConnectionStatusView::getPreferredWidth() const
{
    const int dotDiam = 8, gap = 9;
    // Includes the dot's reserved glow margin — resized() shifts the label right
    // by the same amount, so omitting it here clips the text.
    return dotGlowMargin + dotDiam + gap + currentFont.getStringWidth (currentText) + 2;
}

void ConnectionStatusView::timerCallback()
{
    repaint();
}

void ConnectionStatusView::resized()
{
    const int dotDiam = 8, gap = 9;
    label.setBounds (getLocalBounds().withTrimmedLeft (dotGlowMargin + dotDiam + gap));
}

void ConnectionStatusView::paint (juce::Graphics& g)
{
    const float dotDiam = 8.0f;
    float cy = (float) getHeight() * 0.5f;
    float cx = (float) dotGlowMargin + dotDiam * 0.5f;

    float pulseAmount = (dotOn && pulse) ? CollabSyncLookAndFeel::pulsePhase (1.1) : 0.0f;
    CollabSyncLookAndFeel::paintGlowDot (g, { cx, cy }, dotDiam * 0.5f, dotColour, dotOn, pulseAmount);
}

//==============================================================================
// IndicatorStrip
//==============================================================================
IndicatorStrip::IndicatorStrip (CollabSyncLookAndFeel& lf) : laf (lf)
{
    indicators.fill ({});
}

IndicatorStrip::~IndicatorStrip()
{
    stopTimer();
}

void IndicatorStrip::setIndicators (std::array<Indicator, 5> newIndicators)
{
    indicators = newIndicators;
    if (anyPulsing() && ! isTimerRunning())
        startTimerHz (30);
    else if (! anyPulsing() && isTimerRunning())
        stopTimer();
    repaint();
}

bool IndicatorStrip::anyPulsing() const
{
    for (auto& ind : indicators)
        if (ind.on && ind.pulse)
            return true;
    return false;
}

void IndicatorStrip::timerCallback()
{
    repaint();
}

void IndicatorStrip::paint (juce::Graphics& g)
{
    auto bounds = shapeBounds (*this).toFloat();
    CollabSyncLookAndFeel::paintRecessed (g, bounds, CST::radiusRecord, CST::recessedFill);

    auto content = shapeBounds (*this).reduced (16, 14);
    if (content.isEmpty())
        return;

    float pulse = anyPulsing() ? CollabSyncLookAndFeel::pulsePhase (1.1) : 0.0f;

    auto font = laf.mono (10.0f, 400).withExtraKerningFactor (0.16f);
    g.setFont (font);

    // Measure natural widths (dot + gap + label) so we can lay indicators out
    // left-to-right with a fixed gap, wrapping is not implemented (the panel
    // is wide enough at the minimum supported width for 5 short labels).
    const float dotDiam = 9.0f;
    const float dotToLabelGap = 8.0f;
    const float groupGap = (float) CST::indicatorGap;

    float x = (float) content.getX();
    float cy = (float) content.getCentreY();

    for (auto& ind : indicators)
    {
        juce::String label = ind.label.toUpperCase();
        int labelW = font.getStringWidth (label);

        float dotCx = x + dotDiam * 0.5f;
        CollabSyncLookAndFeel::paintGlowDot (g, { dotCx, cy }, dotDiam * 0.5f, ind.colour,
                                              ind.on, ind.on && ind.pulse ? pulse : 0.0f);

        float labelX = x + dotDiam + dotToLabelGap;
        g.setColour (ind.on ? CST::cream.withAlpha (0.72f) : CST::textMuted32);
        g.drawText (label, (int) labelX, content.getY(), labelW + 4, content.getHeight(),
                    juce::Justification::centredLeft, false);

        x = labelX + (float) labelW + groupGap;
    }
}
