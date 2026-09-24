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

glm::vec2 pushOut(const OrientedBox& a, const OrientedBox& b)
{
    // Of the four candidate axes, the one with the least overlap is the
    // cheapest way out; if any axis separates them there is nothing to do.
    const std::array<glm::vec2, 4> axes = {
        a.forward, glm::vec2{a.forward.y, -a.forward.x},
        b.forward, glm::vec2{b.forward.y, -b.forward.x}
    };
    const glm::vec2 between = a.centre - b.centre;
    float smallest = 1.0e9f;
    glm::vec2 push {0.0f};
    for (const glm::vec2& axis : axes)
    {
        const float along = glm::dot(between, axis);
        const float overlap = projectedRadius(a, axis) + projectedRadius(b, axis) - std::abs(along);
        if (overlap <= 0.0f)
            return glm::vec2 {0.0f};
        if (overlap < smallest)
        {
            smallest = overlap;
            push = axis * (along >= 0.0f ? overlap : -overlap);
        }
    }
    return push;
}

glm::vec2 closestPointOnBox(const OrientedBox& box, glm::vec2 point)
{
    const glm::vec2 side {box.forward.y, -box.forward.x};
    const glm::vec2 local = point - box.centre;
    const float x = glm::clamp(glm::dot(local, side), -box.halfExtents.x, box.halfExtents.x);
    const float y = glm::clamp(glm::dot(local, box.forward), -box.halfExtents.y, box.halfExtents.y);
    return box.centre + side * x + box.forward * y;
}

glm::vec2 pushOut(const Circle& a, const OrientedBox& b)
{
    const glm::vec2 nearest = closestPointOnBox(b, a.centre);
    const glm::vec2 away = a.centre - nearest;
    const float distance = glm::length(away);
    if (distance >= a.radius)
        return glm::vec2 {0.0f};
    if (distance > 1.0e-5f)
        return away / distance * (a.radius - distance);

    // The centre is inside the box: leave by the nearest face.
    const glm::vec2 side {b.forward.y, -b.forward.x};
    const glm::vec2 local = a.centre - b.centre;
    const float x = glm::dot(local, side);
    const float y = glm::dot(local, b.forward);
    const float outX = b.halfExtents.x - std::abs(x);
    const float outY = b.halfExtents.y - std::abs(y);
    if (outX < outY)
        return side * ((x >= 0.0f ? 1.0f : -1.0f) * (outX + a.radius));
    return b.forward * ((y >= 0.0f ? 1.0f : -1.0f) * (outY + a.radius));
}

glm::vec2 pushOut(const OrientedBox& a, const Circle& b)
{
    return -pushOut(b, a);
}

glm::vec2 pushOut(const Circle& a, const Circle& b)
{
    const glm::vec2 away = a.centre - b.centre;
    const float distance = glm::length(away);
    const float overlap = a.radius + b.radius - distance;
    if (overlap <= 0.0f)
        return glm::vec2 {0.0f};
    if (distance < 1.0e-5f)
        return {overlap, 0.0f};
    return away / distance * overlap;
}
