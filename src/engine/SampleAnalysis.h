#pragma once

#include "SampleLibrary.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace carthage::engine
{
    /** Key and tempo of a sound (F20: "detecting a sample's key and tempo in the sample browser").
        Pure functions on audio, plus a worker that analyses the library's sounds in the background.
        Tested in tests/SampleTests.cpp on sounds of known key and tempo.

        Tempo: an onset strength curve (spectral flux), its autocorrelation over 60 to 200 bpm with
        a gentle preference for the middle, then, for a loop, the tempo that makes its length a
        whole number of bars. Reported only for sounds of a second and a half or more whose beat is
        clear; one-shots have none.

        Key: a chromagram (the energy of each of the twelve notes, 55 Hz to 5 kHz) correlated with
        the Krumhansl-Kessler major and minor profiles. Reported only when the best key clearly
        beats the others and the sound is tonal; drums and noise have none. A relative major and
        minor share their notes, so a sound that never leans on either can come out as the other. */
    namespace analysis
    {
        /** Mono audio at `rate`. */
        SampleLibrary::Analysis analyse (const float* samples, int numSamples, double rate);

        /** Reads the start of a file (up to a minute), mixed to mono, and analyses it. */
        std::optional<SampleLibrary::Analysis> analyseFile (const juce::File&);
    }

    /** Analyses every sound in the library that has no analysis yet, one at a time, on its own
        low-priority thread, and stores the results in the library (message thread). Starts again
        whenever the index changes. */
    class SampleAnalyser final : private juce::Thread
    {
    public:
        explicit SampleAnalyser (SampleLibrary&);
        ~SampleAnalyser() override;

        /** The library's index changed: look for sounds to analyse. Message thread. */
        void update();

    private:
        SampleLibrary& library;
        juce::Array<juce::File> queue;   // message thread fills; the worker copies under the lock
        juce::CriticalSection queueLock;
        std::shared_ptr<bool> alive = std::make_shared<bool> (true);

        void run() override;
    };
}
