#pragma once

#include "Route.h"

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

// The two ways this intersection can organise traffic. Both are built from the
// same routes and the same following rules; only the give-way test differs.
enum class IntersectionMode
{
    Signals,
    Roundabout
};

struct Vehicle
{
    std::size_t id = 0;
    Lane lane = Lane::Northbound;   // approach the vehicle entered from
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

    // Route following. `distance` is the only value that is integrated; the
    // position and heading below are read back out of the route each frame.
    std::size_t routeIndex = 0;
    float distance = 0.0f;
    float turnSign = 0.0f;
    float steerAngleDegrees = 0.0f;
};

class TrafficSystem
{
public:
    // Ground-plane geometry, shared with the renderer so the road markings and
    // the vehicle routes cannot drift apart.
    static constexpr float laneOffset = 3.0f;      // lane centre from centreline
    static constexpr float roadHalfWidth = 6.0f;   // kerb to centreline
    static constexpr float spawnRadius = 44.0f;    // where routes start and end
    static constexpr float stopLineRadius = 9.6f;  // signalised stop line
    static constexpr float ringRadius = 7.5f;      // roundabout circulating lane
    static constexpr float islandRadius = 4.6f;    // raised central island
    static constexpr float entryRadius = 5.0f;     // roundabout entry/exit arcs

    TrafficSystem();

    void update(float dt);
    void reset();
    void advancePhase();
    void toggleMode();
    void setMode(IntersectionMode mode);

    const std::vector<Vehicle>& vehicles() const { return vehicles_; }
    SignalState signalFor(Lane lane) const;
    TrafficPhase phase() const { return phase_; }
    std::string phaseName() const;

    IntersectionMode mode() const { return mode_; }
    std::string modeName() const;

    // 0 when the island is flush with the road, 1 when fully raised. Animated
    // so switching modes lifts the island out of the ground instead of popping.
    float islandHeight() const { return islandHeight_; }

    // Geometry checks that need no OpenGL context. Used by --self-test.
    bool selfTest(std::string& report) const;

    // Top-down ASCII map of every route of one mode, for checking by eye that
    // the arcs meet the lane centres and clear the island. Used by --plot.
    std::string topDownPlot(bool roundabout) const;

private:
    struct RouteInfo
    {
        Route route;
        Lane lane = Lane::Northbound;
        bool roundabout = false;

        // Distance along the route at which the stop line (signalised) or the
        // give-way line (roundabout) is reached.
        float giveWayDistance = 0.0f;

        // Distance at which a roundabout route actually joins the ring. Traffic
        // past this point is circulating and has priority.
        float mergeDistance = 0.0f;
    };

    std::vector<RouteInfo> routes_;
    std::vector<Vehicle> vehicles_;

    TrafficPhase phase_ = TrafficPhase::NorthSouthGreen;
    float phaseElapsed_ = 0.0f;
    float greenDuration_ = 8.0f;
    float yellowDuration_ = 2.0f;

    IntersectionMode mode_ = IntersectionMode::Signals;
    float islandHeight_ = 0.0f;
    unsigned int randomState_ = 12345u;

    void buildRoutes();
    void addRouteFamily(
        const Route& base, bool roundabout, float giveWayDistance, float mergeDistance);
    void placeVehiclesOnApproaches();

    const RouteInfo& routeFor(const Vehicle& vehicle) const { return routes_[vehicle.routeIndex]; }
    std::size_t pickRoute(Lane lane);
    Lane leastCrowdedApproach() const;

    float closestLeaderGap(std::size_t vehicleIndex) const;
    bool ringConflict(std::size_t vehicleIndex) const;
    float distanceToGiveWayLine(const Vehicle& vehicle) const;
    bool mustStopAtLine(const Vehicle& vehicle, std::size_t vehicleIndex) const;
    float desiredSpeed(std::size_t vehicleIndex) const;
    static void setTargetSpeed(Vehicle& vehicle, float targetSpeed);

    unsigned int nextRandom();
};
