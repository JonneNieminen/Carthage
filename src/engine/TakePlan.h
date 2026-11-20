#pragma once

#include <optional>
#include <vector>

namespace carthage::takes
{
    /** A stretch of edit time, in seconds. */
    struct Span
    {
        double from = 0.0, to = 0.0;
        double length() const noexcept   { return to - from; }
    };

    /** One clip a take becomes: where it sits in the edit, and where in the recorded file it starts. */
    struct Piece
    {
        Span time;
        double fileOffset = 0.0;
        bool plays = true;   ///< false for an earlier loop pass: kept, muted
    };

    /** How a finished take is cut into clips (5d, owner):
        - `startSeconds` is where its first sample belongs in the edit, `lengthSeconds` how long it is.
        - `keepFrom`: anything before it is dropped: the count-in, and anything before the edit's start.
        - `loop`: while the transport loops this region, the recording wraps at its end: the first
          stretch up to the loop's end, then one piece per pass (FL-style loop recording). The
          newest pass that covers at least half the loop plays; earlier ones are kept, muted.
        - `punch`: only what falls inside it is kept (punch in/out, off unless turned on).
        Pieces shorter than a few milliseconds are dropped. */
    std::vector<Piece> piecesOf (double startSeconds, double lengthSeconds, double keepFrom,
                                 std::optional<Span> loop, std::optional<Span> punch);
}
