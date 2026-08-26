#pragma once

#include <glm/vec3.hpp>

#include <cstddef>
#include <string>
#include <vector>

enum class Lane
{
    Northbound,
    Southbound,
    Eastbound,
    Westbound
};

enum class SignalState
{
    Red,
    Yellow,
    Green
};

enum class TrafficPhase
{
    NorthSouthGreen,
    NorthSouthYellow,
    EastWestGreen,
    EastWestYellow
};

struct Vehicle
{
    std::size_t id = 0;
    Lane lane = Lane::Northbound;
    glm::vec3 position {0.0f};
    glm::vec3 color {0.8f};
    float yawDegrees = 0.0f;
    float currentSpeed = 0.0f;
    float targetSpeed = 0.0f;
    float transitionStartSpeed = 0.0f;
    float transitionElapsed = 1.0f;
    float transitionDuration = 1.0f;
    float maximumSpeed = 6.0f;
    float wheelAngleDegrees = 0.0f;
};

class TrafficSystem
{
public:
    TrafficSystem();

    void update(float dt);
    void reset();
    void advancePhase();

    const std::vector<Vehicle>& vehicles() const { return vehicles_; }
    SignalState signalFor(Lane lane) const;
    TrafficPhase phase() const { return phase_; }
    std::string phaseName() const;

private:
    std::vector<Vehicle> vehicles_;
    TrafficPhase phase_ = TrafficPhase::NorthSouthGreen;
    float phaseElapsed_ = 0.0f;
    float greenDuration_ = 8.0f;
    float yellowDuration_ = 2.0f;

    static glm::vec3 laneDirection(Lane lane);
    static glm::vec3 stopPoint(Lane lane);
    static float distanceToStopLine(const Vehicle& vehicle);
    float closestLeaderGap(std::size_t vehicleIndex) const;
    float desiredSpeed(std::size_t vehicleIndex) const;
    void setTargetSpeed(Vehicle& vehicle, float targetSpeed);
    static void wrapVehicle(Vehicle& vehicle);
};
