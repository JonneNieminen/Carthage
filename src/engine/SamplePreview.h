#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <mutex>

namespace carthage::engine
{
    /** Plays a sample from the browser to the main output, at its own volume (W30).

        A reader thread decodes the file, converts it to the device's rate and fills a lock-free
        FIFO; the audio thread only reads that FIFO and adds it, scaled, into the outputs
        (AudioCallback calls render() after the engine). Starting another sound never makes the
        audio thread wait: the reader raises a flush number, the audio thread drops what is
        queued and answers with the same number, and only then does the reader fill again.

        A sound plays to its end (or until stop()), as recorded: no tempo change here. */
    class SamplePreview final : private juce::Thread
    {
    public:
        SamplePreview();
        ~SamplePreview() override;

        /** Message thread, when the device's rate changes: what is playing stops, and later
            sounds are converted to this rate. Allocates nothing: the FIFO is fixed (a second at
            192 kHz), so the audio thread never sees it change size. */
        void prepare (double deviceSampleRate);

        /** Message thread. Starts `file` from the beginning, replacing anything playing. */
        void play (const juce::File&);
        void stop();

        /** Message thread. Volume of the preview alone, in dB (-60 is silent). */
        void setVolumeDb (float);
        float getVolumeDb() const noexcept      { return volumeDb.load (std::memory_order_relaxed); }

        /** Message thread, for the browser's progress fill. */
        bool isPlaying() const noexcept;
        juce::File getFile() const;
        double getProgress() const noexcept;    ///< 0..1 of the sound heard so far

        /** Audio thread: adds the next `numSamples` into the two given outputs (either may be
            null; a mono sound goes to both). Never allocates, locks or waits. */
        void render (float* left, float* right, int numSamples) noexcept;

        /** The formats the browser lists and the preview reads (W30): WAV, AIFF, FLAC, MP3, OGG. */
        static juce::StringArray extensions();

    private:
        juce::AudioFormatManager formats;

        // Requests, message thread -> reader.
        std::mutex requestLock;
        juce::File requested, current;
        std::atomic<int> requestNumber { 0 };
        juce::WaitableEvent wake;

        // The FIFO, reader -> audio thread.
        static constexpr int capacity = 192000;
        juce::AbstractFifo fifo { capacity };
        juce::AudioBuffer<float> ring { 2, capacity };
        std::atomic<double> rate { 0.0 };
        std::atomic<int> flushNumber { 0 }, flushAnswered { 0 };
        std::atomic<bool> sourceDone { true }, active { false };
        std::atomic<juce::int64> framesHeard { 0 }, framesTotal { 0 };

        std::atomic<float> volumeDb { -6.0f }, gain { juce::Decibels::decibelsToGain (-6.0f) };
        float lastGain = 0.0f;   // audio thread only

        void run() override;
    };
}
