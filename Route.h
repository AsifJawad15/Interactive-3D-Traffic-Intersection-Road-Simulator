#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <vector>

// A vehicle route built from exactly two kinds of piece: straight lines and
// circular arcs, both in the ground plane.
//
// Coordinates are stored as glm::vec2 where .x is world x and .y is world z.
// Headings follow the convention already used by the rest of the project:
//
//     forward = (sin(yaw), 0, cos(yaw))
//
// so yaw 0 faces +z (northbound), yaw 90 faces +x (eastbound).
//
// Everything the simulation needs comes from sampling one scalar - the distance
// travelled along the route. Position, heading and turn direction all fall out
// of that single number, so a turning car is not a special case: it is the same
// update as a straight car, evaluated on a curved segment.

struct RouteSample
{
    glm::vec3 position {0.0f};
    float headingDegrees = 0.0f;

    // 0 on a straight, +1 while turning right, -1 while turning left.
    // The simulation reads it to steer the front wheels.
    float turnSign = 0.0f;

    // 1 / radius on an arc, 0 on a straight. Cornering speed is read from it:
    // v = sqrt(lateral acceleration * radius).
    float curvature = 0.0f;
};

class Route
{
public:
    // Height at which vehicle origins sit above the road surface.
    static constexpr float rideHeight = 0.10f;

    void addLine(glm::vec2 from, glm::vec2 to);

    // Angles are in degrees about the arc centre. A point at angle t sits at
    // centre + radius * (cos t, sin t). A positive sweep advances t, which
    // under the heading convention above is a turn to the vehicle's right.
    void addArc(glm::vec2 centre, float radius, float startDegrees, float sweepDegrees);

    // Append another route's pieces to the end of this one. Used to bolt a
    // rotated exit tail onto a shared entry + ring section.
    void append(const Route& other);

    RouteSample sample(float distance) const;

    // Like sample, but before the start and past the end the route carries on
    // in a straight line. Every route in the city starts and ends halfway
    // along a straight road, so this is exactly where the previous and the
    // next route run: a long vehicle's rear axle can be found before the
    // start of the route its front axle is on.
    RouteSample sampleExtended(float distance) const;

    // The point of the route (extended as above) nearest to `point`, among
    // distances from `from` to `to`: how far along it lies, and how far to
    // the left of the direction of travel `point` is (negative: to the right).
    void project(glm::vec2 point, float from, float to, float& distance, float& lateral) const;

    float totalLength() const { return totalLength_; }
    bool empty() const { return segments_.empty(); }

    // The intersection is four-fold symmetric, so every route is authored once
    // for the northbound approach and rotated by 90, 180 and 270 degrees to
    // produce the other three. Rotating by +90 maps northbound to eastbound.
    Route rotated(float degrees) const;

    // The same route moved in the ground plane, for placing a junction's
    // routes at its own position in a larger network.
    Route translated(glm::vec2 offset) const;

    // The same path driven the other way: the end becomes the start.
    Route reversed() const;

private:
    struct Segment
    {
        bool isArc = false;

        // Straight: P(s) = start + s * direction, with direction normalised.
        glm::vec2 start {0.0f};
        glm::vec2 direction {0.0f};

        // Arc: theta(s) = startAngle + sign * s / radius (arc-length
        // parameterisation, because s = radius * delta-theta).
        glm::vec2 centre {0.0f};
        float radius = 0.0f;
        float startAngle = 0.0f;  // radians
        float sweepAngle = 0.0f;  // radians, signed

        float length = 0.0f;
    };

    std::vector<Segment> segments_;
    float totalLength_ = 0.0f;
};
