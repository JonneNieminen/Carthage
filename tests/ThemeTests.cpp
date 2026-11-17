// The theme header must mirror design/tokens.css value for value (AGENTS.md, "UI").
#include <ui/theme/Theme.h>

#include <map>

class ThemeTests final : public juce::UnitTest
{
public:
    ThemeTests() : juce::UnitTest ("Theme mirrors tokens.css", "ui") {}

    static std::map<juce::String, juce::String> darkTokens()
    {
        const auto css = juce::File (CARTHAGE_SOURCE_DIR).getChildFile ("design/tokens.css").loadFileAsString();
        const auto root = css.fromFirstOccurrenceOf (":root {", false, false).upToFirstOccurrenceOf ("\n}", false, false);
        std::map<juce::String, juce::String> tokens;

        for (auto line : juce::StringArray::fromLines (root))
        {
            line = line.upToFirstOccurrenceOf ("/*", false, false).trim();

            if (line.startsWith ("--") && line.containsChar (':'))
                tokens[line.upToFirstOccurrenceOf (":", false, false).trim()]
                    = line.fromFirstOccurrenceOf (":", false, false).upToLastOccurrenceOf (";", false, false).trim();
        }

        return tokens;
    }

    static juce::Colour parseColour (const juce::String& v)
    {
        if (v.startsWith ("#"))
            return juce::Colour (0xff000000u | static_cast<juce::uint32> (v.substring (1).getHexValue32()));

        if (v.startsWith ("rgba("))
        {
            auto parts = juce::StringArray::fromTokens (v.fromFirstOccurrenceOf ("(", false, false).upToFirstOccurrenceOf (")", false, false), ",", {});
            parts.trim();
            return juce::Colour (static_cast<juce::uint8> (parts[0].getIntValue()), static_cast<juce::uint8> (parts[1].getIntValue()),
                                 static_cast<juce::uint8> (parts[2].getIntValue()),
                                 static_cast<juce::uint8> (parts[3].getDoubleValue() * 255.0 + 0.5));
        }

        return {};
    }

    void runTest() override
    {
        const auto tokens = darkTokens();

        beginTest ("tokens.css was found and parsed");
        expectGreaterThan (static_cast<int> (tokens.size()), 50);

        beginTest ("Every colour token in the header equals tokens.css");
        for (const auto& t : carthage::theme::colourTokens())
        {
            const juce::String tokenName (t.name.data(), t.name.size());
            const auto it = tokens.find (tokenName);
            expect (it != tokens.end(), tokenName + " is not in tokens.css");

            if (it != tokens.end())
                expectEquals (t.value.toString(), parseColour (it->second).toString(), tokenName);
        }

        beginTest ("Every size, radius, spacing and duration token equals tokens.css");
        for (const auto& t : carthage::theme::sizeTokens())
        {
            const juce::String tokenName (t.name.data(), t.name.size());
            const auto it = tokens.find (tokenName);
            expect (it != tokens.end(), tokenName + " is not in tokens.css");

            if (it != tokens.end())
                expectEquals (t.value, static_cast<float> (it->second.retainCharacters ("0123456789.").getDoubleValue()), tokenName);
        }

        beginTest ("Every colour in tokens.css has a mirror in the header");
        for (const auto& [tokenName, value] : tokens)
        {
            if (! (value.startsWith ("#") || value.startsWith ("rgba(")))
                continue;

            bool found = false;
            for (const auto& t : carthage::theme::colourTokens())
                found = found || tokenName == juce::String (t.name.data(), t.name.size());

            expect (found, tokenName + " is in tokens.css but not in Theme.h");
        }
    }
};

static ThemeTests themeTests;
