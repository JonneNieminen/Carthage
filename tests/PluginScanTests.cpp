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

        beginTest ("Mach-O headers: thin, universal, and not Mach-O");
        {
            juce::TemporaryFile thin, fat, text;
            {
                juce::FileOutputStream o (thin.getFile());
                o.writeInt (static_cast<int> (0xfeedfacf));      // little-endian thin 64-bit
                o.writeInt (0x01000007);                         // x86_64
            }
            {
                juce::FileOutputStream o (fat.getFile());
                o.writeIntBigEndian (static_cast<int> (0xcafebabe));
                o.writeIntBigEndian (2);
                o.writeIntBigEndian (0x01000007); for (int i = 0; i < 4; ++i) o.writeIntBigEndian (0);
                o.writeIntBigEndian (0x0100000c); for (int i = 0; i < 4; ++i) o.writeIntBigEndian (0);
            }
            text.getFile().replaceWithText ("not a binary");

            expectEquals (carthage::engine::scan::machOArchitectures (thin.getFile()).joinIntoString (","), juce::String ("x86_64"));
            expectEquals (carthage::engine::scan::machOArchitectures (fat.getFile()).joinIntoString (","), juce::String ("x86_64,arm64"));
            expect (carthage::engine::scan::machOArchitectures (text.getFile()).isEmpty());
            expect (carthage::engine::scan::machOArchitectures (juce::File ("/nonexistent")).isEmpty());

           #if JUCE_MAC
            // This test program itself is built for the host.
            const auto own = carthage::engine::scan::machOArchitectures (juce::File::getSpecialLocation (juce::File::currentExecutableFile));
            expect (own.contains (carthage::engine::scan::hostArchitecture()), own.joinIntoString (","));
           #endif
        }
    }
};

static PluginScanTests pluginScanTests;
