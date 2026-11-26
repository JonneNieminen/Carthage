#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <cstdint>
#include <vector>

namespace carthage::engine::plughost
{
    /** The audio link to a plugin in another process (5f, W65): one plugin's input and output rings
        and its MIDI in one block of shared memory.

        The app's audio thread writes a block's input, wakes the host and takes the output that is
        ready: the output of input frame i is output frame i + latency, where latency is the
        device's block size. The host works through whatever input has arrived, so by the app's
        next block its output is there; a block not back in time plays silence (`late` counts it)
        and the link's latency never changes, so delay compensation stays right. Neither side ever
        waits on the other. Positions are frame counts from the start, never wrapping (64 bits).

        Only `inWrite` (the app) and `inRead` (the host) move; everything else is set once by the
        app before the host attaches. */
    inline constexpr std::uint32_t linkMagic = 0x4354484cU;   // "CTHL"
    inline constexpr std::uint32_t linkVersion = 2;
    inline constexpr int maxLinkParameters = 16384;
    inline constexpr int maxLinkChannels = 32;
    inline constexpr int midiCapacity = 2048;

    /** One short MIDI message at an input frame. Longer messages (system exclusive) are not carried
        yet: `midiDropped` counts them. */
    struct LinkMidi
    {
        std::uint64_t frame;
        std::uint32_t size;
        std::uint8_t bytes[4];
    };

    /** One plugin parameter's newest value from the app. Any thread of the app may set it (the
        message thread, automation on the audio thread): the newest value wins, which is what a
        parameter means. The host applies it before its next piece of audio. */
    struct LinkParameter
    {
        std::atomic<float> value;
        std::atomic<std::uint32_t> generation;
    };

    struct LinkHeader
    {
        std::uint32_t magic, version;
        std::int32_t numIns, numOuts, capacity, maxBlock, latency, numParams;

        std::atomic<std::uint64_t> inWrite, inRead;     // input frames written by the app, read by the host
        std::atomic<std::uint64_t> midiWrite, midiRead; // MIDI messages, the same way
        std::atomic<std::uint32_t> late;                // app blocks whose output was not all ready
        std::atomic<std::uint32_t> skipped;             // host catch-ups after falling a ring behind
        std::atomic<std::uint32_t> midiDropped;         // MIDI the link could not carry
        std::atomic<std::uint32_t> quit;                // set by the app before the last wake
        std::atomic<std::uint64_t> parameterChanges;    // bumped after any parameter is set
    };

    static_assert (std::atomic<float>::is_always_lock_free, "parameters are shared between processes");

    /** The host's own buffers and what it has applied, for Link::serve. */
    struct HostSide
    {
        juce::AudioBuffer<float> audio;
        juce::MidiBuffer midi;
        std::vector<std::uint32_t> applied;   // each parameter's generation last applied
        std::uint64_t changesSeen = 0;
    };

    static_assert (std::atomic<std::uint64_t>::is_always_lock_free && std::atomic<std::uint32_t>::is_always_lock_free,
                   "the link's counters are shared between processes, so they must be lock-free");

    /** A view of one link's memory. */
    class Link
    {
    public:
        Link() = default;

        /** The bytes a link of this shape takes. */
        static size_t bytesFor (int numIns, int numOuts, int capacity, int numParams = 0);

        /** The ring length for a device block size: room for the latency, a block in flight and a
            host that falls a few blocks behind. */
        static int capacityFor (int maxBlock) noexcept   { return maxBlock * 8; }

        /** Lays a new link out in zeroed memory (the app). */
        static Link create (void* memory, size_t bytes, int numIns, int numOuts, int maxBlock, int numParams = 0);

        /** Reads a link the app laid out (the host); invalid if the memory does not hold one. */
        static Link attach (void* memory, size_t bytes);

        bool isValid() const noexcept   { return header != nullptr; }
        LinkHeader& state() const noexcept   { return *header; }

        int numIns() const noexcept    { return header->numIns; }
        int numOuts() const noexcept   { return header->numOuts; }
        int latency() const noexcept   { return header->latency; }
        int maxBlock() const noexcept  { return header->maxBlock; }
        int numParams() const noexcept { return header->numParams; }

        // ---- the app's side (audio thread) ----

        /** One block: `numSamples` frames of input in, the same of output out (silence where the
            host has not caught up). `buffer` holds the inputs on entry and the outputs on return,
            as a plugin's processBlock does; MIDI goes over with its sample positions. */
        void exchange (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi) noexcept;

        /** For export, which may wait: give() hands a block over and returns its first frame;
            once caughtUp(), take() reads that block's output (the same latency as exchange()). */
        std::uint64_t give (const juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi) noexcept;
        bool caughtUp() const noexcept;
        void take (juce::AudioBuffer<float>& buffer, std::uint64_t firstFrame) noexcept;

        /** Sets a parameter (0 to 1) for the host to apply; any thread, never waits. */
        void setParameter (int index, float value) noexcept;

        // ---- the host's side (its render thread) ----

        /** Works through all the input that has arrived, in pieces no longer than maxBlock.
            Before each piece, `parameter (index, value)` applies each parameter the app has set
            since; `render (audio, midi)` then processes the piece in place, `audio` holding
            max (ins, outs) channels. `side` is the host's own, sized by prepareHost. */
        template <typename Render, typename Parameter>
        void serve (HostSide& side, Render&& render, Parameter&& parameter) noexcept
        {
            auto& h = *header;
            auto read = h.inRead.load (std::memory_order_relaxed);
            const auto written = h.inWrite.load (std::memory_order_acquire);

            // A ring behind: the app has written over what was not read. Start again from the
            // newest input; the output of what was skipped is lost, the timing kept.
            if (written - read > static_cast<std::uint64_t> (h.capacity - h.maxBlock))
            {
                read = written - static_cast<std::uint64_t> (h.maxBlock);
                h.skipped.fetch_add (1, std::memory_order_relaxed);
            }

            while (read < written)
            {
                applyParameters (side, parameter);
                const auto n = static_cast<int> (juce::jmin<std::uint64_t> (written - read, static_cast<std::uint64_t> (h.maxBlock)));
                side.audio.setSize (side.audio.getNumChannels(), n, false, false, true);
                readInput (side.audio, read, n);
                readMidi (side.midi, read, n);
                render (side.audio, side.midi);
                writeOutput (side.audio, read + static_cast<std::uint64_t> (h.latency), n);
                read += static_cast<std::uint64_t> (n);
                h.inRead.store (read, std::memory_order_release);
            }
        }

        template <typename Render>
        void serve (HostSide& side, Render&& render) noexcept
        {
            serve (side, std::forward<Render> (render), [] (int, float) noexcept {});
        }

        /** The host's buffers for serve(). */
        void prepareHost (HostSide&) const;

    private:
        LinkHeader* header = nullptr;
        float* inputs = nullptr;    // numIns rings of capacity frames, one after another
        float* outputs = nullptr;   // numOuts rings
        LinkMidi* midiRing = nullptr;
        LinkParameter* parameters = nullptr;

        template <typename Parameter>
        void applyParameters (HostSide& side, Parameter& parameter) const noexcept
        {
            const auto changes = header->parameterChanges.load (std::memory_order_acquire);

            if (changes == side.changesSeen)
                return;

            side.changesSeen = changes;

            for (int i = 0; i < header->numParams && i < static_cast<int> (side.applied.size()); ++i)
            {
                auto& p = parameters[i];
                const auto generation = p.generation.load (std::memory_order_acquire);

                if (generation != side.applied[static_cast<size_t> (i)])
                {
                    side.applied[static_cast<size_t> (i)] = generation;
                    parameter (i, p.value.load (std::memory_order_relaxed));
                }
            }
        }

        float* inRing (int channel) const noexcept    { return inputs + static_cast<size_t> (channel) * static_cast<size_t> (header->capacity); }
        float* outRing (int channel) const noexcept   { return outputs + static_cast<size_t> (channel) * static_cast<size_t> (header->capacity); }

        void readInput (juce::AudioBuffer<float>&, std::uint64_t first, int n) const noexcept;
        void readMidi (juce::MidiBuffer&, std::uint64_t first, int n) const noexcept;
        void writeOutput (const juce::AudioBuffer<float>&, std::uint64_t first, int n) const noexcept;
    };
}
