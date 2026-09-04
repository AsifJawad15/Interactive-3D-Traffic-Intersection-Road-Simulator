#include "Route.h"

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <cmath>

namespace
{
    // Yaw from a ground-plane heading, matching forward = (sin yaw, cos yaw).
    float headingDegreesOf(glm::vec2 heading)
    {
        return glm::degrees(std::atan2(heading.x, heading.y));
    }

    // Rotation of a ground-plane vector by phi, written so that rotating a
    // heading by phi also adds phi to its yaw:
    //
    //     sin(y + p) = sin y cos p + cos y sin p
    //     cos(y + p) = cos y cos p - sin y sin p
    //
    // which for (x, z) = (sin y, cos y) is exactly the matrix below.
    glm::vec2 rotatePoint(glm::vec2 point, float cosPhi, float sinPhi)
    {
        return {
            point.x * cosPhi + point.y * sinPhi,
            -point.x * sinPhi + point.y * cosPhi
        };
    }
}

void Route::addLine(glm::vec2 from, glm::vec2 to)
{
    const glm::vec2 delta = to - from;
    const float length = glm::length(delta);
    if (length <= 1e-5f)
        return;

    Segment segment;
    segment.isArc = false;
    segment.start = from;
    segment.direction = delta / length;
    segment.length = length;

    segments_.push_back(segment);
    totalLength_ += length;
}

void Route::addArc(glm::vec2 centre, float radius, float startDegrees, float sweepDegrees)
{
    if (radius <= 1e-5f || std::abs(sweepDegrees) <= 1e-5f)
        return;

    Segment segment;
    segment.isArc = true;
    segment.centre = centre;
    segment.radius = radius;
    segment.startAngle = glm::radians(startDegrees);
    segment.sweepAngle = glm::radians(sweepDegrees);

    // Arc length is radius times the angle swept: s = r * delta-theta.
    segment.length = radius * std::abs(segment.sweepAngle);

    segments_.push_back(segment);
    totalLength_ += segment.length;
}

void Route::append(const Route& other)
{
    segments_.insert(segments_.end(), other.segments_.begin(), other.segments_.end());
    totalLength_ += other.totalLength_;
}

RouteSample Route::sample(float distance) const
{
    RouteSample result;
    if (segments_.empty())
        return result;

    if (distance < 0.0f)
        distance = 0.0f;
    if (distance > totalLength_)
        distance = totalLength_;

    // Walk the chain until the remaining distance falls inside a segment.
    float remaining = distance;
    const Segment* found = &segments_.back();
    float local = segments_.back().length;

    for (const Segment& segment : segments_)
    {
        if (remaining <= segment.length)
        {
            found = &segment;
            local = remaining;
            break;
        }
        remaining -= segment.length;
    }

    glm::vec2 position {0.0f};
    glm::vec2 heading {0.0f, 1.0f};

    if (!found->isArc)
    {
        position = found->start + found->direction * local;
        heading = found->direction;
        result.turnSign = 0.0f;
    }
    else
    {
        const float sign = found->sweepAngle >= 0.0f ? 1.0f : -1.0f;
        const float theta = found->startAngle + sign * (local / found->radius);

        position = found->centre + found->radius * glm::vec2(std::cos(theta), std::sin(theta));

        // The tangent to the circle, taken in the direction of travel.
        heading = sign * glm::vec2(-std::sin(theta), std::cos(theta));
        result.turnSign = sign;
    }

    result.position = glm::vec3(position.x, rideHeight, position.y);
    result.headingDegrees = headingDegreesOf(heading);
    return result;
}

Route Route::rotated(float degrees) const
{
    const float phi = glm::radians(degrees);
    const float cosPhi = std::cos(phi);
    const float sinPhi = std::sin(phi);

    Route result;
    result.totalLength_ = totalLength_;
    result.segments_.reserve(segments_.size());

    for (const Segment& segment : segments_)
    {
        Segment rotatedSegment = segment;
        if (!segment.isArc)
        {
            rotatedSegment.start = rotatePoint(segment.start, cosPhi, sinPhi);
            rotatedSegment.direction = rotatePoint(segment.direction, cosPhi, sinPhi);
        }
        else
        {
            // Rotating centre + r * (cos t, sin t) by phi gives
            // R(centre) + r * (cos(t - phi), sin(t - phi)), so only the centre
            // and the start angle move. Radius and sweep are unchanged.
            rotatedSegment.centre = rotatePoint(segment.centre, cosPhi, sinPhi);
            rotatedSegment.startAngle = segment.startAngle - phi;
        }
        result.segments_.push_back(rotatedSegment);
    }

    return result;
}
