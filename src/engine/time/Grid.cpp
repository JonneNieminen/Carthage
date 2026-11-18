#include "Grid.h"

#include <cmath>

namespace carthage::time
{
    namespace
    {
        double baseBeats (Division d, double beatsPerBar)
        {
            switch (d)
            {
                case Division::bar:          return beatsPerBar;
                case Division::beat:         return 1.0;
                case Division::half:         return 1.0 / 2.0;
                case Division::quarter:      return 1.0 / 4.0;
                case Division::eighth:       return 1.0 / 8.0;
                case Division::sixteenth:    return 1.0 / 16.0;
                case Division::thirtySecond: return 1.0 / 32.0;
                case Division::sixtyFourth:  return 1.0 / 64.0;
            }

            return 1.0 / 4.0;
        }

        double withFeel (double step, Feel f)
        {
            switch (f)
            {
                case Feel::triplet: return step * 2.0 / 3.0;
                case Feel::dotted:  return step * 1.5;
                case Feel::straight: break;
            }

            return step;
        }

        juce::String divisionName (Division d)
        {
            switch (d)
            {
                case Division::bar:          return "Bar";
                case Division::beat:         return "Beat";
                case Division::half:         return "1/2 beat";
                case Division::quarter:      return "1/4 beat";
                case Division::eighth:       return "1/8 beat";
                case Division::sixteenth:    return "1/16 beat";
                case Division::thirtySecond: return "1/32 beat";
                case Division::sixtyFourth:  return "1/64 beat";
            }

            return {};
        }

        /** The divisions from finest to coarsest, for adaptive zoom. */
        constexpr Division ladder[] = { Division::sixtyFourth, Division::thirtySecond, Division::sixteenth,
                                        Division::eighth, Division::quarter, Division::half, Division::beat,
                                        Division::bar };

        /** Adaptive: the chosen division, or a coarser one until lines are minPixels apart. Beyond
            a bar, whole multiples of bars. */
        double adaptiveStep (const Grid& g, double beatsPerBar, double pixelsPerBeat, Division* chosen)
        {
            bool reached = false;

            for (auto d : ladder)
            {
                reached = reached || d == g.division;

                if (! reached)
                    continue;

                const auto step = withFeel (baseBeats (d, beatsPerBar), g.feel);

                if (step * pixelsPerBeat >= Grid::minPixels)
                {
                    if (chosen != nullptr) *chosen = d;
                    return step;
                }
            }

            auto bars = beatsPerBar;

            while (bars * pixelsPerBeat < Grid::minPixels && bars < 1.0e6)
                bars *= 2.0;

            if (chosen != nullptr) *chosen = Division::bar;
            return bars;
        }
    }

    double Grid::stepBeats (double beatsPerBar, double pixelsPerBeat) const
    {
        if (adaptive)
            return adaptiveStep (*this, beatsPerBar, pixelsPerBeat, nullptr);

        return withFeel (baseBeats (division, beatsPerBar), feel);
    }

    double Grid::nearestLine (double beat, double beatsPerBar, double pixelsPerBeat) const
    {
        const auto step = stepBeats (beatsPerBar, pixelsPerBeat);
        return std::floor (beat / step + 0.5) * step;
    }

    std::vector<double> Grid::lines (double startBeat, double endBeat, double beatsPerBar,
                                     double pixelsPerBeat, size_t maxLines) const
    {
        std::vector<double> out;
        const auto step = stepBeats (beatsPerBar, pixelsPerBeat);

        if (step <= 0.0 || endBeat < startBeat)
            return out;

        for (auto i = std::ceil (startBeat / step - 1.0e-9); out.size() < maxLines; i += 1.0)
        {
            const auto b = i * step;

            if (b > endBeat + 1.0e-9)
                break;

            out.push_back (b);
        }

        return out;
    }

    juce::String Grid::describe (double beatsPerBar, double pixelsPerBeat) const
    {
        auto d = division;

        if (adaptive)
            adaptiveStep (*this, beatsPerBar, pixelsPerBeat, &d);

        auto name = divisionName (d);

        if (feel == Feel::triplet)    name << " triplet";
        else if (feel == Feel::dotted) name << " dotted";

        return adaptive ? "Adaptive (" + name + ")" : name;
    }

    SnapResult snap (double beat, const Grid& grid, const SnapTargets& targets, double beatsPerBar,
                     double pixelsPerBeat, bool bypass, double radiusPixels)
    {
        if (bypass || pixelsPerBeat <= 0.0)
            return { beat, SnapResult::To::nothing };

        // With the grid on, a position always lands on a line, however far away it is.
        SnapResult best { beat, SnapResult::To::nothing };
        auto bestDistance = radiusPixels / pixelsPerBeat;

        if (grid.enabled)
        {
            const auto line = grid.nearestLine (beat, beatsPerBar, pixelsPerBeat);
            best = { line, SnapResult::To::grid };
            bestDistance = std::abs (line - beat);
        }

        // Clip edges, markers and the playhead pull within the radius, and win a tie with the grid,
        // so a clip can meet another clip's edge exactly even when that edge is off the grid.
        const auto radius = radiusPixels / pixelsPerBeat;
        const auto consider = [&] (double candidate, SnapResult::To kind)
        {
            const auto d = std::abs (candidate - beat);

            if (d > radius)
                return;

            const bool wins = best.to == SnapResult::To::nothing
                           || (best.to == SnapResult::To::grid ? d <= bestDistance + 1.0e-12 : d < bestDistance);

            if (wins)
            {
                bestDistance = d;
                best = { candidate, kind };
            }
        };

        for (auto e : targets.clipEdges)  consider (e, SnapResult::To::clipEdge);
        for (auto m : targets.markers)    consider (m, SnapResult::To::marker);
        if (targets.playhead)             consider (*targets.playhead, SnapResult::To::playhead);

        return best;
    }
}
