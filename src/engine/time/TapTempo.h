#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>

namespace carthage::time
{
    /** Tap tempo (owner's list F22): each tap is a time in seconds; the tempo is the mean of the
        last few intervals. A pause longer than `resetAfterSeconds` starts over, and a tap that
        breaks the rhythm by more than a third starts over from the tap before it, so a fumbled
        tap never drags the figure far. Plain logic, tested on its own. */
    class TapTempo
    {
    public:
        static constexpr double resetAfterSeconds = 2.0;
        static constexpr double minBpm = 20.0, maxBpm = 400.0;

        /** Returns the tempo once two taps are in, else nothing. */
        std::optional<double> tap (double seconds) noexcept
        {
            if (count > 0 && (seconds - last > resetAfterSeconds || seconds <= last))
            {
                count = 0;
                intervals = 0;
            }

            if (count > 0)
            {
                const auto interval = seconds - last;

                if (intervals > 0 && std::abs (interval - mean()) > mean() / 3.0)
                    intervals = 0;   // out of rhythm: the tempo starts from this interval

                window[next] = interval;
                next = (next + 1) % window.size();
                intervals = std::min (intervals + 1, window.size());
            }

            last = seconds;
            ++count;

            if (intervals == 0)
                return std::nullopt;

            const auto bpm = 60.0 / mean();
            return bpm < minBpm ? minBpm : bpm > maxBpm ? maxBpm : bpm;
        }

    private:
        std::array<double, 6> window {};
        std::size_t next = 0, intervals = 0;
        int count = 0;
        double last = 0.0;

        /** The mean of the intervals kept: the newest `intervals` of them. */
        double mean() const noexcept
        {
            double sum = 0.0;

            for (std::size_t i = 1; i <= intervals; ++i)
                sum += window[(next + window.size() - i) % window.size()];

            return sum / static_cast<double> (intervals);
        }
    };
}
