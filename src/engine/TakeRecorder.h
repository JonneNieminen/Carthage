#pragma once

#include "RealtimeCheck.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

namespace carthage::engine
{
    /** Records audio from the device callback to a file on disk (brief, feature 10).

        Audio thread -> lock-free single-producer/single-consumer FIFO -> writer thread -> file.
        The audio side only copies samples and touches atomics: no allocation, lock, signal or I/O.
        The writer thread polls the FIFO, so the audio thread never has to wake it. If the FIFO is
        full the block is dropped and counted (AGENTS.md: "an overflow is counted and reported,
        never hidden and never fixed by blocking").

        Threads: prepare/start/stop/statistics on the message thread; pushBlock on the audio thread.
    */
    class TakeRecorder
    {
    public:
        enum class SampleFormat { int16, int24, float32 };

        struct Format
        {
            double sampleRate = 48000.0;
            int numChannels = 1;
            SampleFormat sampleFormat = SampleFormat::int24;
        };

        struct Result
        {
            juce::File file;
            double firstBlockStreamTime = 0.0;  ///< Engine stream time (s) of the first captured sample
            std::int64_t framesWritten = 0;
            std::int64_t framesDropped = 0;
            int overflows = 0;
            juce::String error;                 ///< Empty on success

            bool succeeded() const noexcept     { return error.isEmpty(); }
        };

        TakeRecorder() = default;
        ~TakeRecorder();

        /** Allocates the FIFO. Call on the message thread whenever the device's channel count or rate
            changes, never while recording. */
        void prepare (int maxChannels, int fifoFrames);

        /** Opens the file and starts the writer thread. The audio thread starts capturing on its next
            block. */
        juce::Result start (const juce::File&, const Format&);

        /** Stops capture, waits for the audio thread to acknowledge, drains the FIFO, closes the file. */
        Result stop();

        bool isRecording() const noexcept;
        std::int64_t getFramesCaptured() const noexcept    { return framesCaptured.load (std::memory_order_relaxed); }
        std::int64_t getFramesDropped() const noexcept     { return framesDropped.load (std::memory_order_relaxed); }
        int getOverflowCount() const noexcept              { return overflows.load (std::memory_order_relaxed); }
        int getFifoCapacityFrames() const noexcept         { return fifo.getTotalSize() - 1; }

        /** Audio thread. Copies one block of input into the FIFO. */
        void pushBlock (const float* const* channels, int numChannels, int numFrames,
                        double blockStartStreamTime) noexcept CARTHAGE_NONBLOCKING;

        /** Writes the bytes for one sample format; exposed for tests and the settings UI. */
        static int bitsFor (SampleFormat) noexcept;

    private:
        enum State : int { idle, armed, capturing, stopRequested, stopped };

        std::atomic<int> state { idle };
        std::atomic<double> firstStreamTime { 0.0 };
        std::atomic<std::int64_t> framesCaptured { 0 }, framesDropped { 0 };
        std::atomic<int> overflows { 0 };

        juce::AbstractFifo fifo { 2 };
        juce::AudioBuffer<float> storage;
        int recordChannels = 1;

        std::unique_ptr<juce::AudioFormatWriter> writer;
        juce::File currentFile;
        std::atomic<bool> writerShouldFinish { false };
        std::atomic<bool> writeFailed { false };
        std::jthread writerThread;
        std::int64_t framesWritten = 0;

        void writerLoop();
        int drainFifo();

        JUCE_DECLARE_NON_COPYABLE (TakeRecorder)
    };
}
