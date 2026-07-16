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

    grainTexture = createGrainTexture();

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
juce::Image CollabSyncLookAndFeel::createGrainTexture()
{
    const int size = 128;
    juce::Image img (juce::Image::ARGB, size, size, true);
    juce::Random rnd (0x9E3779B9);

    juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < size; ++y)
    {
        for (int x = 0; x < size; ++x)
        {
            auto v = (juce::uint8) rnd.nextInt (256);
            bd.setPixelColour (x, y, juce::Colour (v, v, v));
        }
    }
    return img;
}

void CollabSyncLookAndFeel::paintGrainOverlay (juce::Graphics& g, juce::Rectangle<int> area, float opacity) const
{
    if (opacity <= 0.0f || area.isEmpty())
        return;

    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (area);
    g.setTiledImageFill (grainTexture, area.getX(), area.getY(), opacity);
    g.fillRect (area);
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

    // Soft top highlight — approximates "inset 0 1px 0 rgba(150,230,190,.10)"
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (shape);
        juce::ColourGradient grad (juce::Colour (0xff96e6c4).withAlpha (0.10f * alphaMul),
                                    bounds.getCentreX(), bounds.getY(),
                                    juce::Colour (0xff96e6c4).withAlpha (0.0f),
                                    bounds.getCentreX(), bounds.getY() + juce::jmax (6.0f, bounds.getHeight() * 0.3f),
                                    false);
        g.setGradientFill (grad);
        g.fillRect (bounds);
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

    // Dark inset shadow emanating from the top-left — "inset 5px 5px 11px rgba(0,0,0,.72-.75)"
    juce::ColourGradient dark (juce::Colours::black.withAlpha (0.65f),
                                bounds.getX(), bounds.getY(),
                                juce::Colours::black.withAlpha (0.0f),
                                bounds.getX() + bounds.getWidth() * 0.65f,
                                bounds.getY() + bounds.getHeight() * 0.65f,
                                false);
    g.setGradientFill (dark);
    g.fillRect (bounds);

    // Faint mint inset highlight from the bottom-right — "inset -3px -3px 9px rgba(95,216,166,.07)"
    juce::ColourGradient lightG (CST::mint.withAlpha (0.10f),
                                  bounds.getRight(), bounds.getBottom(),
                                  CST::mint.withAlpha (0.0f),
                                  bounds.getRight() - bounds.getWidth() * 0.6f,
                                  bounds.getBottom() - bounds.getHeight() * 0.6f,
                                  false);
    g.setGradientFill (lightG);
    g.fillRect (bounds);
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

    // Glow halo — "0 0 8px <color>", expanding to "0 0 12px 2px" at the pulse midpoint.
    float glowExtra = 3.0f + pulseAmount * 6.0f;
    for (int i = 3; i >= 1; --i)
    {
        float t  = (float) i / 3.0f;
        float rr = radius + glowExtra * t;
        g.setColour (colour.withAlpha (0.10f * (1.0f - t) + 0.05f));
        g.fillEllipse (juce::Rectangle<float> (rr * 2.0f, rr * 2.0f).withCentre (centre));
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
