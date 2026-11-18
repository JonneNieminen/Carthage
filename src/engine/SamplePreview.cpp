#include "SamplePreview.h"

namespace carthage::engine
{
    SamplePreview::SamplePreview() : juce::Thread ("Carthage preview")
    {
        formats.registerBasicFormats();   // WAV, AIFF, FLAC, Ogg, MP3 (JUCE_USE_MP3AUDIOFORMAT)
        startThread (juce::Thread::Priority::normal);
    }

    SamplePreview::~SamplePreview()
    {
        signalThreadShouldExit();
        wake.signal();
        stopThread (2000);
    }

    juce::StringArray SamplePreview::extensions()
    {
        return { ".wav", ".aif", ".aiff", ".flac", ".mp3", ".ogg" };
    }

    void SamplePreview::prepare (double deviceSampleRate)
    {
        if (deviceSampleRate > 0.0 && deviceSampleRate != rate.load())
        {
            rate.store (deviceSampleRate);
            stop();
        }
    }

    void SamplePreview::play (const juce::File& file)
    {
        {
            const std::scoped_lock lock (requestLock);
            requested = file;
        }

        requestNumber.fetch_add (1);
        wake.signal();
    }

    void SamplePreview::stop()
    {
        play ({});
    }

    void SamplePreview::setVolumeDb (float db)
    {
        db = juce::jlimit (-60.0f, 6.0f, db);
        volumeDb.store (db, std::memory_order_relaxed);
        gain.store (db <= -60.0f ? 0.0f : juce::Decibels::decibelsToGain (db), std::memory_order_relaxed);
    }

    bool SamplePreview::isPlaying() const noexcept
    {
        return active.load() && ! (sourceDone.load() && framesHeard.load() >= framesTotal.load());
    }

    juce::File SamplePreview::getFile() const
    {
        const std::scoped_lock lock (const_cast<std::mutex&> (requestLock));
        return current;
    }

    double SamplePreview::getProgress() const noexcept
    {
        const auto total = framesTotal.load();
        return total > 0 ? juce::jlimit (0.0, 1.0, static_cast<double> (framesHeard.load()) / static_cast<double> (total)) : 0.0;
    }

    void SamplePreview::render (float* left, float* right, int numSamples) noexcept
    {
        // A new sound was asked for: drop what is queued, and say so.
        if (const auto flush = flushNumber.load (std::memory_order_acquire); flush != flushAnswered.load (std::memory_order_relaxed))
        {
            fifo.finishedRead (fifo.getNumReady());
            flushAnswered.store (flush, std::memory_order_release);
            lastGain = 0.0f;
            return;
        }

        if (! active.load (std::memory_order_relaxed) || numSamples <= 0)
            return;

        // The gain moves to the slider's value across the block, so moving it never clicks.
        const auto target = gain.load (std::memory_order_relaxed);
        // fifo.read's scope marks the frames read when it ends (JUCE's ScopedRead).
        const auto step = (target - lastGain) / static_cast<float> (numSamples);
        auto g = lastGain;
        const auto scope = fifo.read (numSamples);
        int done = 0;

        for (auto [start, size] : { std::pair { scope.startIndex1, scope.blockSize1 }, std::pair { scope.startIndex2, scope.blockSize2 } })
        {
            const auto* l = ring.getReadPointer (0);
            const auto* r = ring.getReadPointer (1);

            for (int i = 0; i < size; ++i)
            {
                g += step;
                if (left != nullptr)  left[done + i] += l[start + i] * g;
                if (right != nullptr) right[done + i] += r[start + i] * g;
            }

            done += size;
        }

        lastGain = g;
        framesHeard.fetch_add (done, std::memory_order_relaxed);
    }

    void SamplePreview::run()
    {
        int handled = 0;
        std::unique_ptr<juce::AudioFormatReader> reader;
        juce::LagrangeInterpolator interpolators[2];
        juce::AudioBuffer<float> source (2, 4096), converted (2, 2048);
        juce::int64 readPosition = 0;
        double ratio = 1.0;

        while (! threadShouldExit())
        {
            // A new request: flush the FIFO through the audio thread, then open the file.
            if (const auto wanted = requestNumber.load(); wanted != handled)
            {
                handled = wanted;
                active.store (false);
                const auto flush = flushNumber.fetch_add (1) + 1;

                // The audio thread answers within a block; if the device is stopped, nobody
                // reads the FIFO, so it is safe to empty it here after a short wait.
                for (int waited = 0; flushAnswered.load (std::memory_order_acquire) != flush && waited < 200; ++waited)
                    wait (1);

                if (flushAnswered.load (std::memory_order_acquire) != flush)
                {
                    fifo.reset();
                    flushAnswered.store (flush);
                }

                juce::File file;
                {
                    const std::scoped_lock lock (requestLock);
                    file = requested;
                    current = file;
                }

                reader.reset (file == juce::File() ? nullptr : formats.createReaderFor (file));
                readPosition = 0;
                framesHeard.store (0);
                sourceDone.store (reader == nullptr);

                if (const auto deviceRate = rate.load(); reader != nullptr && deviceRate > 0.0)
                {
                    ratio = reader->sampleRate / deviceRate;
                    framesTotal.store (static_cast<juce::int64> (static_cast<double> (reader->lengthInSamples) / ratio));
                    for (auto& i : interpolators) i.reset();
                    active.store (true);
                }
                else
                {
                    framesTotal.store (0);
                    reader.reset();
                }
            }

            // Fill whatever room the FIFO has.
            if (reader != nullptr && ! sourceDone.load())
            {
                const auto room = fifo.getFreeSpace();

                if (room >= 512)
                {
                    const auto out = std::min (room, converted.getNumSamples());
                    const auto in = static_cast<int> (std::ceil (out * ratio)) + 4;
                    source.clear();
                    reader->read (&source, 0, in, readPosition, true, true);

                    const auto mono = reader->numChannels < 2;
                    int used = 0;

                    for (int ch = 0; ch < 2; ++ch)
                        used = interpolators[ch].process (ratio, source.getReadPointer (mono ? 0 : ch),
                                                          converted.getWritePointer (ch), out, in, 0);

                    readPosition += used;
                    {
                        // The scope marks the frames written when it ends (JUCE's ScopedWrite).
                        const auto scope = fifo.write (out);

                        for (int ch = 0; ch < 2; ++ch)
                        {
                            if (scope.blockSize1 > 0) ring.copyFrom (ch, scope.startIndex1, converted, ch, 0, scope.blockSize1);
                            if (scope.blockSize2 > 0) ring.copyFrom (ch, scope.startIndex2, converted, ch, scope.blockSize1, scope.blockSize2);
                        }
                    }

                    if (readPosition >= reader->lengthInSamples)
                        sourceDone.store (true);

                    continue;   // keep filling while there is room
                }
            }

            wake.wait (5);
        }
    }
}
