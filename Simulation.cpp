#include "Simulation.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace
{
    constexpr float laneLoopLength = 88.0f;
    constexpr float minimumCenterGap = 5.4f;

    float sigmoidProgress(float value)
    {
        value = glm::clamp(value, 0.0f, 1.0f);
        constexpr float steepness = 12.0f;
        const auto logistic = [](float x)
        {
            return 1.0f / (1.0f + std::exp(-steepness * (x - 0.5f)));
        };
        const float low = logistic(0.0f);
        const float high = logistic(1.0f);
        return (logistic(value) - low) / (high - low);
    }

    bool laneIsNorthSouth(Lane lane)
    {
        return lane == Lane::Northbound || lane == Lane::Southbound;
    }
}

TrafficSystem::TrafficSystem()
{
    reset();
}

void TrafficSystem::reset()
{
    phase_ = TrafficPhase::NorthSouthGreen;
    phaseElapsed_ = 0.0f;
    vehicles_.clear();

    const std::array<glm::vec3, 8> colors = {
        glm::vec3{0.82f, 0.06f, 0.035f}, glm::vec3{0.07f, 0.30f, 0.88f},
        glm::vec3{0.95f, 0.58f, 0.04f}, glm::vec3{0.13f, 0.62f, 0.34f},
        glm::vec3{0.52f, 0.10f, 0.74f}, glm::vec3{0.04f, 0.60f, 0.74f},
        glm::vec3{0.86f, 0.86f, 0.89f}, glm::vec3{0.16f, 0.18f, 0.21f}
    };

    auto addVehicle = [this, &colors](Lane lane, glm::vec3 position, float yaw, std::size_t colorIndex)
    {
        Vehicle vehicle;
        vehicle.id = vehicles_.size();
        vehicle.lane = lane;
        vehicle.position = position;
        vehicle.color = colors[colorIndex];
        vehicle.yawDegrees = yaw;
        vehicle.maximumSpeed = 5.4f + static_cast<float>(vehicle.id % 3) * 0.35f;
        vehicles_.push_back(vehicle);
    };

    addVehicle(Lane::Northbound, {-3.0f, 0.10f, -34.0f},   0.0f, 0);
    addVehicle(Lane::Northbound, {-3.0f, 0.10f, -20.0f},   0.0f, 1);
    addVehicle(Lane::Southbound, { 3.0f, 0.10f,  34.0f}, 180.0f, 2);
    addVehicle(Lane::Southbound, { 3.0f, 0.10f,  20.0f}, 180.0f, 3);
    addVehicle(Lane::Eastbound,  {-34.0f, 0.10f,  3.0f},  90.0f, 4);
    addVehicle(Lane::Eastbound,  {-20.0f, 0.10f,  3.0f},  90.0f, 5);
    addVehicle(Lane::Westbound,  { 34.0f, 0.10f, -3.0f}, -90.0f, 6);
    addVehicle(Lane::Westbound,  { 20.0f, 0.10f, -3.0f}, -90.0f, 7);
}

void TrafficSystem::update(float dt)
{
    dt = glm::clamp(dt, 0.0f, 0.05f);
    phaseElapsed_ += dt;
    const bool yellowPhase = phase_ == TrafficPhase::NorthSouthYellow ||
                             phase_ == TrafficPhase::EastWestYellow;
    if (phaseElapsed_ >= (yellowPhase ? yellowDuration_ : greenDuration_))
        advancePhase();

    for (std::size_t index = 0; index < vehicles_.size(); ++index)
        setTargetSpeed(vehicles_[index], desiredSpeed(index));

    for (std::size_t index = 0; index < vehicles_.size(); ++index)
    {
        Vehicle& vehicle = vehicles_[index];
        vehicle.transitionElapsed += dt;
        const float progress = sigmoidProgress(vehicle.transitionElapsed / vehicle.transitionDuration);
        vehicle.currentSpeed = vehicle.transitionStartSpeed +
            (vehicle.targetSpeed - vehicle.transitionStartSpeed) * progress;
        if (vehicle.transitionElapsed >= vehicle.transitionDuration)
            vehicle.currentSpeed = vehicle.targetSpeed;

        const SignalState signal = signalFor(vehicle.lane);
        const float stopDistance = distanceToStopLine(vehicle);
        const bool mustStop = signal == SignalState::Red ||
                              (signal == SignalState::Yellow && stopDistance > 4.8f);

        float travel = vehicle.currentSpeed * dt;
        const float leaderGap = closestLeaderGap(index);
        if (leaderGap < std::numeric_limits<float>::max())
            travel = std::min(travel, std::max(leaderGap - minimumCenterGap, 0.0f));
        if (mustStop && stopDistance > 0.0f)
            travel = std::min(travel, stopDistance);

        vehicle.position += laneDirection(vehicle.lane) * travel;
        vehicle.wheelAngleDegrees = std::fmod(
            vehicle.wheelAngleDegrees + glm::degrees(travel / 0.34f), 360.0f);

        if ((mustStop && stopDistance > 0.0f && travel >= stopDistance - 0.0001f) ||
            (leaderGap <= minimumCenterGap + 0.001f && travel <= 0.0001f))
        {
            vehicle.currentSpeed = 0.0f;
            vehicle.targetSpeed = 0.0f;
            vehicle.transitionStartSpeed = 0.0f;
            vehicle.transitionElapsed = vehicle.transitionDuration;
        }

        wrapVehicle(vehicle);
    }
}

void TrafficSystem::advancePhase()
{
    switch (phase_)
    {
    case TrafficPhase::NorthSouthGreen:  phase_ = TrafficPhase::NorthSouthYellow; break;
    case TrafficPhase::NorthSouthYellow: phase_ = TrafficPhase::EastWestGreen; break;
    case TrafficPhase::EastWestGreen:    phase_ = TrafficPhase::EastWestYellow; break;
    case TrafficPhase::EastWestYellow:   phase_ = TrafficPhase::NorthSouthGreen; break;
    }
    phaseElapsed_ = 0.0f;
}

SignalState TrafficSystem::signalFor(Lane lane) const
{
    const bool northSouth = laneIsNorthSouth(lane);
    if (phase_ == TrafficPhase::NorthSouthGreen)
        return northSouth ? SignalState::Green : SignalState::Red;
    if (phase_ == TrafficPhase::NorthSouthYellow)
        return northSouth ? SignalState::Yellow : SignalState::Red;
    if (phase_ == TrafficPhase::EastWestGreen)
        return northSouth ? SignalState::Red : SignalState::Green;
    return northSouth ? SignalState::Red : SignalState::Yellow;
}

std::string TrafficSystem::phaseName() const
{
    switch (phase_)
    {
    case TrafficPhase::NorthSouthGreen:  return "N/S GREEN";
    case TrafficPhase::NorthSouthYellow: return "N/S YELLOW";
    case TrafficPhase::EastWestGreen:    return "E/W GREEN";
    case TrafficPhase::EastWestYellow:   return "E/W YELLOW";
    }
    return "UNKNOWN";
}

glm::vec3 TrafficSystem::laneDirection(Lane lane)
{
    switch (lane)
    {
    case Lane::Northbound: return { 0.0f, 0.0f,  1.0f};
    case Lane::Southbound: return { 0.0f, 0.0f, -1.0f};
    case Lane::Eastbound:  return { 1.0f, 0.0f,  0.0f};
    case Lane::Westbound:  return {-1.0f, 0.0f,  0.0f};
    }
    return glm::vec3{0.0f};
}

glm::vec3 TrafficSystem::stopPoint(Lane lane)
{
    switch (lane)
    {
    case Lane::Northbound: return {-3.0f, 0.10f, -9.6f};
    case Lane::Southbound: return { 3.0f, 0.10f,  9.6f};
    case Lane::Eastbound:  return {-9.6f, 0.10f,  3.0f};
    case Lane::Westbound:  return { 9.6f, 0.10f, -3.0f};
    }
    return glm::vec3{0.0f};
}

float TrafficSystem::distanceToStopLine(const Vehicle& vehicle)
{
    return glm::dot(stopPoint(vehicle.lane) - vehicle.position, laneDirection(vehicle.lane));
}

float TrafficSystem::closestLeaderGap(std::size_t vehicleIndex) const
{
    const Vehicle& vehicle = vehicles_[vehicleIndex];
    const glm::vec3 direction = laneDirection(vehicle.lane);
    float closest = std::numeric_limits<float>::max();

    for (std::size_t index = 0; index < vehicles_.size(); ++index)
    {
        if (index == vehicleIndex || vehicles_[index].lane != vehicle.lane)
            continue;
        float gap = glm::dot(vehicles_[index].position - vehicle.position, direction);
        if (gap <= 0.0f)
            gap += laneLoopLength;
        closest = std::min(closest, gap);
    }
    return closest;
}

float TrafficSystem::desiredSpeed(std::size_t vehicleIndex) const
{
    const Vehicle& vehicle = vehicles_[vehicleIndex];
    float target = vehicle.maximumSpeed;

    const float gap = closestLeaderGap(vehicleIndex);
    if (gap < 6.4f)
        target = 0.0f;
    else if (gap < 8.5f)
        target = std::min(target, 1.8f);
    else if (gap < 11.5f)
        target = std::min(target, 3.6f);
    else if (gap < 14.0f)
        target = std::min(target, 4.8f);

    const float stopDistance = distanceToStopLine(vehicle);
    const SignalState signal = signalFor(vehicle.lane);
    const bool mustStop = signal == SignalState::Red ||
                          (signal == SignalState::Yellow && stopDistance > 4.8f);
    if (mustStop && stopDistance > 0.0f && stopDistance < 18.0f)
    {
        if (stopDistance < 2.4f)
            target = 0.0f;
        else if (stopDistance < 5.5f)
            target = std::min(target, 1.4f);
        else if (stopDistance < 10.0f)
            target = std::min(target, 2.8f);
        else if (stopDistance < 14.0f)
            target = std::min(target, 4.2f);
        else
            target = std::min(target, 5.0f);
    }
    return target;
}

void TrafficSystem::setTargetSpeed(Vehicle& vehicle, float targetSpeed)
{
    targetSpeed = glm::clamp(targetSpeed, 0.0f, vehicle.maximumSpeed);
    if (std::abs(targetSpeed - vehicle.targetSpeed) < 0.12f)
        return;

    vehicle.transitionStartSpeed = vehicle.currentSpeed;
    vehicle.targetSpeed = targetSpeed;
    vehicle.transitionElapsed = 0.0f;
    const float speedChange = std::abs(vehicle.targetSpeed - vehicle.transitionStartSpeed);
    vehicle.transitionDuration = 0.8f + 0.75f * (speedChange / vehicle.maximumSpeed);
}

void TrafficSystem::wrapVehicle(Vehicle& vehicle)
{
    switch (vehicle.lane)
    {
    case Lane::Northbound:
        if (vehicle.position.z > 44.0f) vehicle.position.z -= laneLoopLength;
        break;
    case Lane::Southbound:
        if (vehicle.position.z < -44.0f) vehicle.position.z += laneLoopLength;
        break;
    case Lane::Eastbound:
        if (vehicle.position.x > 44.0f) vehicle.position.x -= laneLoopLength;
        break;
    case Lane::Westbound:
        if (vehicle.position.x < -44.0f) vehicle.position.x += laneLoopLength;
        break;
    }
}
