#pragma once

#include <algorithm>
#include <cmath>
#include <optional>

namespace carthage::engine
{
    /** Watches a take's time line while it records.

        A take's samples are stamped with the engine's device stream time; they map to edit time
        only while the playhead runs, and then the mapping is a constant offset (edit time minus
        stream time). Session samples that offset every few milliseconds and feeds it here. The
        watch keeps the settled offset and says when playback stopped or jumped under the take
        (an engine graph rebuild after a device change stops the transport), so the take can end
        there and still be placed in time. Pure: tested without an interface
        (tests/TakePlacementTests.cpp).
    */
    class TakeWatch
    {
    public:
        enum class State { waiting, running, interrupted };

        /** Just after playback starts the offset settles by one audio block, so a move of up to two
            blocks (and never less than this) is not a jump. A relocation moves it by far more. A
            fixed 10 ms was too tight: one block of 512 samples at 48 kHz is 10.7 ms, and takes
            recorded at that buffer ended themselves after 11 ms. */
        static constexpr double minimumJumpToleranceSeconds = 0.010;

        explicit TakeWatch (double blockSeconds = 0.0) noexcept
            : jumpToleranceSeconds (std::max (minimumJumpToleranceSeconds, 2.0 * blockSeconds)) {}

        double getJumpTolerance() const noexcept                 { return jumpToleranceSeconds; }

        /** How long a take waits for playback to start before it gives up. */
        static constexpr int maxWaitForPlaybackMs = 2000;

        /** `offset`: edit time minus stream time now, or nothing while playback is not running.
            `elapsedMs`: time since the take started. */
        State update (std::optional<double> offset, int elapsedMs) noexcept
        {
            if (state == State::interrupted)
                return state;

            if (state == State::waiting)
            {
                if (offset)
                {
                    settled = offset;
                    state = State::running;
                }
                else if (elapsedMs >= maxWaitForPlaybackMs)
                {
                    state = State::interrupted;   // playback never started
                }

                return state;
            }

            if (! offset || ! settled || std::abs (*offset - *settled) > jumpToleranceSeconds)
                state = State::interrupted;       // stopped or jumped: keep the last good offset
            else
                settled = offset;                 // follow the settled value

            return state;
        }

        State getState() const noexcept                         { return state; }

        /** The offset to place the take with: the last one seen while playback ran normally. */
        std::optional<double> getOffset() const noexcept         { return settled; }

    private:
        double jumpToleranceSeconds;
        State state = State::waiting;
        std::optional<double> settled;
    };

    /** Edit time in seconds of a take's first sample.

        `blockSamples`: the engine's playback clock runs one block ahead of its device stream time
        (when it syncs a playback context it hands it the coming block, then advances it again
        before rendering), so what plays out during the block at stream time t belongs to edit
        time t + offset + one block. Shown by carthage-bench align through a loopback: without it
        every take lands one buffer early.

        `roundTripSamples`: what the input heard was played that long before it was captured. */
    inline double takeStartSeconds (double firstSampleStreamTime, double streamToEditOffset,
                                    int blockSamples, int roundTripSamples, double sampleRate) noexcept
    {
        return firstSampleStreamTime + streamToEditOffset + (blockSamples - roundTripSamples) / sampleRate;
    }
}
