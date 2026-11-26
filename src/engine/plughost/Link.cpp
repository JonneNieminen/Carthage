#include "Link.h"

#include <new>

namespace carthage::engine::plughost
{
    namespace
    {
        constexpr size_t alignUp (size_t n) noexcept   { return (n + 63) & ~static_cast<size_t> (63); }

        struct Layout
        {
            size_t header, inputs, outputs, midi, total;
        };

        Layout layoutFor (int numIns, int numOuts, int capacity) noexcept
        {
            const auto ring = static_cast<size_t> (capacity) * sizeof (float);
            Layout l {};
            l.header = 0;
            l.inputs = alignUp (sizeof (LinkHeader));
            l.outputs = l.inputs + alignUp (ring * static_cast<size_t> (numIns));
            l.midi = l.outputs + alignUp (ring * static_cast<size_t> (numOuts));
            l.total = l.midi + alignUp (sizeof (LinkMidi) * static_cast<size_t> (midiCapacity));
            return l;
        }

        /** Copies `n` frames between a ring and a plain buffer, wrapping at the ring's end. */
        void toRing (float* ring, int capacity, std::uint64_t first, const float* source, int n) noexcept
        {
            const auto start = static_cast<int> (first % static_cast<std::uint64_t> (capacity));
            const auto head = juce::jmin (n, capacity - start);
            std::copy (source, source + head, ring + start);
            std::copy (source + head, source + n, ring);
        }

        void fromRing (const float* ring, int capacity, std::uint64_t first, float* dest, int n) noexcept
        {
            const auto start = static_cast<int> (first % static_cast<std::uint64_t> (capacity));
            const auto head = juce::jmin (n, capacity - start);
            std::copy (ring + start, ring + start + head, dest);
            std::copy (ring, ring + (n - head), dest + head);
        }

        void zeroRing (float* ring, int capacity, std::uint64_t first, int n) noexcept
        {
            const auto start = static_cast<int> (first % static_cast<std::uint64_t> (capacity));
            const auto head = juce::jmin (n, capacity - start);
            std::fill (ring + start, ring + start + head, 0.0f);
            std::fill (ring, ring + (n - head), 0.0f);
        }
    }

    size_t Link::bytesFor (int numIns, int numOuts, int capacity)
    {
        return layoutFor (numIns, numOuts, capacity).total;
    }

    Link Link::create (void* memory, size_t bytes, int numIns, int numOuts, int maxBlock)
    {
        const auto capacity = capacityFor (maxBlock);

        if (memory == nullptr || numIns < 0 || numOuts < 0 || numIns > maxLinkChannels || numOuts > maxLinkChannels
            || maxBlock <= 0 || bytes < bytesFor (numIns, numOuts, capacity))
            return {};

        auto* h = new (memory) LinkHeader {};
        h->magic = linkMagic;
        h->version = linkVersion;
        h->numIns = numIns;
        h->numOuts = numOuts;
        h->capacity = capacity;
        h->maxBlock = maxBlock;
        h->latency = maxBlock;   // the output rings start as `latency` frames of silence (zeroed memory)
        return attach (memory, bytes);
    }

    Link Link::attach (void* memory, size_t bytes)
    {
        if (memory == nullptr || bytes < sizeof (LinkHeader))
            return {};

        auto* h = static_cast<LinkHeader*> (memory);

        if (h->magic != linkMagic || h->version != linkVersion || h->numIns < 0 || h->numOuts < 0
            || h->numIns > maxLinkChannels || h->numOuts > maxLinkChannels || h->maxBlock <= 0
            || h->capacity < 4 * h->maxBlock || h->latency != h->maxBlock
            || bytes < bytesFor (h->numIns, h->numOuts, h->capacity))
            return {};

        const auto l = layoutFor (h->numIns, h->numOuts, h->capacity);
        auto* base = static_cast<char*> (memory);
        Link link;
        link.header = h;
        link.inputs = reinterpret_cast<float*> (base + l.inputs);
        link.outputs = reinterpret_cast<float*> (base + l.outputs);
        link.midiRing = reinterpret_cast<LinkMidi*> (base + l.midi);
        return link;
    }

    std::uint64_t Link::give (const juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi) noexcept
    {
        auto& h = *header;
        const auto first = h.inWrite.load (std::memory_order_relaxed);
        const auto n = juce::jmin (buffer.getNumSamples(), h.maxBlock);

        for (int c = 0; c < h.numIns; ++c)
        {
            if (c < buffer.getNumChannels())
                toRing (inRing (c), h.capacity, first, buffer.getReadPointer (c), n);
            else
                zeroRing (inRing (c), h.capacity, first, n);
        }

        // MIDI first, so the host never reads a frame before its messages are there.
        auto write = h.midiWrite.load (std::memory_order_relaxed);
        const auto read = h.midiRead.load (std::memory_order_acquire);

        for (const auto m : midi)
        {
            if (m.samplePosition >= n)
                break;

            if (m.numBytes > 4 || write - read >= static_cast<std::uint64_t> (midiCapacity))
            {
                h.midiDropped.fetch_add (1, std::memory_order_relaxed);
                continue;
            }

            auto& slot = midiRing[write % static_cast<std::uint64_t> (midiCapacity)];
            slot.frame = first + static_cast<std::uint64_t> (m.samplePosition);
            slot.size = static_cast<std::uint32_t> (m.numBytes);
            std::copy (m.data, m.data + m.numBytes, slot.bytes);
            ++write;
        }

        h.midiWrite.store (write, std::memory_order_release);
        h.inWrite.store (first + static_cast<std::uint64_t> (n), std::memory_order_release);
        return first;
    }

    void Link::take (juce::AudioBuffer<float>& buffer, std::uint64_t first) noexcept
    {
        auto& h = *header;
        const auto n = juce::jmin (buffer.getNumSamples(), h.maxBlock);
        const auto readyUntil = h.inRead.load (std::memory_order_acquire) + static_cast<std::uint64_t> (h.latency);
        const auto ready = readyUntil <= first ? 0 : static_cast<int> (juce::jmin<std::uint64_t> (readyUntil - first, static_cast<std::uint64_t> (n)));

        for (int c = 0; c < buffer.getNumChannels(); ++c)
        {
            auto* dest = buffer.getWritePointer (c);

            if (c < h.numOuts)
                fromRing (outRing (c), h.capacity, first, dest, ready);
            else
                std::fill (dest, dest + ready, 0.0f);

            std::fill (dest + ready, dest + buffer.getNumSamples(), 0.0f);
        }

        if (ready < n)
            h.late.fetch_add (1, std::memory_order_relaxed);
    }

    void Link::exchange (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi) noexcept
    {
        const auto first = give (buffer, midi);
        take (buffer, first);
    }

    bool Link::caughtUp() const noexcept
    {
        return header->inRead.load (std::memory_order_acquire) >= header->inWrite.load (std::memory_order_acquire);
    }

    void Link::prepareHost (juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) const
    {
        // At least one channel (attach() checked the counts); the analyzer can not see that through jmax.
        audio.setSize (juce::jmax (1, header->numIns, header->numOuts), header->maxBlock);   // NOLINT(clang-analyzer-security.ArrayBound)
        midi.ensureSize (static_cast<size_t> (midiCapacity) * 16);
    }

    void Link::readInput (juce::AudioBuffer<float>& audio, std::uint64_t first, int n) const noexcept
    {
        for (int c = 0; c < audio.getNumChannels(); ++c)
        {
            if (c < header->numIns)
                fromRing (inRing (c), header->capacity, first, audio.getWritePointer (c), n);
            else
                audio.clear (c, 0, n);
        }
    }

    void Link::readMidi (juce::MidiBuffer& midi, std::uint64_t first, int n) const noexcept
    {
        auto& h = *header;
        midi.clear();
        auto read = h.midiRead.load (std::memory_order_relaxed);
        const auto written = h.midiWrite.load (std::memory_order_acquire);
        const auto end = first + static_cast<std::uint64_t> (n);

        while (read < written)
        {
            const auto& m = midiRing[read % static_cast<std::uint64_t> (midiCapacity)];

            if (m.frame >= end)
                break;

            if (m.frame >= first)   // older than this piece: skipped with its audio
                midi.addEvent (m.bytes, static_cast<int> (m.size), static_cast<int> (m.frame - first));

            ++read;
        }

        h.midiRead.store (read, std::memory_order_release);
    }

    void Link::writeOutput (const juce::AudioBuffer<float>& audio, std::uint64_t first, int n) const noexcept
    {
        for (int c = 0; c < header->numOuts; ++c)
        {
            if (c < audio.getNumChannels())
                toRing (outRing (c), header->capacity, first, audio.getReadPointer (c), n);
            else
                zeroRing (outRing (c), header->capacity, first, n);
        }
    }
}
