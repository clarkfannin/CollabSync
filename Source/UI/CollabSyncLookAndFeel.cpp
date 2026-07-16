#include "CollabSyncLookAndFeel.h"
#include "CollabSyncTheme.h"
#include "BinaryData.h"

//==============================================================================
namespace
{
    // Loads a bundled TTF from BinaryData by base filename (e.g. "DMSans_Regular_ttf").
    // Returns nullptr (caller falls back to a system font) if the resource isn't found —
    // this keeps the plugin functional even if font bundling ever breaks.
    juce::Typeface::Ptr loadTypeface (const char* data, int size)
    {
        if (data == nullptr || size <= 0)
            return nullptr;
        return juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
    }
}

//==============================================================================
CollabSyncLookAndFeel::CollabSyncLookAndFeel()
{
    sansRegular  = loadTypeface (BinaryData::DMSansRegular_ttf,  BinaryData::DMSansRegular_ttfSize);
    sansMedium   = loadTypeface (BinaryData::DMSansMedium_ttf,   BinaryData::DMSansMedium_ttfSize);
    sansSemiBold = loadTypeface (BinaryData::DMSansSemiBold_ttf, BinaryData::DMSansSemiBold_ttfSize);
    sansBold     = loadTypeface (BinaryData::DMSansBold_ttf,     BinaryData::DMSansBold_ttfSize);
    monoRegular  = loadTypeface (BinaryData::DMMonoRegular_ttf,  BinaryData::DMMonoRegular_ttfSize);
    monoMedium   = loadTypeface (BinaryData::DMMonoMedium_ttf,   BinaryData::DMMonoMedium_ttfSize);

    setColour (juce::TextEditor::backgroundColourId,     juce::Colours::transparentBlack);
    setColour (juce::TextEditor::outlineColourId,         juce::Colours::transparentBlack);
    setColour (juce::TextEditor::focusedOutlineColourId,  juce::Colours::transparentBlack);
    setColour (juce::TextEditor::textColourId,            CST::cream);
    setColour (juce::CaretComponent::caretColourId,       CST::mint);
}

CollabSyncLookAndFeel::~CollabSyncLookAndFeel() = default;

//==============================================================================
juce::Font CollabSyncLookAndFeel::sans (float size, int weight) const
{
    juce::Typeface::Ptr tf = sansRegular;
    if (weight >= 700)      tf = sansBold   != nullptr ? sansBold   : tf;
    else if (weight >= 600) tf = sansSemiBold != nullptr ? sansSemiBold : tf;
    else if (weight >= 500) tf = sansMedium  != nullptr ? sansMedium  : tf;

    if (tf != nullptr)
        return juce::Font (juce::FontOptions {}.withTypeface (tf).withHeight (size));

    // Fallback: system sans-serif, synthesised bold for weight >= 600.
    auto opts = juce::FontOptions { size };
    if (weight >= 600)
        opts = opts.withStyle ("Bold");
    return juce::Font (opts);
}

juce::Font CollabSyncLookAndFeel::mono (float size, int weight) const
{
    juce::Typeface::Ptr tf = weight >= 500 && monoMedium != nullptr ? monoMedium : monoRegular;

    if (tf != nullptr)
        return juce::Font (juce::FontOptions {}.withTypeface (tf).withHeight (size));

    return juce::Font (juce::FontOptions { size }.withName (juce::Font::getDefaultMonospacedFontName()));
}

juce::Font CollabSyncLookAndFeel::getLabelFont (juce::Label& l)
{
    juce::ignoreUnused (l);
    return sans (13.0f, 400);
}

//==============================================================================
void CollabSyncLookAndFeel::paintRaised (juce::Graphics& g, juce::Rectangle<float> bounds,
                                          float radius, juce::Colour fill, float alphaMul)
{
    if (bounds.isEmpty())
        return;

    juce::Path shape;
    shape.addRoundedRectangle (bounds, radius);

    // Dark shadow, offset down-right — "6px 6px 14px rgba(0,0,0,.5)"
    juce::DropShadow dark (juce::Colours::black.withAlpha (0.5f * alphaMul), 14, { 6, 6 });
    dark.drawForPath (g, shape);

    // Faint mint shadow, offset up-left — "-5px -5px 12px rgba(120,220,175,.07)"
    juce::DropShadow light (juce::Colour (0xff78dcaf).withAlpha (0.07f * alphaMul), 12, { -5, -5 });
    light.drawForPath (g, shape);

    g.setColour (fill.withMultipliedAlpha (alphaMul));
    g.fillPath (shape);

    // "inset 0 1px 0 rgba(150,230,190,.10)" — a hard 1px lit edge along the top,
    // zero blur. This crisp line is most of what reads as "raised"; drawing it as
    // a soft vertical fade washes the top third of the button instead.
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (shape);

        juce::Path lip (shape);
        lip.applyTransform (juce::AffineTransform::translation (0.0f, 1.0f));

        g.setColour (juce::Colour (0xff96e6c4).withAlpha (0.10f * alphaMul));
        g.strokePath (lip, juce::PathStrokeType (2.0f));
    }
}

void CollabSyncLookAndFeel::paintInsetShadow (juce::Graphics& g, const juce::Path& shape,
                                              juce::Point<float> offset, float blur, juce::Colour colour)
{
    if (blur <= 0.0f || colour.isTransparent())
        return;

    // Walk outward from the border in 1px steps. Each step strokes the outline
    // (translated by the offset) at a widening thickness with alpha falling off
    // toward zero, so the strokes accumulate into a soft band densest against the
    // edge — the falloff CSS's blur radius produces. The caller's clip keeps the
    // outer half of each stroke off the surface, which is what makes the band sit
    // inside the shape.
    const int steps = juce::jmax (1, (int) std::ceil (blur));

    for (int i = steps; i >= 1; --i)
    {
        float t = (float) i / (float) steps;   // 1 at the outermost, faintest step

        // Quadratic falloff approximates a Gaussian closely enough at these radii
        // and keeps the edge itself near full strength.
        float alpha = colour.getFloatAlpha() * (1.0f - t) * (1.0f - t);
        if (alpha <= 0.001f)
            continue;

        juce::Path outline (shape);
        outline.applyTransform (juce::AffineTransform::translation (offset.x, offset.y));

        g.setColour (colour.withAlpha (alpha));
        g.strokePath (outline, juce::PathStrokeType (t * blur * 2.0f));
    }
}

void CollabSyncLookAndFeel::paintRecessed (juce::Graphics& g, juce::Rectangle<float> bounds,
                                            float radius, juce::Colour fill)
{
    if (bounds.isEmpty())
        return;

    juce::Path shape;
    shape.addRoundedRectangle (bounds, radius);

    g.setColour (fill);
    g.fillPath (shape);

    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (shape);

    // "inset 5px 5px 11px rgba(0,0,0,.75)" — dark band along the top-left edge.
    paintInsetShadow (g, shape, { 5.0f, 5.0f }, 11.0f, juce::Colours::black.withAlpha (0.75f));

    // "inset -3px -3px 9px rgba(95,216,166,.07)" — faint mint along the bottom-right.
    paintInsetShadow (g, shape, { -3.0f, -3.0f }, 9.0f, CST::mint.withAlpha (0.07f));
}

//==============================================================================
float CollabSyncLookAndFeel::pulsePhase (double periodSeconds)
{
    if (periodSeconds <= 0.0)
        return 0.0f;

    double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    double t   = std::fmod (now, periodSeconds) / periodSeconds; // 0..1
    // ease-in-out triangle via a half-sine, matching the CSS keyframe's
    // 0% -> 50% -> 100% (rest -> peak -> rest) shape.
    return (float) std::sin (t * juce::MathConstants<double>::pi);
}

void CollabSyncLookAndFeel::paintGlowDot (juce::Graphics& g, juce::Point<float> centre, float radius,
                                           juce::Colour colour, bool on, float pulseAmount)
{
    auto dotBounds = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre);

    if (! on)
    {
        g.setColour (CST::dotOffFill);
        g.fillEllipse (dotBounds);

        juce::Graphics::ScopedSaveState save (g);
        juce::Path circle;
        circle.addEllipse (dotBounds);
        g.reduceClipRegion (circle);
        juce::ColourGradient grad (juce::Colours::black.withAlpha (0.5f),
                                    dotBounds.getX(), dotBounds.getY(),
                                    juce::Colours::transparentBlack,
                                    dotBounds.getCentreX(), dotBounds.getCentreY(), true);
        g.setGradientFill (grad);
        g.fillEllipse (dotBounds);
        return;
    }

    // Glow halo — "0 0 6px <colour>" at rest, "0 0 12px 2px" at the pulse midpoint.
    // Built from many thin rings: at three rings the steps land far enough apart
    // to read as concentric discs with visible edges rather than a soft halo.
    {
        float glowRadius = 6.0f + pulseAmount * 6.0f;   // CSS blur: 6px -> 12px
        float spread     = pulseAmount * 2.0f;          // CSS spread: 0 -> 2px
        const int rings  = 24;

        for (int i = rings; i >= 1; --i)
        {
            float t  = (float) i / (float) rings;       // 1 = outermost
            float rr = radius + spread + glowRadius * t;

            // Quadratic falloff, matching the blur profile used for the inset
            // shadows so glow and bevel share a visual language.
            float alpha = 0.22f * (1.0f - t) * (1.0f - t);

            g.setColour (colour.withAlpha (alpha));
            g.fillEllipse (juce::Rectangle<float> (rr * 2.0f, rr * 2.0f).withCentre (centre));
        }
    }

    g.setColour (colour);
    g.fillEllipse (dotBounds);

    // Pulse midpoint also dips opacity to ~.6 in the CSS keyframe.
    if (pulseAmount > 0.0f)
    {
        g.setColour (CST::dotOffFill.withAlpha (pulseAmount * 0.32f));
        g.fillEllipse (dotBounds);
    }
}
