#include <engine/Log.h>

using namespace carthage;

class LogTests final : public juce::UnitTest
{
public:
    LogTests() : juce::UnitTest ("Log", "app") {}

    void runTest() override
    {
        juce::TemporaryFile holder;
        const auto& folder = holder.getFile();

        beginTest ("Lines go to the file and to memory; JUCE's own messages too");
        {
            log::start (folder, "Carthage test 1");
            log::warning ("first warning");
            juce::Logger::writeToLog ("from JUCE");
            expect (log::text().contains ("Warning: first warning"));
            expect (log::text().contains ("from JUCE"));
            log::stop();
            expect (folder.getChildFile ("Carthage.log").loadFileAsString().contains ("Closed normally"));
        }

        beginTest ("The next start keeps the last run's log beside it");
        {
            log::start (folder, "Carthage test 2");
            expect (log::previousRun().contains ("first warning"));
            expect (! log::text().contains ("first warning"), "this run starts empty");
            log::stop();
        }

        beginTest ("Exports show the home folder as ~");
        {
            const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName();
            expectEquals (log::withoutHome (home + "/Music/song.carthage"), juce::String ("~/Music/song.carthage"));
        }

        beginTest ("A crash note is offered once");
        {
            log::start (folder, "Carthage test 3");
            folder.getChildFile ("Carthage crash.txt").replaceWithText ("stopped at test");
            expectEquals (log::takeCrashNote(), juce::String ("stopped at test"));
            expect (log::takeCrashNote().isEmpty(), "gone after it was taken");
            log::stop();
        }

        folder.deleteRecursively();
    }
};

static LogTests logTests;
