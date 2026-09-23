#include "Collision.h"

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>

OrientedBox makeOrientedBox(glm::vec2 centre, float headingDegrees, glm::vec2 halfExtents)
{
    const float yaw = glm::radians(headingDegrees);
    OrientedBox box;
    box.centre = centre;
    box.forward = {std::sin(yaw), std::cos(yaw)};
    box.halfExtents = halfExtents;
    return box;
}

namespace
{
    // Half the length of the box's shadow on a unit axis.
    float projectedRadius(const OrientedBox& box, glm::vec2 axis)
    {
        const glm::vec2 side {box.forward.y, -box.forward.x};
        return box.halfExtents.x * std::abs(glm::dot(side, axis)) +
               box.halfExtents.y * std::abs(glm::dot(box.forward, axis));
    }
}

float boxSeparation(const OrientedBox& a, const OrientedBox& b)
{
    // Two convex shapes are apart exactly when some axis separates their
    // shadows. For rectangles only the four edge normals need testing.
    const std::array<glm::vec2, 4> axes = {
        a.forward, glm::vec2{a.forward.y, -a.forward.x},
        b.forward, glm::vec2{b.forward.y, -b.forward.x}
    };

    const glm::vec2 between = b.centre - a.centre;
    float largestGap = -1.0e9f;
    for (const glm::vec2& axis : axes)
    {
        const float gap = std::abs(glm::dot(between, axis)) -
                          projectedRadius(a, axis) - projectedRadius(b, axis);
        largestGap = std::max(largestGap, gap);
    }
    return largestGap;
}
