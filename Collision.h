#pragma once

#include <glm/vec2.hpp>

// Ground-plane collision shapes. As in Route, a glm::vec2 holds world (x, z),
// and headings follow forward = (sin yaw, cos yaw).

// A rectangle that can face any direction: a vehicle body seen from above.
struct OrientedBox
{
    glm::vec2 centre {0.0f};
    glm::vec2 forward {0.0f, 1.0f};     // unit vector along the length
    glm::vec2 halfExtents {1.0f, 2.0f}; // x = half width, y = half length
};

OrientedBox makeOrientedBox(glm::vec2 centre, float headingDegrees, glm::vec2 halfExtents);

// Separating-axis test. Returns the largest gap found along the four box axes:
// positive means the boxes are apart by at least that distance, zero or
// negative means they overlap (by at most that depth along the best axis).
float boxSeparation(const OrientedBox& a, const OrientedBox& b);

inline bool boxesOverlap(const OrientedBox& a, const OrientedBox& b)
{
    return boxSeparation(a, b) < 0.0f;
}
