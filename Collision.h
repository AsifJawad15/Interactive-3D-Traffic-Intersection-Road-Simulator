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

// Someone the AI traffic must not drive into - the player's car, or the
// player on foot - and how fast they are moving.
struct Guest
{
    OrientedBox body;
    glm::vec2 velocity {0.0f};
};

// A circle on the ground: a tree trunk, a post, a person, an island.
struct Circle
{
    glm::vec2 centre {0.0f};
    float radius = 0.5f;
};

// How far, and which way, `a` must move so it no longer overlaps the other
// shape: the shortest such move (the minimum translation vector). Zero when
// they do not overlap.
glm::vec2 pushOut(const OrientedBox& a, const OrientedBox& b);
glm::vec2 pushOut(const OrientedBox& a, const Circle& b);
glm::vec2 pushOut(const Circle& a, const OrientedBox& b);
glm::vec2 pushOut(const Circle& a, const Circle& b);

// The point of the box nearest to `point` (the point itself when inside).
glm::vec2 closestPointOnBox(const OrientedBox& box, glm::vec2 point);
