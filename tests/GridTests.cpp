#include <engine/time/Grid.h>

using namespace carthage::time;

class GridTests final : public juce::UnitTest
{
public:
    GridTests() : juce::UnitTest ("Grid and snap", "time") {}

    void runTest() override
    {
        constexpr double bar = 4.0;    // 4/4
        constexpr double ppb = 40.0;   // pixels per beat: a quarter beat is 10 px

        beginTest ("Steps: the default is a quarter beat; bars follow the meter; triplet and dotted");
        {
            Grid g;
            expectEquals (g.stepBeats (bar, ppb), 0.25);
            g.division = Division::bar;      expectEquals (g.stepBeats (3.0, ppb), 3.0);
            g.division = Division::beat;     expectEquals (g.stepBeats (bar, ppb), 1.0);
            g.division = Division::sixtyFourth; expectEquals (g.stepBeats (bar, ppb), 1.0 / 64.0);
            g.division = Division::eighth; g.feel = Feel::triplet;
            expectWithinAbsoluteError (g.stepBeats (bar, ppb), 1.0 / 12.0, 1.0e-12);
            g.feel = Feel::dotted;
            expectWithinAbsoluteError (g.stepBeats (bar, ppb), 3.0 / 16.0, 1.0e-12);
            expectEquals (g.describe (bar, ppb), juce::String ("1/8 beat dotted"));
        }

        beginTest ("Adaptive: lines never closer than minPixels; coarser when zoomed out");
        {
            Grid g;
            g.adaptive = true;
            g.division = Division::sixtyFourth;

            for (double zoom : { 2000.0, 400.0, 40.0, 4.0, 0.5, 0.01 })
                expectGreaterOrEqual (g.stepBeats (bar, zoom) * zoom, Grid::minPixels);

            expectEquals (g.stepBeats (bar, 40.0), 0.25, "at 40 px per beat a quarter beat is 10 px");
            expectEquals (g.describe (bar, 40.0), juce::String ("Adaptive (1/4 beat)"));
            expect (g.stepBeats (bar, 0.5) >= bar * 2.0, "far out: several bars");
        }

        beginTest ("Lines for drawing cover the range on step");
        {
            Grid g;
            const auto l = g.lines (0.9, 2.1, bar, ppb);
            expectEquals (static_cast<int> (l.size()), 5);
            expectEquals (l.front(), 1.0);
            expectEquals (l.back(), 2.0);
        }

        beginTest ("Snap: the grid quantises at any distance; Cmd/Ctrl bypasses");
        {
            Grid g;
            SnapTargets none;
            expectEquals (snap (1.12, g, none, bar, ppb, false).beat, 1.0);
            expect (snap (1.12, g, none, bar, ppb, false).to == SnapResult::To::grid);
            expectEquals (snap (1.13, g, none, bar, ppb, false).beat, 1.25);
            expectEquals (snap (1.12, g, none, bar, ppb, true).beat, 1.12, "bypass");
            g.enabled = false;
            expectEquals (snap (1.12, g, none, bar, ppb, false).beat, 1.12, "grid off, nothing near");
        }

        beginTest ("Snap: clip edges, markers and the playhead pull within the radius and beat the grid");
        {
            Grid g;
            SnapTargets t;
            t.clipEdges = { 1.07 };      // 2.8 px from 1.0, off the grid
            t.markers = { 3.33 };
            t.playhead = 6.0;            // on a grid line: a true tie with the grid

            const auto edge = snap (1.05, g, t, bar, ppb, false);   // 0.8 px from the edge, 2 px from the grid line
            expect (edge.to == SnapResult::To::clipEdge);
            expectEquals (edge.beat, 1.07);

            expect (snap (3.35, g, t, bar, ppb, false).to == SnapResult::To::marker);
            expect (snap (5.98, g, t, bar, ppb, false).to == SnapResult::To::playhead, "a tie with the grid goes to the playhead");
            expect (snap (6.2, g, t, bar, ppb, false).to == SnapResult::To::grid, "a nearer grid line (6.25) beats the playhead (6.0) within reach");
            expect (snap (2.5, g, t, bar, ppb, false).to == SnapResult::To::grid, "nothing within 8 px: the grid");

            g.enabled = false;
            expectEquals (snap (1.05, g, t, bar, ppb, false).beat, 1.07, "targets still pull with the grid off");
            expectEquals (snap (5.0, g, t, bar, ppb, false).beat, 5.0);
            expect (snap (1.05, g, t, bar, ppb, true).to == SnapResult::To::nothing, "bypass ignores targets too");
        }
    }
};

static GridTests gridTests;
