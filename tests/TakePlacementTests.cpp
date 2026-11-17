#include <engine/TakePlacement.h>

#include <juce_core/juce_core.h>

using carthage::engine::TakeWatch;
using carthage::engine::takeStartSeconds;

class TakePlacementTests final : public juce::UnitTest
{
public:
    TakePlacementTests() : juce::UnitTest ("Take placement", "engine") {}

    void runTest() override
    {
        beginTest ("The first sample lands at stream time + offset + one block - round trip");
        {
            // 48 kHz, buffer 64, round trip 256: the numbers carthage-bench align measured on an iD44.
            const auto t = takeStartSeconds (10.0, -9.5, 64, 256, 48000.0);
            expectWithinAbsoluteError (t, 0.5 + (64 - 256) / 48000.0, 1.0e-12);
        }

        beginTest ("A take that runs normally keeps the settled offset");
        {
            TakeWatch w;
            expect (w.update (std::nullopt, 0) == TakeWatch::State::waiting);       // play is pending
            expect (w.update (-5.010667, 5) == TakeWatch::State::running);          // first reading
            expect (w.update (-5.012000, 10) == TakeWatch::State::running);         // settles by one block
            expect (w.update (-5.012000, 15) == TakeWatch::State::running);
            expectWithinAbsoluteError (*w.getOffset(), -5.012, 1.0e-9);
        }

        beginTest ("Playback stopping under the take interrupts it and keeps the last good offset");
        {
            TakeWatch w;
            w.update (-1.0, 5);
            w.update (-1.0, 10);
            expect (w.update (std::nullopt, 15) == TakeWatch::State::interrupted);
            expectWithinAbsoluteError (*w.getOffset(), -1.0, 1.0e-12);

            // Once interrupted, it stays so even if playback comes back.
            expect (w.update (-1.0, 20) == TakeWatch::State::interrupted);
        }

        beginTest ("A jump in the time line interrupts the take");
        {
            TakeWatch w;
            w.update (-1.0, 5);
            expect (w.update (-1.0 + TakeWatch::jumpToleranceSeconds * 0.5, 10) == TakeWatch::State::running);
            expect (w.update (-1.0 + 0.5, 15) == TakeWatch::State::interrupted);
            expectWithinAbsoluteError (*w.getOffset(), -1.0 + TakeWatch::jumpToleranceSeconds * 0.5, 1.0e-12);
        }

        beginTest ("Playback that never starts gives up, with no offset");
        {
            TakeWatch w;
            expect (w.update (std::nullopt, TakeWatch::maxWaitForPlaybackMs - 1) == TakeWatch::State::waiting);
            expect (w.update (std::nullopt, TakeWatch::maxWaitForPlaybackMs) == TakeWatch::State::interrupted);
            expect (! w.getOffset().has_value());
        }
    }
};

static TakePlacementTests takePlacementTests;
