#pragma once

#include "Collision.h"
#include "Route.h"

#include <glm/vec3.hpp>

#include <array>
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

// Which way a route leaves the junction. At the roundabout the first exit is
// the right turn, the second goes straight on and the third is the left turn.
enum class Turn
{
    Straight,
    Right,
    Left
};

enum class SignalState
{
    Red,
    Yellow,
    Green
};

// One full cycle. Each axis gets a protected left-turn arrow (skipped when
// nobody is waiting to turn left), then green for everyone on that axis with
// left turns giving way, then yellow and an all-red clearance interval.
enum class TrafficPhase
{
    NorthSouthLeftArrow,
    NorthSouthGreen,
    NorthSouthYellow,
    AllRedBeforeEastWest,
    EastWestLeftArrow,
    EastWestGreen,
    EastWestYellow,
    AllRedBeforeNorthSouth
};

// The two ways this intersection can organise traffic. Both are built from the
// same rules; only the routes, the priorities and the signals differ.
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
    float acceleration = 0.0f;
    float maximumSpeed = 6.0f;
    float wheelAngleDegrees = 0.0f;

    // Body size seen from above, used for gaps, conflicts and collision.
    float halfLength = 2.03f;
    float halfWidth = 0.94f;

    // Route following. `distance` is the only value that is integrated; the
    // position and heading above are read back out of the route every step.
    std::size_t routeIndex = 0;
    float distance = 0.0f;
    float turnSign = 0.0f;
    float steerAngleDegrees = 0.0f;

    // A vehicle that has left the scene and is waiting for a safe gap to
    // re-enter. It is not drawn and takes no part in the traffic.
    bool active = true;

    // True once the vehicle has been allowed through the junction. It then
    // holds a claim on every conflict zone on its way until it has left it.
    bool committed = false;
    std::vector<std::size_t> claims;

    float stoppedSeconds = 0.0f;

    // The pose at the start of the latest simulation step. Rendering blends
    // from this to the current pose, so motion is smooth at any refresh rate
    // even though the simulation itself only moves in 1/60 s steps.
    glm::vec3 previousPosition {0.0f};
    float previousYawDegrees = 0.0f;
    float previousWheelAngleDegrees = 0.0f;
    float previousSteerAngleDegrees = 0.0f;
};

// What the renderer and the cameras need of a vehicle, blended between two
// simulation steps.
struct VehiclePose
{
    glm::vec3 position {0.0f};
    glm::vec3 color {0.8f};
    float yawDegrees = 0.0f;
    float wheelAngleDegrees = 0.0f;
    float steerAngleDegrees = 0.0f;
    bool active = false;
};

// Measurements for --soak and the HUD. Body overlap is tested with the real
// vehicle outline every simulation step.
struct TrafficStats
{
    double simulatedSeconds = 0.0;
    std::size_t overlapSteps = 0;       // steps in which any two bodies overlapped
    std::size_t overlapPairsNow = 0;    // overlapping pairs in the latest step
    float closestBodyGap = 1.0e9f;      // smallest body separation seen, metres
    float longestStop = 0.0f;           // longest time any vehicle stood still
    std::array<std::size_t, 4> tripsPerApproach {};
    std::size_t trips = 0;
};

class TrafficSystem
{
public:
    // Ground-plane geometry, shared with the renderer so the road markings and
    // the vehicle routes cannot drift apart.
    static constexpr float laneOffset = 3.0f;      // lane centre from centreline
    static constexpr float roadHalfWidth = 6.0f;   // kerb to centreline
    static constexpr float spawnRadius = 44.0f;    // where routes start and end
    static constexpr float stopLineRadius = 10.4f; // car centre when stopped at the signal
    static constexpr float ringRadius = 7.5f;      // roundabout circulating lane
    static constexpr float islandRadius = 4.6f;    // raised central island
    static constexpr float entryRadius = 5.0f;     // roundabout entry/exit arcs

    explicit TrafficSystem(std::size_t vehicleCount = 8, unsigned int seed = 12345u);

    void update(float dt);
    void reset();
    void advancePhase();
    void toggleMode();
    void setMode(IntersectionMode mode);

    const std::vector<Vehicle>& vehicles() const { return vehicles_; }
    SignalState signalFor(Lane lane) const;          // the round lenses
    SignalState leftArrowFor(Lane lane) const;       // the left-turn arrow
    TrafficPhase phase() const { return phase_; }
    std::string phaseName() const;

    IntersectionMode mode() const { return mode_; }
    std::string modeName() const;

    // 0 when the island is flush with the road, 1 when fully raised. Animated
    // so switching modes lifts the island out of the ground instead of popping.
    float islandHeight() const { return islandHeight_; }

    // Render interpolation. `alpha` is how far the clock has run into the next
    // simulation step (0..1): poses are blended from the previous step's to
    // the current one. The output vector is reused, so this never allocates
    // once it has reached its size.
    void interpolatePoses(float alpha, std::vector<VehiclePose>& poses) const;
    float islandHeight(float alpha) const;

    const TrafficStats& stats() const { return stats_; }
    void resetStats();

    // One line per vehicle (route, position, speed, commit and claims), for
    // diagnosing a failed soak run.
    std::string describe() const;

    // Geometry and rule checks that need no OpenGL context. Used by --self-test.
    bool selfTest(std::string& report) const;

    // Top-down ASCII map of every route of one mode, with the conflict zones
    // marked, for checking the geometry by eye. Used by --plot.
    std::string topDownPlot(bool roundabout) const;

private:
    // Part of another route that runs along exactly the same line as this one:
    // a shared approach lane, a shared exit lane, or a shared stretch of the
    // roundabout ring. For s in [from, to + tail], the same place on the other
    // route is at s + offset. The tail keeps the two cars aware of each other
    // for a few metres after the routes split.
    struct SharedSpan
    {
        std::size_t other = 0;
        float from = 0.0f;
        float to = 0.0f;
        float tail = 0.0f;
        float offset = 0.0f;
    };

    struct ConflictRef
    {
        std::size_t conflict = 0;
        int side = 0;
    };

    struct RouteInfo
    {
        Route route;
        Lane lane = Lane::Northbound;
        Turn turn = Turn::Straight;
        bool roundabout = false;

        // Where the vehicle centre waits for the signal or for a gap. It always
        // lies before every conflict zone, so a waiting car blocks nobody.
        float stopDistance = 0.0f;

        // Distance at which a roundabout route joins the ring.
        float mergeDistance = 0.0f;

        // End of the last conflict zone: from here on the car has left the box.
        float junctionExit = 0.0f;

        std::vector<SharedSpan> shared;
        std::vector<ConflictRef> conflicts;

        // Highest comfortable speed at each metre of the route, from its curvature.
        std::vector<float> curveSpeed;
    };

    // Two routes whose vehicles would touch somewhere near the middle. The
    // zone is stored as an interval of vehicle-centre distance on each route:
    // while a car's centre is outside its interval, no car on the other route
    // can touch it, wherever that car is. So if the two intervals are never
    // occupied at the same time, the two routes can never collide.
    struct Conflict
    {
        std::array<std::size_t, 2> route {};
        std::array<float, 2> in {};
        std::array<float, 2> out {};
        int prioritySide = -1;   // 0 or 1; -1 = first come, first served
    };

    std::vector<RouteInfo> routes_;
    std::vector<Conflict> conflicts_;
    std::vector<int> claimCounts_;   // two slots per conflict, one per side
    std::vector<Vehicle> vehicles_;
    std::size_t vehicleCount_ = 8;
    unsigned int seed_ = 12345u;

    TrafficPhase phase_ = TrafficPhase::NorthSouthLeftArrow;
    float phaseElapsed_ = 0.0f;

    IntersectionMode mode_ = IntersectionMode::Signals;
    float islandHeight_ = 0.0f;
    float previousIslandHeight_ = 0.0f;
    unsigned int randomState_ = 12345u;

    TrafficStats stats_;

    // --- construction (TrafficBuild in Simulation.cpp)
    void buildRoutes();
    void addRouteFamily(const Route& base, Turn turn, bool roundabout, float lineDistance, float mergeDistance);
    void buildSharedSpans();
    void buildConflicts();
    void finishRoutes();

    // --- placement and spawning
    void placeVehiclesOnApproaches();
    bool trySpawn(Vehicle& vehicle);
    std::size_t pickRoute(Lane lane);
    void placeOnRoute(Vehicle& vehicle, std::size_t routeIndex, float distance, float speed);
    void releaseClaims(Vehicle& vehicle);

    // --- decisions
    struct Leader
    {
        const Vehicle* vehicle = nullptr;
        float gap = 1.0e9f;     // bumper to bumper, metres
        float speed = 0.0f;
    };

    // Scratch space for one simulation step, kept so a step never allocates.
    std::vector<Leader> leaders_;
    std::vector<std::size_t> order_;

    Leader findLeader(std::size_t vehicleIndex) const;
    bool projectOnto(const Vehicle& other, std::size_t routeIndex, float& distanceOnRoute) const;
    bool exitHasRoom(const Vehicle& vehicle) const;
    bool tryCommit(std::size_t vehicleIndex, const Leader& leader);
    // Why the vehicle may not enter the junction yet, or nullptr if it may.
    const char* commitBlocker(std::size_t vehicleIndex, const Leader& leader) const;
    SignalState movementSignal(Lane lane, Turn turn) const;
    bool movementPermitted(const Vehicle& vehicle) const;
    float commandedAcceleration(const Vehicle& vehicle, const Leader& leader) const;
    bool signalDemand(bool northSouth, bool leftTurnsOnly) const;
    bool signalPhaseOver() const;

    // --- stats
    void measureBodies(float dt);
    OrientedBox bodyOf(const Vehicle& vehicle) const;

    const RouteInfo& routeFor(const Vehicle& vehicle) const { return routes_[vehicle.routeIndex]; }
    unsigned int nextRandom();
};
