#include <engine/PluginScan.h>

using namespace carthage::engine;

class PluginScanTests final : public juce::UnitTest
{
public:
    PluginScanTests() : juce::UnitTest ("Plugin scan protocol", "engine") {}

    void runTest() override
    {
        beginTest ("Descriptions survive the trip through the scanner's output, even with noise around it");
        {
            juce::PluginDescription d;
            d.name = "Amp";
            d.pluginFormatName = "VST3";
            d.fileOrIdentifier = "/Library/Audio/Plug-Ins/VST3/Amp.vst3";
            d.manufacturerName = "Someone";
            d.uniqueId = 1234;
            d.deprecatedUid = 99;

            const auto text = "plugin says hello\n" + scan::toXml ({ d }) + "\nand goodbye\n";
            const auto parsed = scan::fromXml (text);
            expectEquals (parsed.size(), 1);
            expect (parsed.getFirst().isDuplicateOf (d));
            expectEquals (parsed.getFirst().name, d.name);
        }

        beginTest ("A missing scanner is reported, not crashed into");
        {
            const auto outcome = scan::runScanner (juce::File ("/nonexistent/carthage-scan"), "VST3", "/x.vst3", 1000);
            expect (outcome.kind == scan::Outcome::Kind::couldNotStart);
            expect (! outcome.shouldBlock());
        }

        beginTest ("The blocklist and known list persist, versioned");
        {
            const auto folder = juce::File::createTempFile ("carthage-catalogue");
            juce::AudioPluginFormatManager formats;

            {
                juce::KnownPluginList known;
                PluginCatalogue catalogue (known, formats, folder);
                catalogue.block ("/bad.vst3", "crashed");
                juce::PluginDescription d;
                d.name = "Good"; d.pluginFormatName = "VST3"; d.fileOrIdentifier = "/good.vst3"; d.uniqueId = 5;
                catalogue.addScanned ({ d });
            }

            juce::KnownPluginList known;
            PluginCatalogue reopened (known, formats, folder);
            expect (reopened.isBlocked ("/bad.vst3"));
            expect (! reopened.isBlocked ("/good.vst3"));
            expectEquals (reopened.findScanned ("/good.vst3").size(), 1);
            expect (reopened.isScanned (reopened.findScanned ("/good.vst3").getFirst()));
            expect (juce::parseXML (reopened.getBlocklistFile())->getIntAttribute ("version") == PluginCatalogue::cacheFormatVersion);
            folder.deleteRecursively();
        }
    }
};

static PluginScanTests pluginScanTests;
