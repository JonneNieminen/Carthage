#pragma once

// The Carthage theme in C++. It mirrors the dark column of design/tokens.css value for value, under
// the same names (--bg2 -> theme::bg2, --sky-deep -> theme::skyDeep, --lane-sky -> theme::laneSky).
// Change a value in tokens.css first, then here; tests/ThemeTests.cpp fails if the two disagree.
// Components take every colour, size and radius from this header, never from literals.

#include <juce_graphics/juce_graphics.h>

#include <array>
#include <string_view>

namespace carthage::theme
{
    using juce::Colour;

    constexpr juce::uint32 rgb (juce::uint32 hex) noexcept     { return 0xff000000u | hex; }

    /** rgba(r, g, b, a) with the alpha rounded the way a browser stores it, to 8 bits. */
    constexpr juce::uint32 rgba (juce::uint32 hex, double alpha) noexcept
    {
        // alpha is 0..1, so adding a half rounds correctly; std::lround is not constexpr on every compiler.
        return (static_cast<juce::uint32> (alpha * 255.0 + 0.5) << 24) | hex;   // NOLINT(bugprone-incorrect-roundings)
    }

    // Harbour palette
    inline const Colour graphite    { rgb (0x1c2127) };
    inline const Colour steel       { rgb (0x3a434d) };
    inline const Colour grey        { rgb (0x8b96a1) };
    inline const Colour mist        { rgb (0xd5dce3) };
    inline const Colour ice         { rgb (0xeef2f5) };
    inline const Colour sky         { rgb (0x8fd0f5) };
    inline const Colour skyDeep     { rgb (0x4fa8dc) };

    // Surfaces, back to front
    inline const Colour well        { rgb (0x14181c) };
    inline const Colour bg          { rgb (0x1c2127) };
    inline const Colour bg2         { rgb (0x232a31) };
    inline const Colour card        { rgb (0x2b333b) };
    inline const Colour hover       { rgb (0x353e47) };
    inline const Colour line        { rgba (0xffffff, 0.08) };
    inline const Colour lineStrong  { rgba (0xffffff, 0.16) };

    // Text
    inline const Colour text        { rgb (0xeef2f5) };
    inline const Colour muted       { rgb (0x9aa5b0) };
    inline const Colour faint       { rgb (0x5d6873) };

    // The one accent: whatever is live
    inline const Colour accent      { rgb (0x8fd0f5) };
    inline const Colour accentText  { rgb (0x8fd0f5) };
    inline const Colour onAccent    { rgb (0x10242f) };
    inline const Colour ring        { rgb (0xd5dce3) };
    inline const Colour tile        { rgb (0x1c2127) };

    // Track lanes
    inline const Colour laneSky     { rgb (0x8fd0f5) };
    inline const Colour laneTide    { rgb (0x7fcfc2) };
    inline const Colour laneKelp    { rgb (0xa8cf8c) };
    inline const Colour laneSand    { rgb (0xe0c27e) };
    inline const Colour laneCoral   { rgb (0xec9c8b) };
    inline const Colour laneMurex   { rgb (0xb9a0ea) };
    inline const Colour laneRose    { rgb (0xe597b8) };
    inline const Colour laneSlate   { rgb (0x8b96a1) };

    // Signal states: meters, record arm, warnings only
    inline const Colour ok          { rgb (0x8fd6a4) };
    inline const Colour warn        { rgb (0xe8c46a) };
    inline const Colour clip        { rgb (0xf0736a) };

    // Type sizes (px)
    constexpr float textXs = 11.0f, textSm = 12.0f, textMd = 13.0f, textLg = 15.0f, textXl = 20.0f, text2xl = 28.0f;

    // Shape (px). Radii follow nesting.
    constexpr float radiusXs = 3.0f, radiusSm = 6.0f, radiusMd = 10.0f, radiusLg = 14.0f, stroke = 1.0f;

    // Spacing, 4 px grid
    constexpr int space1 = 4, space2 = 8, space3 = 12, space4 = 16, space6 = 24, space8 = 32;

    // Motion (ms)
    constexpr int fast = 120, slow = 220;

    /** Font roles (W45), bundled so Carthage reads the same on every machine (theme/Fonts.cpp):
        the interface in Barlow, dense rows (lane headers, the toolbar) in Barlow Semi Condensed,
        and figures that must not shift (clocks, meters, keycaps) in IBM Plex Mono. Sizes are em
        sizes, JUCE's point height. */
    juce::Font uiFont (float size = textMd, bool medium = false);
    juce::Font denseFont (float size = textMd, bool medium = false);
    juce::Font timeFont (float size = textMd);

    /** Barlow, for JUCE's own text (labels, buttons, menus) through the look and feel. */
    juce::Typeface::Ptr defaultTypeface();

    /** Every mirrored token by its tokens.css name, for the consistency test. */
    struct ColourToken { std::string_view name; const Colour& value; };
    struct SizeToken { std::string_view name; float value; };

    inline const std::array<ColourToken, 33>& colourTokens()
    {
        static const std::array<ColourToken, 33> t {{
            { "--graphite", graphite }, { "--steel", steel }, { "--grey", grey }, { "--mist", mist },
            { "--ice", ice }, { "--sky", sky }, { "--sky-deep", skyDeep },
            { "--well", well }, { "--bg", bg }, { "--bg2", bg2 }, { "--card", card }, { "--hover", hover },
            { "--line", line }, { "--line-strong", lineStrong },
            { "--text", text }, { "--muted", muted }, { "--faint", faint },
            { "--accent", accent }, { "--accent-text", accentText }, { "--on-accent", onAccent },
            { "--ring", ring }, { "--tile", tile },
            { "--lane-sky", laneSky }, { "--lane-tide", laneTide }, { "--lane-kelp", laneKelp },
            { "--lane-sand", laneSand }, { "--lane-coral", laneCoral }, { "--lane-murex", laneMurex },
            { "--lane-rose", laneRose }, { "--lane-slate", laneSlate },
            { "--ok", ok }, { "--warn", warn }, { "--clip", clip },
        }};
        return t;
    }

    inline const std::array<SizeToken, 19>& sizeTokens()
    {
        static const std::array<SizeToken, 19> t {{
            { "--text-xs", textXs }, { "--text-sm", textSm }, { "--text-md", textMd }, { "--text-lg", textLg },
            { "--text-xl", textXl }, { "--text-2xl", text2xl },
            { "--radius-xs", radiusXs }, { "--radius-sm", radiusSm }, { "--radius-md", radiusMd },
            { "--radius-lg", radiusLg }, { "--stroke", stroke },
            { "--space-1", static_cast<float> (space1) }, { "--space-2", static_cast<float> (space2) }, { "--space-3", static_cast<float> (space3) },
            { "--space-4", static_cast<float> (space4) }, { "--space-6", static_cast<float> (space6) }, { "--space-8", static_cast<float> (space8) },
            { "--fast", static_cast<float> (fast) }, { "--slow", static_cast<float> (slow) },
        }};
        return t;
    }
}
