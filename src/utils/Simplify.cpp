//Copyright (c) 2022 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#include <limits>
#include <queue> //Priority queue to prioritise removing unimportant vertices.

#include "Simplify.h"
#include "linearAlg2D.h"
#include "SVG.h" //DEBUG!

namespace cura
{

Simplify::Simplify(const coord_t max_resolution, const coord_t max_deviation, const coord_t max_area_deviation)
    : max_resolution(max_resolution)
    , max_deviation(max_deviation)
    , max_area_deviation(max_area_deviation)
{}

Simplify::Simplify(const Settings& settings)
    : max_resolution(settings.get<coord_t>("meshfix_maximum_resolution"))
    , max_deviation(settings.get<coord_t>("meshfix_maximum_deviation"))
    , max_area_deviation(settings.get<coord_t>("meshfix_maximum_area_deviation"))
{}

coord_t Simplify::importance(const PolygonRef& polygon, const std::vector<bool>& to_delete, const size_t index, const bool is_closed) const
{
    const size_t poly_size = polygon.size();
    if(!is_closed && (index == 0 || index == poly_size - 1))
    {
        return std::numeric_limits<coord_t>::max(); //Endpoints of the polyline must always be retained.
    }
    //From here on out we can safely look at the vertex neighbors and assume it's a polygon. We won't go out of bounds of the polyline.

    const Point& vertex = polygon[index];
    const Point& before = polygon[previousNotDeleted(index, to_delete)];
    const Point& after = polygon[nextNotDeleted(index, to_delete)];
    const coord_t deviation2 = LinearAlg2D::getDist2FromLine(vertex, before, after);
    if(deviation2 <= min_resolution * min_resolution) //Deviation so small that it's always desired to remove them.
    {
        return deviation2;
    }
    if(vSize2(before - vertex) > max_resolution * max_resolution && vSize2(after - vertex) > max_resolution * max_resolution)
    {
        return std::numeric_limits<coord_t>::max(); //Long line segments, no need to remove this one.
    }
    return deviation2;
}

Polygon Simplify::polygon(const PolygonRef polygon)
{
    constexpr bool is_closed = true;
    return simplify(polygon, is_closed);
}

Polygon Simplify::simplify(const PolygonRef polygon, const bool is_closed)
{
    const size_t min_size = is_closed ? 2 : 3;
    if(polygon.size() < min_size) //For polygon, 2 or fewer vertices is degenerate. Delete it. For polyline, 1 vertex is degenerate.
    {
        return Polygon();
    }
    if(polygon.size() == min_size) //For polygon, don't reduce below 3. For polyline, not below 2.
    {
        return polygon;
    }

    std::vector<bool> to_delete(polygon.size(), false);
    auto comparator = [](const std::pair<size_t, coord_t>& vertex_a, const std::pair<size_t, coord_t>& vertex_b)
    {
        return vertex_a.second > vertex_b.second || (vertex_a.second == vertex_b.second && vertex_a.first > vertex_b.first);
    };
    std::priority_queue<std::pair<size_t, coord_t>, std::vector<std::pair<size_t, coord_t>>, decltype(comparator)> by_importance(comparator);

    //Add the initial points.
    for(size_t i = 0; i < polygon.size(); ++i)
    {
        const coord_t vertex_importance = importance(polygon, to_delete, i, is_closed);
        by_importance.emplace(i, vertex_importance);
    }

    //Iteratively remove the least important point until a threshold.
    Polygon result = polygon; //Make a copy so that we can also shift vertices.
    coord_t vertex_importance = 0;
    while(by_importance.size() > 3)
    {
        std::pair<size_t, coord_t> vertex = by_importance.top();
        by_importance.pop();
        //The importance may have changed since this vertex was inserted. Re-compute it now.
        //If it doesn't change, it's safe to process.
        vertex_importance = importance(result, to_delete, vertex.first, is_closed);
        if(vertex_importance != vertex.second)
        {
            by_importance.emplace(vertex.first, vertex_importance); //Re-insert with updated importance.
            continue;
        }

        if(vertex_importance <= max_deviation * max_deviation)
        {
            remove(result, to_delete, vertex.first, vertex_importance, is_closed);
        }
    }

    //Now remove the marked vertices in one sweep.
    Polygon filtered;
    for(size_t i = 0; i < result.size(); ++i)
    {
        if(!to_delete[i])
        {
            filtered.add(result[i]);
        }
    }

    return filtered;
}

void Simplify::remove(Polygon& polygon, std::vector<bool>& to_delete, const size_t vertex, const coord_t deviation2, const bool is_closed) const
{
    if(deviation2 <= min_resolution * min_resolution)
    {
        //At less than the minimum resolution we're always allowed to delete the vertex.
        //Even if the adjacent line segments are very long.
        to_delete[vertex] = true;
        return;
    }

    const size_t before = previousNotDeleted(vertex, to_delete);
    const size_t after = nextNotDeleted(vertex, to_delete);
    const Point vertex_position = polygon[vertex];
    const Point before_position = polygon[before];
    const Point after_position = polygon[after];
    const coord_t length2_before = vSize2(vertex_position - before_position);
    const coord_t length2_after = vSize2(vertex_position - after_position);

    if(length2_before <= max_resolution * max_resolution && length2_after <= max_resolution * max_resolution) //Both adjacent line segments are short.
    {
        //Removing this vertex does little harm. No long lines will be shifted.
        to_delete[vertex] = true;
        return;
    }

    //Otherwise, one edge next to this vertex is longer than max_resolution. The other is shorter.
    //In this case we want to remove the short edge by replacing it with a vertex where the two surrounding edges intersect.
    //Find the two line segments surrounding the short edge here ("before" and "after" edges).
    Point before_from, before_to, after_from, after_to;
    if(length2_before <= length2_after) //Before is the shorter line.
    {
        if(!is_closed && before == 0) //No edge before the short edge.
        {
            return; //Edge cannot be deleted without shifting a long edge. Don't remove anything.
        }
        const size_t before_before = previousNotDeleted(before, to_delete);
        before_from = polygon[before_before];
        before_to = polygon[before];
        after_from = polygon[vertex];
        after_to = polygon[after];
    }
    else
    {
        if(!is_closed && after == polygon.size() - 1) //No edge after the short edge.
        {
            return; //Edge cannot be deleted without shifting a long edge. Don't remove anything.
        }
        const size_t after_after = nextNotDeleted(after, to_delete);
        before_from = polygon[before];
        before_to = polygon[vertex];
        after_from = polygon[after];
        after_to = polygon[after_after];
    }
    Point intersection;
    const bool did_intersect = LinearAlg2D::lineLineIntersection(before_from, before_to, after_from, after_to, intersection);
    if(!did_intersect) //Lines are parallel.
    {
        return; //Cannot remove edge without shifting a long edge. Don't remove anything.
    }
    const coord_t intersection_deviation = LinearAlg2D::getDist2FromLine(intersection, before_to, after_from);
    if(intersection_deviation <= max_deviation * max_deviation) //Intersection point doesn't deviate too much. Use it!
    {
        to_delete[vertex] = true;
        if(length2_before <= length2_after)
        {
            polygon[before] = intersection;
        }
        else
        {
            polygon[after] = intersection;
        }
    }
}

size_t Simplify::nextNotDeleted(size_t index, const std::vector<bool>& to_delete) const
{
    const size_t size = to_delete.size();
    for(index = (index + 1) % size; to_delete[index]; index = (index + 1) % size); //Changes the index variable in-place until we found one that is not deleted.
    return index;
}

size_t Simplify::previousNotDeleted(size_t index, const std::vector<bool>& to_delete) const
{
    const size_t size = to_delete.size();
    for(index = (index + size - 1) % size; to_delete[index]; index = (index + size - 1) % size); //Changes the index variable in-place until we found one that is not deleted.
    return index;
}

}