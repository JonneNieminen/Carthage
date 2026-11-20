#include "AudioCallback.h"

namespace carthage::engine
{
    namespace
    {
        // tracktion::DeviceManager implements juce::AudioIODeviceCallback as a private base. A
        // C-style cast is the one conversion the language allows to an inaccessible base
        // ([expr.cast]/4); it is well-defined and yields the same object the engine registers itself.
        juce::AudioIODeviceCallback& engineCallbackOf (tracktion::DeviceManager& dm)
        {
           #if defined (__clang__)
            #pragma clang diagnostic push
            #pragma clang diagnostic ignored "-Wold-style-cast"
           #endif
            return *(juce::AudioIODeviceCallback*) &dm;
           #if defined (__clang__)
            #pragma clang diagnostic pop
           #endif
        }

        float peakOf (const float* data, int n) noexcept
        {
            const auto range = juce::FloatVectorOperations::findMinAndMax (data, n);
            return std::max (std::abs (range.getStart()), std::abs (range.getEnd()));
        }

        void storeMax (std::atomic<float>& a, float v) noexcept
        {
            auto current = a.load (std::memory_order_relaxed);
            while (v > current && ! a.compare_exchange_weak (current, v, std::memory_order_relaxed)) {}
        }
    }

    AudioCallback::AudioCallback (tracktion::Engine& e, std::span<TakeRecorder, maxTakes> r, LatencyProbe& p, SamplePreview& s)
        : engine (e), engineCallback (engineCallbackOf (e.getDeviceManager())), recorders (r), probe (p), preview (s)
    {
        numDeviceInputs[0] = 1;
        numInputs[0].store (1, std::memory_order_relaxed);
    }

    AudioCallback::~AudioCallback()
    {
        detach();
    }

    void AudioCallback::attach()
    {
        if (attached)
            return;

        auto& adm = engine.getDeviceManager().deviceManager;
        adm.removeAudioCallback (&engineCallback);
        adm.addAudioCallback (this);
        attached = true;
    }

    void AudioCallback::detach()
    {
        if (! attached)
            return;

        // Hand the device back to the engine, so its own shutdown path finds things as it left them.
        auto& adm = engine.getDeviceManager().deviceManager;
        adm.removeAudioCallback (this);
        adm.addAudioCallback (&engineCallback);
        attached = false;
    }

    void AudioCallback::setTakeInputs (int slot, int firstDeviceChannel, int numChannels)
    {
        if (slot < 0 || slot >= maxTakes)
            return;

        firstDeviceInput[static_cast<size_t> (slot)] = std::max (0, firstDeviceChannel);
        numDeviceInputs[static_cast<size_t> (slot)] = slot == 0 ? std::clamp (numChannels, 1, 2) : std::clamp (numChannels, 0, 2);
        updateInputIndices();
    }

    void AudioCallback::setPreviewOutput (int firstDeviceChannel)
    {
        previewDeviceOutput = std::max (0, firstDeviceChannel);
        updateOutputIndices();
    }

    void AudioCallback::updateOutputIndices()
    {
        // As with inputs: the callback's output array holds the active channels only.
        int left = -1, right = -1;

        if (auto* device = engine.getDeviceManager().deviceManager.getCurrentAudioDevice())
        {
            const auto active = device->getActiveOutputChannels();
            const auto indexOf = [&active] (int channel)
            {
                if (! active[channel])
                    return -1;

                int index = 0;
                for (int i = 0; i < channel; ++i)
                    if (active[i])
                        ++index;
                return index;
            };

            left = indexOf (previewDeviceOutput);
            right = indexOf (previewDeviceOutput + 1);
        }

        previewLeft.store (left, std::memory_order_relaxed);
        previewRight.store (right, std::memory_order_relaxed);
    }

    void AudioCallback::updateInputIndices()
    {
        // The callback receives only the device's active input channels, packed in order, so a
        // device channel number becomes "how many active channels come before it".
        auto* device = engine.getDeviceManager().deviceManager.getCurrentAudioDevice();
        const auto active = device != nullptr ? device->getActiveInputChannels() : juce::BigInteger();

        for (size_t slot = 0; slot < static_cast<size_t> (maxTakes); ++slot)
        {
            int first = -1, count = 0;
            const auto wanted = firstDeviceInput[slot];

            if (numDeviceInputs[slot] > 0 && active[wanted])
            {
                first = 0;

                for (int i = 0; i < wanted; ++i)
                    if (active[i])
                        ++first;

                count = (numDeviceInputs[slot] == 2 && active[wanted + 1]) ? 2 : 1;
            }

            firstInput[slot].store (first, std::memory_order_relaxed);
            numInputs[slot].store (count, std::memory_order_relaxed);
        }
    }

    void AudioCallback::audioDeviceAboutToStart (juce::AudioIODevice* device)
    {
        lastHostTimeNs = 0;   // the device is stopped, so the audio thread is not running
        engineCallback.audioDeviceAboutToStart (device);
        sampleRate.store (device->getCurrentSampleRate(), std::memory_order_relaxed);
        updateInputIndices();
        updateOutputIndices();
    }

    void AudioCallback::audioDeviceStopped()
    {
        engineCallback.audioDeviceStopped();
    }

    void AudioCallback::audioDeviceError (const juce::String& message)
    {
        engineCallback.audioDeviceError (message);
    }

    void AudioCallback::audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                                          float* const* outputChannelData, int numOutputChannels,
                                                          int numSamples,
                                                          const juce::AudioIODeviceCallbackContext& context)
    {
        const rt::ScopedRealtime realtime;
        const auto startTicks = juce::Time::getHighResolutionTicks();

        // 1. The engine: inputs, monitored chain, playback, mix.
        engineCallback.audioDeviceIOCallbackWithContext (inputChannelData, numInputChannels,
                                                         outputChannelData, numOutputChannels,
                                                         numSamples, context);

        // 2. The recorder. The engine has just advanced its stream time to the end of this block.
        const auto rate = sampleRate.load (std::memory_order_relaxed);
        const auto blockStart = engine.getDeviceManager().getCurrentStreamTime()
                                  - (rate > 0 ? numSamples / rate : 0.0);

        // Each recorder its own inputs (5d: one take per armed channel). An idle one returns at once.
        std::array<const float*, 2> recorded { nullptr, nullptr };
        int numRecorded = 0;

        for (size_t slot = 0; slot < static_cast<size_t> (maxTakes); ++slot)
        {
            const auto first = firstInput[slot].load (std::memory_order_relaxed);
            const auto count = numInputs[slot].load (std::memory_order_relaxed);
            std::array<const float*, 2> channels { nullptr, nullptr };
            int n = 0;

            for (int i = 0; i < count; ++i)
                if (first >= 0 && first + i < numInputChannels)
                    channels[static_cast<size_t> (n++)] = inputChannelData[first + i];

            if (slot == 0)
            {
                recorded = channels;   // the meters show the first recorder's inputs
                numRecorded = n;
            }

            if (count > 0)
                recorders[slot].pushBlock (channels.data(), n, numSamples, blockStart);
        }

        // 3. The sample browser's preview, on the main outputs.
        const auto left = previewLeft.load (std::memory_order_relaxed);
        const auto right = previewRight.load (std::memory_order_relaxed);
        preview.render (left >= 0 && left < numOutputChannels ? outputChannelData[left] : nullptr,
                        right >= 0 && right < numOutputChannels ? outputChannelData[right] : nullptr, numSamples);

        // 4. The latency probe silences everything else while it runs.
        probe.process (inputChannelData, numInputChannels, outputChannelData, numOutputChannels, numSamples);

        // 5. Meters and load, for the UI to poll.
        for (int i = 0; i < numRecorded; ++i)
            storeMax (inputPeak[static_cast<size_t> (i)], peakOf (recorded[static_cast<size_t> (i)], numSamples));

        if (rate > 0)
        {
            const auto seconds = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - startTicks);
            const auto load = static_cast<float> (seconds / (numSamples / rate));
            loadSum.store (loadSum.load (std::memory_order_relaxed) + load, std::memory_order_relaxed);
            loadCount.fetch_add (1, std::memory_order_relaxed);
            storeMax (loadPeak, load);
            storeMax (testPeak, load);
        }

        // A missed deadline shows up as a gap between blocks longer than the previous block lasted.
        // The device's host time is exact; without it, the time this callback started is close.
        const auto hostNs = context.hostTimeNs != nullptr
                              ? *context.hostTimeNs
                              : static_cast<std::uint64_t> (juce::Time::highResolutionTicksToSeconds (startTicks) * 1.0e9);

        if (lastHostTimeNs != 0 && rate > 0 && hostNs > lastHostTimeNs)
        {
            const auto expectedNs = lastBlockSize / rate * 1.0e9;

            if (static_cast<double> (hostNs - lastHostTimeNs) > expectedNs * 1.5)
                lateCallbacks.fetch_add (1, std::memory_order_relaxed);
        }

        lastHostTimeNs = hostNs;
        lastBlockSize = numSamples;
        samplesProcessed.fetch_add (numSamples, std::memory_order_relaxed);
        callbacks.fetch_add (1, std::memory_order_relaxed);
    }

    AudioCallback::Load AudioCallback::takeLoad() noexcept
    {
        // A sum read and reset by another thread can lose one block's contribution; for a meter
        // that is fine, and it keeps the audio side to plain loads and stores.
        const auto n = loadCount.exchange (0, std::memory_order_relaxed);
        const auto sum = loadSum.exchange (0.0f, std::memory_order_relaxed);
        Load l;
        l.average = n > 0 ? sum / static_cast<float> (n) : 0.0f;
        l.peak = loadPeak.exchange (0.0f, std::memory_order_relaxed);
        return l;
    }

    std::array<float, 2> AudioCallback::takeInputPeaks() noexcept
    {
        return { inputPeak[0].exchange (0.0f, std::memory_order_relaxed),
                 inputPeak[1].exchange (0.0f, std::memory_order_relaxed) };
    }
}
