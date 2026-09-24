#include "Route.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

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
        result.curvature = 1.0f / found->radius;
    }

    result.position = glm::vec3(position.x, rideHeight, position.y);
    result.headingDegrees = headingDegreesOf(heading);
    return result;
}

RouteSample Route::sampleExtended(float distance) const
{
    if (distance >= 0.0f && distance <= totalLength_)
        return sample(distance);

    const bool before = distance < 0.0f;
    RouteSample result = sample(before ? 0.0f : totalLength_);
    const float yaw = glm::radians(result.headingDegrees);
    const float beyond = before ? distance : distance - totalLength_;
    result.position += glm::vec3(std::sin(yaw), 0.0f, std::cos(yaw)) * beyond;
    result.turnSign = 0.0f;
    result.curvature = 0.0f;
    return result;
}

void Route::project(glm::vec2 point, float from, float to, float& distance, float& lateral) const
{
    float bestSquared = std::numeric_limits<float>::max();
    distance = from;

    // One candidate per piece: the nearest point of that piece, limited to
    // [from, to]. The straight run before the start and after the end count
    // as two more pieces.
    const auto consider = [&](float along)
    {
        along = glm::clamp(along, from, to);
        const glm::vec3 position = sampleExtended(along).position;
        const glm::vec2 difference = point - glm::vec2(position.x, position.z);
        const float squared = glm::dot(difference, difference);
        if (squared < bestSquared)
        {
            bestSquared = squared;
            distance = along;
        }
    };

    {
        const RouteSample start = sample(0.0f);
        const float yaw = glm::radians(start.headingDegrees);
        const glm::vec2 forward {std::sin(yaw), std::cos(yaw)};
        consider(std::min(0.0f, glm::dot(point - glm::vec2(start.position.x, start.position.z), forward)));
        const RouteSample end = sample(totalLength_);
        const float endYaw = glm::radians(end.headingDegrees);
        const glm::vec2 endForward {std::sin(endYaw), std::cos(endYaw)};
        consider(totalLength_ + std::max(0.0f, glm::dot(point - glm::vec2(end.position.x, end.position.z), endForward)));
    }

    float offset = 0.0f;
    for (const Segment& segment : segments_)
    {
        if (!segment.isArc)
        {
            consider(offset + glm::clamp(glm::dot(point - segment.start, segment.direction), 0.0f, segment.length));
        }
        else
        {
            // The angle of the point round the centre, measured from the
            // start of the arc in the direction of travel.
            const float sign = segment.sweepAngle >= 0.0f ? 1.0f : -1.0f;
            const glm::vec2 relative = point - segment.centre;
            float swept = sign * (std::atan2(relative.y, relative.x) - segment.startAngle);
            constexpr float twoPi = 6.28318530718f;
            swept = std::fmod(swept, twoPi);
            if (swept < 0.0f)
                swept += twoPi;
            const float sweep = std::abs(segment.sweepAngle);
            if (swept <= sweep)
            {
                consider(offset + swept * segment.radius);
            }
            else
            {
                consider(offset);
                consider(offset + segment.length);
            }
        }
        offset += segment.length;
    }

    const RouteSample nearest = sampleExtended(distance);
    const float yaw = glm::radians(nearest.headingDegrees);
    // Left of the direction of travel: for a heading (x, z) = (sin, cos),
    // the driver's left is (cos, -sin) (the lanes of right-hand traffic lie
    // at -x when heading +z).
    const glm::vec2 left {std::cos(yaw), -std::sin(yaw)};
    lateral = glm::dot(point - glm::vec2(nearest.position.x, nearest.position.z), left);
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

Route Route::translated(glm::vec2 offset) const
{
    Route result = *this;
    for (Segment& segment : result.segments_)
    {
        if (segment.isArc)
            segment.centre += offset;
        else
            segment.start += offset;
    }
    return result;
}

Route Route::reversed() const
{
    Route result;
    result.totalLength_ = totalLength_;
    result.segments_.reserve(segments_.size());

    for (auto segment = segments_.rbegin(); segment != segments_.rend(); ++segment)
    {
        Segment flipped = *segment;
        if (!segment->isArc)
        {
            // Start from the old end point and run back along the line.
            flipped.start = segment->start + segment->direction * segment->length;
            flipped.direction = -segment->direction;
        }
        else
        {
            // Start at the old end angle and sweep back the other way.
            flipped.startAngle = segment->startAngle + segment->sweepAngle;
            flipped.sweepAngle = -segment->sweepAngle;
        }
        result.segments_.push_back(flipped);
    }

    return result;
}
