//Copyright (c) 2020 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#include "DistributedBeadingStrategy.h"

namespace cura
{

    DistributedBeadingStrategy::Beading DistributedBeadingStrategy::compute(coord_t thickness, coord_t bead_count) const
    {
        Beading ret;

        ret.total_thickness = thickness;
        if (bead_count > 0)
        {
            ret.bead_widths.resize(bead_count, thickness / bead_count);
            for (coord_t bead_idx = 0; bead_idx < bead_count; bead_idx++)
            {
                ret.toolpath_locations.emplace_back(thickness * (bead_idx * 2 + 1) / bead_count / 2);
            }
            ret.left_over = 0;
        }
        else
        {
            ret.left_over = thickness;
        }

        return ret;
    }

    coord_t DistributedBeadingStrategy::getOptimalThickness(coord_t bead_count) const
    {
        return bead_count * optimal_width;
    }

    coord_t DistributedBeadingStrategy::getTransitionThickness(coord_t lower_bead_count) const
    {
        return lower_bead_count * optimal_width + optimal_width * wall_transition_threshold;
    }

    coord_t DistributedBeadingStrategy::getOptimalBeadCount(coord_t thickness) const
    {
        return (thickness + optimal_width / 2) / optimal_width;
    }

} // namespace cura