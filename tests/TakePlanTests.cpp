#include <engine/TakePlan.h>

#include <juce_core/juce_core.h>

using namespace carthage;

/** How a finished take is cut into clips (5d): loop passes, punch, count-in. */
class TakePlanTests final : public juce::UnitTest
{
public:
    TakePlanTests() : juce::UnitTest ("Take plan", "engine") {}

    void runTest() override
    {
        constexpr double e = 1.0e-9;
        const auto near = [this] (const takes::Piece& p, double from, double to, double offset, const juce::String& what)
        {
            expectWithinAbsoluteError (p.time.from, from, e, what + ": start");
            expectWithinAbsoluteError (p.time.to, to, e, what + ": end");
            expectWithinAbsoluteError (p.fileOffset, offset, e, what + ": where in the file");
        };

        beginTest ("Without loop or punch a take is one clip, from where it began until it was stopped");
        {
            const auto p = takes::piecesOf (2.0, 5.0, 2.0, std::nullopt, std::nullopt);
            expectEquals (static_cast<int> (p.size()), 1);
            near (p[0], 2.0, 7.0, 0.0, "the take");
            expect (p[0].plays);
        }

        beginTest ("A count-in is cut off: the take starts where record was pressed");
        {
            const auto p = takes::piecesOf (0.0, 6.0, 2.0, std::nullopt, std::nullopt);   // two seconds of count-in
            expectEquals (static_cast<int> (p.size()), 1);
            near (p[0], 2.0, 6.0, 2.0, "after the count-in");
        }

        beginTest ("Nothing before the edit's start is kept");
        {
            const auto p = takes::piecesOf (-0.01, 1.0, 0.0, std::nullopt, std::nullopt);
            near (p[0], 0.0, 0.99, 0.01, "trimmed at zero");
        }

        beginTest ("Loop recording: one clip per pass, the newest full pass playing, earlier ones muted");
        {
            // Loop 4..8 s, recording from 4 s for three full passes and a bit.
            const auto p = takes::piecesOf (4.0, 12.5, 4.0, takes::Span { 4.0, 8.0 }, std::nullopt);
            expectEquals (static_cast<int> (p.size()), 4);
            near (p[0], 4.0, 8.0, 0.0, "pass 1");
            near (p[1], 4.0, 8.0, 4.0, "pass 2");
            near (p[2], 4.0, 8.0, 8.0, "pass 3");
            near (p[3], 4.0, 4.5, 12.0, "the start of pass 4");
            expect (! p[0].plays && ! p[1].plays && ! p[3].plays, "earlier passes and a short last one are muted");
            expect (p[2].plays, "the newest full pass plays");
        }

        beginTest ("Loop recording from before the loop: the first clip runs in to the loop's end");
        {
            const auto p = takes::piecesOf (2.0, 12.0, 2.0, takes::Span { 4.0, 8.0 }, std::nullopt);
            expectEquals (static_cast<int> (p.size()), 3);
            near (p[0], 2.0, 8.0, 0.0, "the roll-in");
            near (p[1], 4.0, 8.0, 6.0, "pass 2");
            near (p[2], 4.0, 6.0, 10.0, "what was left");
        }

        beginTest ("Punch: only what falls inside the region is kept");
        {
            const auto p = takes::piecesOf (2.0, 10.0, 2.0, std::nullopt, takes::Span { 4.0, 8.0 });
            expectEquals (static_cast<int> (p.size()), 1);
            near (p[0], 4.0, 8.0, 2.0, "punched in at 4 s, out at 8 s");
        }

        beginTest ("Punch with loop: each pass inside the region");
        {
            const auto p = takes::piecesOf (2.0, 12.0, 2.0, takes::Span { 4.0, 8.0 }, takes::Span { 4.0, 8.0 });
            expectEquals (static_cast<int> (p.size()), 3);
            near (p[0], 4.0, 8.0, 2.0, "the roll-in, punched in");
            near (p[1], 4.0, 8.0, 6.0, "pass 2");
            near (p[2], 4.0, 6.0, 10.0, "what was left");
        }

        beginTest ("A take that never reaches the punch region keeps nothing");
        expect (takes::piecesOf (0.0, 3.0, 0.0, std::nullopt, takes::Span { 4.0, 8.0 }).empty());
    }
};

static TakePlanTests takePlanTests;
