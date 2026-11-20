#include "TakePlan.h"

#include <algorithm>

namespace carthage::takes
{
    namespace
    {
        constexpr double shortest = 0.005;   // seconds: anything shorter is a sliver, not a take
    }

    std::vector<Piece> piecesOf (double startSeconds, double lengthSeconds, double keepFrom,
                                 std::optional<Span> loop, std::optional<Span> punch)
    {
        std::vector<Piece> raw;
        const auto end = startSeconds + lengthSeconds;

        if (loop && loop->length() > shortest && startSeconds < loop->to)
        {
            // Up to the loop's end, then pass after pass from its start.
            raw.push_back ({ { startSeconds, std::min (end, loop->to) }, 0.0 });
            auto offset = loop->to - startSeconds;

            while (offset < lengthSeconds - shortest)
            {
                const auto take = std::min (loop->length(), lengthSeconds - offset);
                raw.push_back ({ { loop->from, loop->from + take }, offset });
                offset += loop->length();
            }
        }
        else
        {
            raw.push_back ({ { startSeconds, end }, 0.0 });
        }

        // Cut to what is kept: from keepFrom, and inside the punch range when there is one.
        std::vector<Piece> kept;

        for (auto p : raw)
        {
            auto from = std::max (p.time.from, keepFrom);
            auto to = p.time.to;

            if (punch)
            {
                from = std::max (from, punch->from);
                to = std::min (to, punch->to);
            }

            if (to - from < shortest)
                continue;

            p.fileOffset += from - p.time.from;
            p.time = { from, to };
            kept.push_back (p);
        }

        // In a loop, one pass plays: the newest that covers at least half the loop (a pass cut
        // short by stopping just after the wrap does not hide the full one before it).
        if (loop && kept.size() > 1)
        {
            auto playing = kept.size() - 1;

            for (auto i = kept.size(); i-- > 0;)
                if (kept[i].time.length() >= loop->length() * 0.5)
                {
                    playing = i;
                    break;
                }

            for (size_t i = 0; i < kept.size(); ++i)
                kept[i].plays = i == playing;
        }

        return kept;
    }
}
