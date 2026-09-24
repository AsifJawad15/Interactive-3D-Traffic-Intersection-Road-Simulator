#pragma once

#include "Collision.h"
#include "RoadNetwork.h"
#include "Route.h"

#include <glm/vec3.hpp>

#include <array>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

// Which way a route leaves its junction. At a roundabout the first exit is
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

// One full cycle of a signalised junction. Each axis gets a protected
// left-turn arrow (skipped when nobody is waiting to turn left), then green
// for everyone on that axis with left turns giving way, then yellow and an
// all-red clearance interval.
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

inline constexpr std::size_t noRoute = std::numeric_limits<std::size_t>::max();

struct Vehicle
{
    std::size_t id = 0;
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
    // A route runs from the middle of one road, through a junction, to the
    // middle of the next; `nextRouteIndex` is where the car goes after that.
    std::size_t routeIndex = 0;
    std::size_t nextRouteIndex = noRoute;
    float distance = 0.0f;
    float turnSign = 0.0f;
    float steerAngleDegrees = 0.0f;

    // False only if the car could not be placed at the start (the city was
    // too full). An inactive car is not simulated or drawn.
    bool active = true;

    // True once the vehicle has been allowed through its junction. It then
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
    std::size_t trips = 0;              // routes completed
    std::vector<double> junctionSeconds;   // vehicle-seconds spent on each junction's routes
};

// Where a lane waits at a junction: painted as a stop line (signals) or a
// dashed give-way line.
struct StopMarking
{
    glm::vec2 position {0.0f};   // centre of the lane, at the line
    glm::vec2 direction {0.0f};  // direction of travel
    bool giveWay = false;
};

class TrafficSystem
{
public:
    explicit TrafficSystem(std::size_t vehicleCount = 24, unsigned int seed = 12345u);

    void update(float dt);
    void reset();
    void advancePhase();   // every signalised junction moves on one phase

    const RoadNetwork& network() const { return network_; }
    const std::vector<Vehicle>& vehicles() const { return vehicles_; }
    SignalState signalFor(std::size_t junction, int arm) const;      // the round lenses
    SignalState leftArrowFor(std::size_t junction, int arm) const;   // the left-turn arrow
    std::string phaseName() const;   // of the central crossroads, for the HUD

    // Render interpolation. `alpha` is how far the clock has run into the next
    // simulation step (0..1): poses are blended from the previous step's to
    // the current one. The output vector is reused, so this never allocates
    // once it has reached its size.
    void interpolatePoses(float alpha, std::vector<VehiclePose>& poses) const;

    const TrafficStats& stats() const { return stats_; }
    void resetStats();

    // Where every lane stops, for painting the lines where the cars really stop.
    std::vector<StopMarking> stopMarkings() const;

    // One line per vehicle (route, position, speed, commit and claims), for
    // diagnosing a failed soak run.
    std::string describe() const;

    // Geometry and rule checks that need no OpenGL context. Used by --self-test.
    bool selfTest(std::string& report) const;

    // A summary of every junction's routes, zones and stop lines (--plot),
    // and a top-down picture of the whole network as a PNG.
    std::string networkReport() const;
    bool writeNetworkImage(const std::string& path) const;

private:
    // Part of another route that runs along exactly the same line as this one:
    // a shared approach lane, a shared exit lane, or a shared stretch of a
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
        std::size_t junction = 0;
        int inArm = 0;
        int inLane = 0;   // 0 = inner, 1 = outer
        int outArm = 0;
        int outLane = 0;
        Turn turn = Turn::Straight;
        int priorityRank = 0;

        // Where the vehicle centre waits for the signal or for a gap. It always
        // lies before every conflict zone, so a waiting car blocks nobody.
        float stopDistance = 0.0f;
        bool needsCommit = true;   // false: nothing to cross (a bend)

        // Distance at which a roundabout route joins the ring.
        float mergeDistance = 0.0f;

        // End of the last conflict zone: from here on the car has left the box.
        float junctionExit = 0.0f;

        std::vector<std::size_t> successors;
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

    struct SignalController
    {
        TrafficPhase phase = TrafficPhase::NorthSouthLeftArrow;
        float elapsed = 0.0f;
    };

    RoadNetwork network_;
    std::vector<RouteInfo> routes_;
    std::vector<Conflict> conflicts_;
    std::vector<int> claimCounts_;   // two slots per conflict, one per side
    std::vector<SignalController> signals_;   // one per junction (unused when unsignalised)
    std::vector<Vehicle> vehicles_;
    std::size_t vehicleCount_ = 24;
    unsigned int seed_ = 12345u;
    unsigned int randomState_ = 12345u;

    TrafficStats stats_;

    // --- construction (TrafficBuild.cpp)
    void buildRoutes();
    void addRoute(std::size_t junction, int inArm, int inLane, int outArm, int outLane,
                  Turn turn, const Route& canonical, float lineDistance, float mergeDistance);
    void buildSuccessors();
    void buildSharedSpans();
    void buildConflicts();
    void finishRoutes();

    // --- placement and routing
    void placeVehiclesInTown();
    std::size_t chooseNextRoute(std::size_t routeIndex);
    void placeOnRoute(Vehicle& vehicle, std::size_t routeIndex, float distance, float speed);
    void enterRoute(Vehicle& vehicle, std::size_t routeIndex);
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
    bool sameStartLane(std::size_t a, std::size_t b) const;
    bool exitHasRoom(const Vehicle& vehicle) const;
    bool tryCommit(std::size_t vehicleIndex, const Leader& leader);
    float slowestClearingTime(const Vehicle& vehicle, float distance) const;
    bool canSlipIn(const Vehicle& vehicle, const Leader& leader, const Conflict& conflict, int mine) const;
    // Why the vehicle may not enter the junction yet, or nullptr if it may.
    // `takeTurns` = false skips the turn-taking rule (used when asking whether
    // the car we would take turns with could go at all).
    const char* commitBlocker(std::size_t vehicleIndex, const Leader& leader, bool takeTurns = true) const;
    SignalState movementSignal(const RouteInfo& route) const;
    // What a movement from `arm` turning `turn` sees during `phase`.
    static SignalState phaseSignal(TrafficPhase phase, int arm, Turn turn);
    bool movementPermitted(const Vehicle& vehicle) const;
    bool decidesEarly(const RouteInfo& route) const;
    float commandedAcceleration(const Vehicle& vehicle, const Leader& leader) const;

    // --- signals
    bool signalDemand(std::size_t junction, bool northSouth, bool leftTurnsOnly) const;
    bool signalPhaseOver(std::size_t junction) const;
    void advancePhase(std::size_t junction);

    // --- stats
    void measureBodies(float dt);
    OrientedBox bodyOf(const Vehicle& vehicle) const;

    const RouteInfo& routeFor(const Vehicle& vehicle) const { return routes_[vehicle.routeIndex]; }
    unsigned int nextRandom();
};
