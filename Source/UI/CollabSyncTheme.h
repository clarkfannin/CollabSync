#pragma once
#include <JuceHeader.h>

// Design tokens transcribed verbatim from design-reference/DESIGN_HANDOFF.md.
// Keep this file free of logic — it's the single source of truth for colours,
// radii and spacing so LookAndFeel / components never hardcode a hex value.
namespace CST
{
    //======================================================================
    // Panel background gradient stops
    static const juce::Colour panelGradA { 0xff052a1c };
    static const juce::Colour panelGradB { 0xff032018 };
    static const juce::Colour panelGradC { 0xff02150e };

    // Recessed surfaces: wells, cards, disabled record button, status strip
    static const juce::Colour recessedFill { 0xff02130c };
    // Off (inactive) indicator dot background
    static const juce::Colour dotOffFill { 0xff00160f };

    // Raised button fill (shared by every neumorphic button)
    static const juce::Colour buttonFill { 0xff073c28 };

    // Text
    static const juce::Colour cream { 0xffeaf1e6 };
    inline juce::Colour creamAlpha (float a) { return cream.withAlpha (a); }
    static const juce::Colour textMuted50 = juce::Colour (0xffeaf1e6).withAlpha (0.50f);
    static const juce::Colour textMuted42 = juce::Colour (0xffeaf1e6).withAlpha (0.42f);
    static const juce::Colour textMuted34 = juce::Colour (0xffeaf1e6).withAlpha (0.34f);
    static const juce::Colour textMuted32 = juce::Colour (0xffeaf1e6).withAlpha (0.32f);
    static const juce::Colour textMuted24 = juce::Colour (0xffeaf1e6).withAlpha (0.24f);
    static const juce::Colour textMuted22 = juce::Colour (0xffeaf1e6).withAlpha (0.22f);

    // Accent colours
    static const juce::Colour mint       { 0xff5fd8a6 };
    static const juce::Colour mintBright { 0xff8ef0c2 };
    static const juce::Colour amber      { 0xffe2a15a };
    static const juce::Colour amberBright{ 0xfff0bd82 };
    static const juce::Colour red        { 0xffe8564b };
    static const juce::Colour redBright  { 0xfff2837a };

    // Waveform / piano-roll fill colours
    static const juce::Colour waveLocal { juce::Colour (0xffeaf1e6).withAlpha (0.62f) }; // "You"
    static const juce::Colour wavePeer  { juce::Colour (0xff5fd8a6).withAlpha (0.55f) }; // "Peer"

    //======================================================================
    // Radii
    constexpr float radiusButton = 13.5f; // spec says 13-14px
    constexpr float radiusWell   = 13.0f;
    constexpr float radiusCard   = 15.0f;
    constexpr float radiusRecord = 14.0f;
    constexpr float radiusPill   = 9.0f;

    // Spacing
    //
    // Neumorphic surfaces cast shadows outside their shape (the widest is the
    // raised button's "6px 6px 14px", reaching ~20px down-right). A JUCE component
    // cannot paint beyond its own bounds, so components that draw these surfaces
    // are grown by shadowMargin on every side and paint their shape inset by the
    // same amount, letting the shadow land in the reserved space instead of being
    // sliced square at the edge. Layout code positions the *visible* rectangle and
    // calls expandedForShadow() to get the bounds to actually set.
    constexpr int shadowMargin   = 20;

    // Grows a visible rectangle into the component bounds needed to paint it with
    // room for its shadows. Inverse of Rectangle::reduced (shadowMargin).
    inline juce::Rectangle<int> expandedForShadow (juce::Rectangle<int> visible)
    {
        return visible.expanded (shadowMargin);
    }

    constexpr int panelPadding   = 28;
    constexpr int dividerMargin  = 22;
    constexpr int gridGap        = 12;
    constexpr int indicatorGap   = 22;

    // Reference widths (design aid only; the real UI is freely resizable)
    constexpr int widthCompact  = 380;
    constexpr int widthStandard = 620;
    constexpr int widthWide     = 800;
    constexpr int widthMin      = 340;
    constexpr int widthMax      = 900;

    // Responsive breakpoint: generated-files grid collapses to one column below this
    constexpr int gridCollapseWidth = 520;

    //======================================================================
    // Session UI state — mirrors the handoff's idle -> hosting -> connected ->
    // recording -> finished enum. Derived every timer tick from processor
    // accessors (this project does not add new processor state).
    enum class SessionUIState
    {
        idle,
        hosting,
        connected,
        recording,
        finished
    };
}
