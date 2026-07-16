#pragma once
#include <JuceHeader.h>

// Custom LookAndFeel for the CollabSync neumorphic redesign.
//
// Owns:
//   - The bundled DM Sans / DM Mono typefaces (loaded from BinaryData; falls
//     back to JUCE's default sans/mono system faces if the binary data is
//     unavailable for some reason).
//   - The procedurally-generated grain/noise texture used for the panel's
//     film-grain overlay.
//   - Static paint helpers that draw the two neumorphic surface treatments
//     used everywhere in the UI: "raised" (buttons) and "recessed" (wells,
//     cards, the status strip, the disabled record button) — plus a glow-dot
//     helper for the status indicators.
//
// These helpers are deliberately exposed as free paint functions (not
// LookAndFeel_V4 virtual overrides) because every neumorphic surface in this
// design needs per-instance colour/state control (record button's 3 states,
// per-role button text colour/weight, etc). Routing all of that through the
// handful of LookAndFeel_V4 button/label virtuals would mean smuggling state
// through Component properties. Housing the actual drawing code on the
// LookAndFeel subclass (rather than scattering it across components) still
// keeps a single place that owns "what a neumorphic surface looks like".
class CollabSyncLookAndFeel : public juce::LookAndFeel_V4
{
public:
    CollabSyncLookAndFeel();
    ~CollabSyncLookAndFeel() override;

    //======================================================================
    // Fonts (DM Sans / DM Mono, falling back to system faces if the bundled
    // binary data typefaces failed to load).
    juce::Font sans (float size, int weight = 400) const;   // weight: 400/500/600/700
    juce::Font mono (float size, int weight = 400) const;   // weight: 400/500

    // LookAndFeel_V4 overrides so plain juce::Label instances (used for plain,
    // non-glowing text) pick up DM Sans automatically.
    juce::Font getLabelFont (juce::Label&) override;

    //======================================================================
    // Grain overlay — tiled noise texture drawn at low opacity over the panel.
    void paintGrainOverlay (juce::Graphics& g, juce::Rectangle<int> area, float opacity) const;

    //======================================================================
    // Neumorphic surface helpers (static: no LookAndFeel state required).

    // Raised button chrome: dark drop-shadow (down-right) + faint mint
    // drop-shadow (up-left) + flat fill + soft top highlight.
    static void paintRaised (juce::Graphics& g, juce::Rectangle<float> bounds,
                              float radius, juce::Colour fill, float alphaMul = 1.0f);

    // Recessed well/card chrome: flat fill + inward corner shading that
    // approximates CSS's inset box-shadow (dark from top-left, faint mint
    // highlight from bottom-right).
    static void paintRecessed (juce::Graphics& g, juce::Rectangle<float> bounds,
                                float radius, juce::Colour fill);

    // Status/record dot. `pulseAmount` is 0..1 (0 = rest, 1 = pulse midpoint);
    // callers drive this from a running clock for the "dotpulse" animation.
    static void paintGlowDot (juce::Graphics& g, juce::Point<float> centre, float radius,
                               juce::Colour colour, bool on, float pulseAmount = 0.0f);

    // 0..1 "dotpulse"-style triangular easing for a repeating period (seconds).
    static float pulsePhase (double periodSeconds);

private:
    juce::Typeface::Ptr sansRegular, sansMedium, sansSemiBold, sansBold;
    juce::Typeface::Ptr monoRegular, monoMedium;

    juce::Image grainTexture;
    static juce::Image createGrainTexture();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CollabSyncLookAndFeel)
};
