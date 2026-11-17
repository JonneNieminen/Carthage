// Runs every juce::UnitTest in the binary. Exit code 1 if any expectation failed.
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

int main (int argc, char* argv[])
{
    const juce::ScopedJuceInitialiser_GUI juce;
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

    std::printf ("\n%s: %d failure(s)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
