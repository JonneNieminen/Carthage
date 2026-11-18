#include <engine/PluginLibrary.h>

using carthage::engine::fuzzyScore;
using carthage::engine::PluginLibrary;

class PluginLibraryTests final : public juce::UnitTest
{
public:
    PluginLibraryTests() : juce::UnitTest ("Plugin library", "engine") {}

    static juce::PluginDescription plugin (const juce::String& name, const juce::String& vendor,
                                           const juce::String& format, bool instrument)
    {
        juce::PluginDescription d;
        d.name = name;
        d.manufacturerName = vendor;
        d.pluginFormatName = format;
        d.isInstrument = instrument;
        d.fileOrIdentifier = "/" + vendor + "/" + name + "." + format;
        d.uniqueId = d.deprecatedUid = static_cast<int> (std::hash<std::string>{} ((name + format).toStdString()) & 0x7fffffff);
        return d;
    }

    static PluginLibrary::Filter filter (const juce::String& query, const juce::String& format = {}, const juce::String& vendor = {},
                                         PluginLibrary::Kind kind = PluginLibrary::Kind::any, bool favouritesOnly = false)
    {
        PluginLibrary::Filter f;
        f.query = query;
        f.format = format;
        f.vendor = vendor;
        f.kind = kind;
        f.favouritesOnly = favouritesOnly;
        return f;
    }

    void runTest() override
    {
        beginTest ("Fuzzy matching: letters in order, word starts and runs rank higher");
        {
            expect (fuzzyScore ("nam", "Neural Amp Modeler") > 0);
            expect (fuzzyScore ("xyz", "Neural Amp Modeler") < 0);
            expect (fuzzyScore ("mna", "Neural Amp Modeler") < 0, "order matters");
            expect (fuzzyScore ("NEURAL", "Neural Amp Modeler") > 0, "case does not matter");
            expect (fuzzyScore ("amp", "AmpKnob - RevC") > fuzzyScore ("amp", "Neural Amp Modeler"), "prefix beats a later word");
            expect (fuzzyScore ("nam", "Neural Amp Modeler") > fuzzyScore ("nam", "Pianoteq Grand Manual"), "word starts beat scattered letters");
            expect (fuzzyScore ("proq", "Pro-Q 4") > 0, "punctuation is skipped");
            expect (fuzzyScore ("amp", "Neural Amp Modeler") > fuzzyScore ("amp", "AUDynamicsProcessor"),
                    "a whole word beats letters scattered over a name");
            expect (fuzzyScore ("verb", "ValhallaVintageVerb") > fuzzyScore ("verb", "Valhalla Vintage Echo Room Bus"),
                    "a camel-case word counts as a word");
            expect (fuzzyScore ("", "anything") == 0);
        }

        juce::TemporaryFile folder;
        folder.getFile().createDirectory();

        juce::Array<juce::PluginDescription> list {
            plugin ("Neural Amp Modeler", "Steven Atkinson", "VST3", false),
            plugin ("Neural Amp Modeler", "Steven Atkinson", "AudioUnit", false),
            plugin ("AmpKnob - RevC", "Bogren Digital", "AudioUnit", false),
            plugin ("Pro-Q 4", "FabFilter", "VST3", false),
            plugin ("Pianoteq 9", "Modartt", "VST3", true),
            plugin ("Diva", "u-he", "AudioUnit", true)
        };

        beginTest ("Find: query ranks, filters narrow");
        {
            PluginLibrary lib (folder.getFile());
            lib.setPlugins (list);
            const auto& e = lib.getEntries();

            auto names = [&] (const std::vector<size_t>& idx)
            {
                juce::StringArray out;
                for (auto i : idx) out.add (e[i].description.name + "/" + e[i].description.pluginFormatName);
                return out;
            };

            expectEquals (static_cast<int> (lib.find ({}).size()), 6);
            expectEquals (names (lib.find (filter ("amp"))).joinIntoString (","),
                          juce::String ("AmpKnob - RevC/AudioUnit,Neural Amp Modeler/AudioUnit,Neural Amp Modeler/VST3"));
            expectEquals (names (lib.find (filter ("fab q"))).joinIntoString (","), juce::String ("Pro-Q 4/VST3"), "a word can match the vendor");
            expectEquals (names (lib.find (filter ("nam", "VST3"))).joinIntoString (","), juce::String ("Neural Amp Modeler/VST3"));
            expectEquals (static_cast<int> (lib.find (filter ({}, {}, {}, PluginLibrary::Kind::instruments)).size()), 2);
            expectEquals (static_cast<int> (lib.find (filter ({}, {}, {}, PluginLibrary::Kind::effects)).size()), 4);
            expectEquals (names (lib.find (filter ({}, {}, "u-he"))).joinIntoString (","), juce::String ("Diva/AudioUnit"));
            expectEquals (lib.getVendors().joinIntoString (","), juce::String ("Bogren Digital,FabFilter,Modartt,Steven Atkinson,u-he"));
        }

        beginTest ("Favourites and last used survive a restart and a rescan");
        {
            juce::String ampKey, divaKey;
            {
                PluginLibrary lib (folder.getFile());
                lib.setPlugins (list);
                ampKey = lib.getEntries()[0].key;   // AmpKnob, first by name
                divaKey = lib.find (filter ("diva")).empty() ? juce::String() : lib.getEntries()[lib.find (filter ("diva"))[0]].key;
                lib.setFavourite (ampKey, true);
                lib.markUsed (ampKey);
                juce::Thread::sleep (5);
                lib.markUsed (divaKey);
            }

            PluginLibrary reopened (folder.getFile());
            reopened.setPlugins (list);
            expect (reopened.findEntry (ampKey) != nullptr && reopened.findEntry (ampKey)->favourite);
            expectEquals (static_cast<int> (reopened.find (filter ({}, {}, {}, PluginLibrary::Kind::any, true)).size()), 1);
            const auto recent = reopened.recent (5);
            expectEquals (static_cast<int> (recent.size()), 2);
            expectEquals (reopened.getEntries()[recent[0]].key, divaKey, "newest first");
        }

        beginTest ("Search over 2000 plugins takes well under a keystroke (16 ms)");
        {
            juce::Array<juce::PluginDescription> big;
            juce::Random rng (11);
            const char* words[] = { "Amp", "Verb", "Delay", "Comp", "Pro", "Neural", "Tape", "Room", "Synth", "Q", "Drive", "Bass" };

            for (int i = 0; i < 2000; ++i)
                big.add (plugin (juce::String (words[rng.nextInt (12)]) + " " + words[rng.nextInt (12)] + " " + juce::String (i),
                                 "Vendor " + juce::String (i % 60), i % 2 ? "VST3" : "AudioUnit", i % 7 == 0));

            PluginLibrary lib (folder.getFile());
            lib.setPlugins (big);

            double worst = 0.0;
            for (const auto* q : { "a", "am", "amp", "amp v", "neural tape 12", "zzz" })
            {
                const auto t0 = juce::Time::getMillisecondCounterHiRes();
                const auto hits = lib.find (filter (q));
                worst = std::max (worst, juce::Time::getMillisecondCounterHiRes() - t0);
                juce::ignoreUnused (hits);
            }

            logMessage ("worst search over 2000 plugins: " + juce::String (worst, 2) + " ms");
            expectLessThan (worst, 16.0);
        }
    }
};

static PluginLibraryTests pluginLibraryTests;
