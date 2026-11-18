// Runs every juce::UnitTest in the binary. Exit code 1 if any expectation failed.
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

namespace
{
    /** JUCE logs to the debugger on Windows; CI needs the failures on the console. */
    struct ConsoleLogger final : juce::Logger
    {
        void logMessage (const juce::String& message) override
        {
            std::fputs ((message + "\n").toRawUTF8(), stdout);
            std::fflush (stdout);
        }
    };
}

int main (int argc, char* argv[])
{
    const juce::ScopedJuceInitialiser_GUI juce;
    ConsoleLogger logger;
    juce::Logger::setCurrentLogger (&logger);
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);

    const auto category = argc > 1 ? juce::String (argv[1]) : juce::String();

    if (category.isNotEmpty())
        runner.runTestsInCategory (category);
    else
        runner.runAllTests();

    int failures = 0;

    for (int i = 0; i < runner.getNumResults(); ++i)
        failures += runner.getResult (i)->failures;

    juce::Logger::setCurrentLogger (nullptr);
    std::printf ("\n%s: %d failure(s)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
