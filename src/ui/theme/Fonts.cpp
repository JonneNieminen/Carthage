#include "Theme.h"

#include <CarthageFonts.h>

namespace carthage::theme
{
    namespace
    {
        struct Faces
        {
            juce::Typeface::Ptr regular, semibold, denseRegular, denseSemibold, mono;

            Faces()
            {
                const auto load = [] (const char* data, int size) { return juce::Typeface::createSystemTypefaceFor (data, static_cast<size_t> (size)); };
                regular = load (CarthageFonts::BarlowRegular_ttf, CarthageFonts::BarlowRegular_ttfSize);
                semibold = load (CarthageFonts::BarlowSemiBold_ttf, CarthageFonts::BarlowSemiBold_ttfSize);
                denseRegular = load (CarthageFonts::BarlowSemiCondensedRegular_ttf, CarthageFonts::BarlowSemiCondensedRegular_ttfSize);
                denseSemibold = load (CarthageFonts::BarlowSemiCondensedSemiBold_ttf, CarthageFonts::BarlowSemiCondensedSemiBold_ttfSize);
                mono = load (CarthageFonts::IBMPlexMonoRegular_ttf, CarthageFonts::IBMPlexMonoRegular_ttfSize);
            }
        };

        // Made once, on first use (the message thread draws).
        const Faces& faces()
        {
            static const Faces loaded;
            return loaded;
        }

        juce::Font make (const juce::Typeface::Ptr& face, float size)
        {
            return juce::Font (juce::FontOptions (face).withPointHeight (size));
        }
    }

    juce::Font uiFont (float size, bool medium)     { return make (medium ? faces().semibold : faces().regular, size); }
    juce::Font denseFont (float size, bool medium)  { return make (medium ? faces().denseSemibold : faces().denseRegular, size); }
    juce::Font timeFont (float size)                { return make (faces().mono, size); }
    juce::Typeface::Ptr defaultTypeface()           { return faces().regular; }
}
