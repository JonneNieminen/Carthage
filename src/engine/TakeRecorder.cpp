#include "TakeRecorder.h"

#include <chrono>

namespace carthage::engine
{
    namespace
    {
        // How often the writer thread looks at the FIFO. Short enough that a FIFO sized for a few
        // seconds never comes close to filling, long enough to cost nothing.
        constexpr auto writerPollInterval = std::chrono::milliseconds (4);
    }

    TakeRecorder::~TakeRecorder()
    {
        if (isRecording())
            stop();
    }

    int TakeRecorder::bitsFor (SampleFormat f) noexcept
    {
        switch (f)
        {
            case SampleFormat::int16:   return 16;
            case SampleFormat::int24:   return 24;
            case SampleFormat::float32: return 32;
        }

        return 24;
    }

    void TakeRecorder::prepare (int maxChannels, int fifoFrames)
    {
        jassert (! isRecording());
        storage.setSize (std::max (1, maxChannels), std::max (2, fifoFrames + 1), false, true, false);
        fifo.setTotalSize (storage.getNumSamples());
        fifo.reset();
    }

    bool TakeRecorder::isRecording() const noexcept
    {
        const auto s = state.load (std::memory_order_acquire);
        return s == armed || s == capturing || s == stopRequested;
    }

    juce::Result TakeRecorder::start (const juce::File& file, const Format& format)
    {
        if (isRecording())
            return juce::Result::fail ("Already recording");

        if (format.numChannels < 1 || format.numChannels > storage.getNumChannels())
            return juce::Result::fail ("The recorder was not prepared for " + juce::String (format.numChannels) + " channels");

        if (! file.getParentDirectory().createDirectory())
            return juce::Result::fail ("Could not create the folder " + file.getParentDirectory().getFullPathName());

        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);

        if (! static_cast<juce::FileOutputStream&> (*stream).openedOk())
            return juce::Result::fail ("Could not open " + file.getFullPathName() + " for writing");

        const auto options = juce::AudioFormatWriterOptions{}
                                .withSampleRate (format.sampleRate)
                                .withNumChannels (format.numChannels)
                                .withBitsPerSample (bitsFor (format.sampleFormat))
                                .withSampleFormat (format.sampleFormat == SampleFormat::float32
                                                       ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                                       : juce::AudioFormatWriterOptions::SampleFormat::integral);

        writer = juce::WavAudioFormat().createWriterFor (stream, options);

        if (writer == nullptr)
            return juce::Result::fail ("Could not create a WAV writer for this format");

        currentFile = file;
        recordChannels = format.numChannels;
        framesWritten = 0;
        framesCaptured.store (0, std::memory_order_relaxed);
        framesDropped.store (0, std::memory_order_relaxed);
        overflows.store (0, std::memory_order_relaxed);
        writeFailed.store (false, std::memory_order_relaxed);
        writerShouldFinish.store (false, std::memory_order_relaxed);
        fifo.reset();

        writerThread = std::jthread ([this] { writerLoop(); });

        // Publishes everything above to the audio thread.
        state.store (armed, std::memory_order_release);
        return juce::Result::ok();
    }

    TakeRecorder::Result TakeRecorder::stop()
    {
        Result result;

        for (;;)
        {
            auto s = state.load (std::memory_order_acquire);

            if (s == armed)
            {
                if (state.compare_exchange_weak (s, stopped, std::memory_order_acq_rel))
                    break;
            }
            else if (s == capturing)
            {
                if (state.compare_exchange_weak (s, stopRequested, std::memory_order_acq_rel))
                {
                    // Wait for the audio thread to see the request, so it is guaranteed not to be
                    // writing to the FIFO any more. If the device has stopped, no block will come.
                    const auto deadline = juce::Time::getMillisecondCounter() + 1000;

                    while (state.load (std::memory_order_acquire) != stopped
                           && juce::Time::getMillisecondCounter() < deadline)
                        std::this_thread::sleep_for (std::chrono::milliseconds (1));

                    state.store (stopped, std::memory_order_release);
                    break;
                }
            }
            else
            {
                result.error = "Not recording";
                return result;
            }
        }

        writerShouldFinish.store (true, std::memory_order_release);

        if (writerThread.joinable())
            writerThread.join();

        writer.reset();   // flushes and closes the file

        result.file = currentFile;
        result.firstBlockStreamTime = firstStreamTime.load (std::memory_order_acquire);
        result.framesWritten = framesWritten;
        result.framesDropped = framesDropped.load (std::memory_order_relaxed);
        result.overflows = overflows.load (std::memory_order_relaxed);

        if (writeFailed.load (std::memory_order_acquire))
            result.error = "Writing to " + currentFile.getFullPathName() + " failed";

        state.store (idle, std::memory_order_release);
        return result;
    }

    void TakeRecorder::pushBlock (const float* const* channels, int numChannels, int numFrames,
                                  double blockStartStreamTime) noexcept
    {
        auto s = state.load (std::memory_order_acquire);

        if (s == armed)
        {
            if (! state.compare_exchange_strong (s, capturing, std::memory_order_acq_rel))
                return;   // stop() won the race

            firstStreamTime.store (blockStartStreamTime, std::memory_order_release);
            s = capturing;
        }

        if (s == stopRequested)
        {
            state.store (stopped, std::memory_order_release);
            return;
        }

        if (s != capturing || numFrames <= 0)
            return;

        int start1, size1, start2, size2;
        fifo.prepareToWrite (numFrames, start1, size1, start2, size2);

        if (size1 + size2 < numFrames)
        {
            framesDropped.fetch_add (numFrames, std::memory_order_relaxed);
            overflows.fetch_add (1, std::memory_order_relaxed);
            return;
        }

        for (int ch = 0; ch < recordChannels; ++ch)
        {
            // A mono take from a stereo request still gets a full block: missing channels are silent.
            const float* src = ch < numChannels ? channels[ch] : nullptr;

            if (src != nullptr)
            {
                if (size1 > 0) storage.copyFrom (ch, start1, src, size1);
                if (size2 > 0) storage.copyFrom (ch, start2, src + size1, size2);
            }
            else
            {
                if (size1 > 0) storage.clear (ch, start1, size1);
                if (size2 > 0) storage.clear (ch, start2, size2);
            }
        }

        fifo.finishedWrite (size1 + size2);
        framesCaptured.fetch_add (numFrames, std::memory_order_relaxed);
    }

    int TakeRecorder::drainFifo()
    {
        int start1, size1, start2, size2;
        fifo.prepareToRead (fifo.getNumReady(), start1, size1, start2, size2);

        const auto write = [this] (int start, int size)
        {
            if (size > 0 && ! writer->writeFromAudioSampleBuffer (storage, start, size))
                writeFailed.store (true, std::memory_order_release);
        };

        write (start1, size1);
        write (start2, size2);

        const auto total = size1 + size2;
        fifo.finishedRead (total);
        framesWritten += total;
        return total;
    }

    void TakeRecorder::writerLoop()
    {
        for (;;)
        {
            // Read the flag before draining: once it is set the audio thread has stopped writing,
            // so one more drain after seeing it empties the FIFO for good.
            const bool finishing = writerShouldFinish.load (std::memory_order_acquire);

            drainFifo();

            if (finishing)
            {
                drainFifo();
                return;
            }

            std::this_thread::sleep_for (writerPollInterval);
        }
    }
}
