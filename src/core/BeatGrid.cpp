#include "core/BeatGrid.h"

#include <algorithm>
#include <cmath>

namespace harmonia::core::beat_finder::grid
{
    namespace
    {
        /** How well the grid `start + n * step` holds the hits: each counts by its strength, less
            the further off it is. */
        double fitOf (const std::vector<Hit>& hits, double start, double step, double tolerance)
        {
            double sum = 0.0;

            for (const auto& hit : hits)
            {
                const auto off = std::abs (offGrid (hit.seconds - start, step));

                if (off < tolerance)
                    sum += hit.strength * (1.0 - off / tolerance);
            }

            return sum;
        }

        /** The strongest hits, at most `count` of them. */
        std::vector<Hit> strongest (std::vector<Hit> hits, size_t count)
        {
            std::sort (hits.begin(), hits.end(), [] (const Hit& a, const Hit& b) { return a.strength > b.strength; });

            if (hits.size() > count)
                hits.resize (count);

            return hits;
        }
    }

    double offGrid (double value, double step)
    {
        return value - std::round (value / step) * step;
    }


    Grid bestGrid (const std::vector<Hit>& hits, double beatPeriod, double spread, double increment)
    {
        const auto anchors = strongest (hits, 6);
        const auto reach = (int) std::round (spread / increment);
        Grid best;
        auto bestFit = -1.0;

        for (int i = -reach; i <= reach; ++i)
        {
            const auto step = beatPeriod * (1.0 + i * increment) / 4.0;
            const auto tolerance = std::min (onGridSeconds * 1.5, step * 0.3);

            for (const auto& anchor : anchors)
            {
                const auto fit = fitOf (hits, anchor.seconds, step, tolerance);

                if (fit > bestFit)
                {
                    bestFit = fit;
                    best = { anchor.seconds, step };
                }
            }
        }

        return best;
    }


    double refine (const std::vector<Hit>& hits, Grid& grid, double& offGridSeconds)
    {
        auto onGrid = 0.0, all = 0.0;

        for (int pass = 0; pass < 3; ++pass)
        {
            double sw = 0.0, sn = 0.0, st = 0.0, snn = 0.0, snt = 0.0, squares = 0.0;
            all = 0.0;

            for (const auto& hit : hits)
            {
                all += hit.strength;
                const auto n = std::round ((hit.seconds - grid.start) / grid.step);
                const auto off = hit.seconds - (grid.start + n * grid.step);

                if (std::abs (off) > std::min (onGridSeconds, grid.step * 0.3))
                    continue;

                const double w = hit.strength;
                sw += w;
                sn += w * n;
                st += w * hit.seconds;
                snn += w * n * n;
                snt += w * n * hit.seconds;
                squares += w * off * off;
            }

            onGrid = sw;
            offGridSeconds = sw > 0.0 ? std::sqrt (squares / sw) : 0.0;
            const auto spread = sw * snn - sn * sn;

            if (sw <= 0.0 || std::abs (spread) < 1.0e-12)
                break;

            const auto step = (sw * snt - sn * st) / spread;
            const auto start = (st - step * sn) / sw;

            if (step <= 0.0)
                break;

            grid = { start, step };
        }

        return all > 0.0 ? onGrid / all : 0.0;
    }
}
