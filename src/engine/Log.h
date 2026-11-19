#pragma once

#include <juce_core/juce_core.h>

namespace carthage::log
{
    /** Carthage's own log (W38): what it was running on, the audio device, plugin loads and scans,
        warnings and errors. Kept on disk (the system's log folder, the previous run's file beside
        it) and in memory, for "Export log" and the crash report. Nothing leaves the computer
        unless the person saves or copies it, and then with the home folder written as ~.

        Any thread except an audio thread: writing takes a lock and touches the disk. */
    enum class Level { info, warning, error };

    /** Starts this run's log in `folder` as Carthage.log (the last run's becomes
        "Carthage previous.log"), and makes it JUCE's logger, so Tracktion's and JUCE's messages
        are in it too. `firstLine` says what is running (name and version). */
    void start (const juce::File& folder, const juce::String& firstLine);
    void stop();

    void write (Level, const juce::String&);
    inline void info (const juce::String& s)      { write (Level::info, s); }
    inline void warning (const juce::String& s)   { write (Level::warning, s); }
    inline void error (const juce::String& s)     { write (Level::error, s); }

    /** This run's lines so far (the newest few thousand). */
    juce::String text();

    /** The folder the log is in, and the last run's log ("" if none). */
    juce::File folder();
    juce::String previousRun();

    /** The computer, for the top of an export: system, processor, memory. */
    juce::String systemSummary();

    /** `s` with the home folder written as ~, for sharing. */
    juce::String withoutHome (const juce::String& s);

    /** A crash leaves a note (what failed and where) for the next launch to offer as a report.
        installCrashHandler writes it; takeCrashNote returns it once, then it is gone. */
    void installCrashHandler();
    juce::String takeCrashNote();
}
