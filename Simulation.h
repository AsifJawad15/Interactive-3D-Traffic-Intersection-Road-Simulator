#pragma once

#include "Collision.h"
#include "RoadNetwork.h"
#include "Route.h"
#include "VehicleTypes.h"

#include <glm/vec2.hpp>
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

// What the walkers tell the traffic about one crossing, every step.
struct CrossingState
{
    int waiting = 0;             // at the kerb (or on the island), wanting to cross
    int onBand = 0;              // on their way over it
    float clearSeconds = 0.0f;   // until the last of those is across
};

// The walkers' lights at a signalised crossing.
enum class WalkLight
{
    None,       // a zebra with no lights
    Walk,       // the green figure: start crossing
    Flashing,   // the red figure flashing: finish crossing, do not start
    DontWalk
};

struct Vehicle
{
    std::size_t id = 0;
    VehicleKind kind = VehicleKind::Sedan;
    SizeClass sizeClass = SizeClass::Car;
    int tier = 0;   // width tier of the size class (VehicleTypes.h)

    glm::vec3 position {0.0f};   // body centre
    glm::vec3 color {0.8f};
    float yawDegrees = 0.0f;
    float currentSpeed = 0.0f;
    float acceleration = 0.0f;
    float wheelAngleDegrees = 0.0f;

    // Real body size seen from above, used for gaps and collisions.
    float halfLength = 2.03f;
    float halfWidth = 0.94f;

    // How far the size class's body reaches along its lane, behind and
    // ahead of `distance`. A junction zone is measured along the lane, so a
    // vehicle is in it while any of that stretch overlaps the zone.
    float rearReach = 2.5f;
    float frontReach = 2.5f;

    // Intelligent Driver Model parameters of this vehicle.
    float maximumSpeed = 8.0f;
    float maximumAcceleration = 1.6f;
    float comfortableBraking = 2.2f;
    float timeHeadway = 1.1f;

    // Route following. `distance` is the only value that is integrated; the
    // position and heading above are read back out of the route every step.
    // A route runs from the middle of one road, through a junction, to the
    // middle of the next; `nextRouteIndex` is where the car goes after that.
    std::size_t routeIndex = 0;
    std::size_t nextRouteIndex = noRoute;
    float distance = 0.0f;
    float steerAngleDegrees = 0.0f;

    // False only if the car could not be placed at the start (the city was
    // too full). An inactive car is not simulated or drawn.
    bool active = true;

    // True once the vehicle has been allowed through its junction. It then
    // holds a claim on every conflict zone on its way until it has left it.
    bool committed = false;
    std::vector<std::size_t> claims;

    float stoppedSeconds = 0.0f;

    // How long it has been standing for people on a crossing, or waiting to
    // cross. After a while it stops giving way to people still at the kerb,
    // and they let it go first, so a busy crossing cannot hold it for ever.
    float pedestrianWaitSeconds = 0.0f;
    bool heldByPedestrians = false;

    // Lights: brake lights, and the indicator (-1 left, +1 right, 0 off).
    bool braking = false;
    int indicator = 0;

    // A line bus: whether it has already stopped at the stop on its current
    // route, how long it still stands there, and how far its doors are open.
    bool lineBus = false;
    bool stopServed = false;
    float dwellSeconds = 0.0f;
    float doorOpen = 0.0f;

    // The pose at the start of the latest simulation step. Rendering blends
    // from this to the current pose, so motion is smooth at any refresh rate
    // even though the simulation itself only moves in 1/60 s steps.
    glm::vec3 previousPosition {0.0f};
    float previousYawDegrees = 0.0f;
    float previousWheelAngleDegrees = 0.0f;
    float previousSteerAngleDegrees = 0.0f;
    float previousDoorOpen = 0.0f;
};

// What the renderer and the cameras need of a vehicle, blended between two
// simulation steps.
struct VehiclePose
{
    std::size_t id = 0;
    VehicleKind kind = VehicleKind::Sedan;
    glm::vec3 position {0.0f};
    glm::vec3 color {0.8f};
    float yawDegrees = 0.0f;
    float wheelAngleDegrees = 0.0f;
    float steerAngleDegrees = 0.0f;
    float doorOpen = 0.0f;
    bool braking = false;
    int indicator = 0;
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
    std::size_t busStopsServed = 0;     // times a line bus opened its doors at a stop
    std::vector<double> junctionSeconds;   // vehicle-seconds spent on each junction's routes
    // Times a vehicle that was still moving faster than 1 m/s had to be held
    // back by the safety net short of an occupied crossing: a stop harder
    // than any driver would make. The rules are meant to keep this at 0.
    std::size_t crossingHardStops = 0;
};

// Where every lane stops, for painting the lines where the cars really stop.
struct StopMarking
{
    glm::vec2 position {0.0f};   // centre of the lane, at the line
    glm::vec2 direction {0.0f};  // direction of travel
    bool giveWay = false;
};

// A stop of the bus line, where the bus stands and where its shelter goes.
struct BusStopSite
{
    glm::vec2 busCentre {0.0f};   // centre of a waiting bus, in the kerb lane
    glm::vec2 shelter {0.0f};     // middle of the shelter, on the sidewalk
    float facingDegrees = 0.0f;   // the shelter's open side looks this way (at the road)
    float headingDegrees = 0.0f;  // the way the buses travel past it
};

class TrafficSystem
{
public:
    explicit TrafficSystem(std::size_t vehicleCount = 36, unsigned int seed = 12345u);

    void update(float dt);
    void reset();

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

    // The stops of the bus line, for the shelters on the sidewalk.
    const std::vector<BusStopSite>& busStopSites() const { return busStopSites_; }

    // One line per vehicle (route, position, speed, commit and claims), for
    // diagnosing a failed soak run.
    std::string describe() const;

    // The player's car and the player on foot. The traffic never drives into
    // them: every car looks along its own path and brakes for them as for a
    // car in front, and does not enter a junction they are standing in.
    void setGuests(const std::vector<Guest>& guests) { guests_ = guests; }

    // Every active AI body as it stands now, for the player's collisions.
    void bodies(std::vector<OrientedBox>& out) const;

    // ---- Pedestrian crossings (plan section 4.6) ------------------------
    // Walkers on a crossing never wait for anything; the traffic keeps out
    // of their way. A vehicle stops short of a crossing someone is on; one
    // about to turn across a crossing where people wait (at a zebra, or at
    // WALK) gives way before it enters the junction; and on the approach to
    // a zebra, drivers who can comfortably stop let waiting people over.
    const std::vector<Crossing>& crossings() const { return network_.crossings(); }
    // From the walkers, once a step.
    void setCrossingStates(const std::vector<CrossingState>& states);
    const std::vector<CrossingState>& crossingStates() const { return crossingStates_; }
    // As far as the lights go, people may start over it now: WALK, or a zebra.
    bool walkAllowed(std::size_t crossing) const;
    WalkLight walkLight(std::size_t crossing) const;
    // And as far as the traffic goes: no vehicle is on it or already turning
    // onto it, and every vehicle heading for it can still stop comfortably
    // before it (or is held at its line until it is clear).
    bool crossingClear(std::size_t crossing) const { return crossingBlocker(crossing) == nullptr; }
    // The vehicle that keeps the crossing from being clear, and why.
    const Vehicle* crossingBlocker(std::size_t crossing, const char** reason = nullptr) const;
    // One line on a crossing: its junction's phase, the walk light, who waits
    // and crosses, and what keeps it from being clear (--trace-crossing).
    std::string crossingReport(std::size_t crossing) const;

    // Geometry and rule checks that need no OpenGL context. Used by --self-test.
    bool selfTest(std::string& report) const;

    // A summary of every junction's routes, zones and stop lines (--plot),
    // and a top-down picture of the whole network as a PNG.
    std::string networkReport() const;
    bool writeNetworkImage(const std::string& path) const;

    // A close-up of the central crossroads with bodies drawn every metre
    // through three turns: the bus's left from the kerb lane, a truck's wide
    // right and a car's right. It shows how the rear axle cuts in and the
    // nose swings out, the longer the vehicle the more.
    bool writeTurnImage(const std::string& path) const;

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

    // A crossing on a route. While the vehicle's centre is between `in` and
    // `out`, its body (with a margin) is over the band (with a margin).
    // `afterLine`: the crossing lies beyond where the vehicle waits to enter
    // the junction, so it is only ever reached after committing.
    struct CrossingRef
    {
        std::size_t crossing = 0;
        float in = 0.0f;
        float out = 0.0f;
        bool afterLine = false;
    };

    // Where a vehicle shows its indicator on a route: from `from` to `to`
    // (route distance of the body centre), towards `direction`.
    struct Indication
    {
        float from = 0.0f;
        float to = 0.0f;
        int direction = 0;
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

        // A turn only one size class may take, or -1 for everyone: the bus
        // line's left turns from the kerb lane, and the trucks' wide right
        // turns into the far lane.
        int onlyClass = -1;

        // Which size classes may drive the route: their bodies stay clear of
        // every kerb and island all the way through it.
        std::array<bool, sizeClassCount> allowed {};

        // Where the front of a waiting vehicle stops: just short of the
        // painted line (or the end of the route, for a bend).
        float lineFront = 0.0f;

        // Distance at which a roundabout route joins the ring.
        float mergeDistance = 0.0f;

        // Heading of each size class's body along the route, every
        // `bodyStep` metres: the front axle follows the lane and the rear
        // axle trails a wheelbase behind it.
        std::array<std::vector<float>, sizeClassCount> bodyYaw;

        // Per size class: where the vehicle centre waits for the signal or
        // for a gap (always before every conflict zone, so a waiting vehicle
        // blocks nobody), and where it has left the last zone behind it.
        std::array<float, sizeClassCount> stopDistance {};
        std::array<float, sizeClassCount> junctionExit {};

        // Per width tier: the conflict zones, and whether there is anything
        // to wait for at all (false for a bend).
        std::array<std::vector<ConflictRef>, widthTierCount> conflicts;
        std::array<bool, widthTierCount> needsCommit {};

        std::vector<std::size_t> successors;
        std::vector<SharedSpan> shared;
        std::vector<Indication> indications;

        // Highest comfortable speed at each metre of the route, from its curvature.
        std::vector<float> curveSpeed;

        // A stop of the bus line on this route: where the bus centre stands
        // (route distance), or below zero for none.
        float busStop = -1.0f;

        // Per size class: every crossing the body passes over, as the
        // interval of route distance in which it would touch the band.
        std::array<std::vector<CrossingRef>, sizeClassCount> crossings;
    };

    // Two routes whose vehicles would touch somewhere near the middle. The
    // zone is an interval of LANE distance on each route: while no part of
    // a vehicle's body lies over its interval, nothing on the other route
    // can touch it, wherever that is. So if the two intervals are never
    // occupied at the same time, the two routes can never collide. The zone
    // holds for one width tier on each side; a vehicle adds its own length
    // (its reach behind and ahead) to find when its body is over it.
    struct Conflict
    {
        std::array<std::size_t, 2> route {};
        std::array<int, 2> tier {};
        std::array<float, 2> in {};
        std::array<float, 2> out {};
        int prioritySide = -1;   // 0 or 1; -1 = first come, first served
    };

    struct SignalController
    {
        TrafficPhase phase = TrafficPhase::NorthSouthLeftArrow;
        float elapsed = 0.0f;
        // The green is due to end but people are still crossing with it: the
        // walkers' lights flash (nobody new may start) and the green holds
        // until they will be over before the crossing traffic gets its turn.
        bool walkClosed = false;
        float heldSeconds = 0.0f;
    };

    // Where a size class's body stands at one route distance.
    struct BodyFrame
    {
        glm::vec3 centre {0.0f};
        float yawDegrees = 0.0f;
        float steerDegrees = 0.0f;   // front wheels, relative to the body
    };

    static constexpr float bodyStep = 0.25f;

    RoadNetwork network_;
    std::vector<RouteInfo> routes_;
    std::vector<Conflict> conflicts_;
    std::vector<int> claimCounts_;   // two slots per conflict, one per side
    std::vector<SignalController> signals_;   // one per junction (unused when unsignalised)
    std::vector<Vehicle> vehicles_;
    std::vector<Guest> guests_;
    std::size_t vehicleCount_ = 36;
    unsigned int seed_ = 12345u;
    unsigned int randomState_ = 12345u;

    // How far each size class's body reaches along the lane, behind and
    // ahead of its route distance, over every route it may drive.
    std::array<float, sizeClassCount> rearReach_ {};
    std::array<float, sizeClassCount> frontReach_ {};

    // The bus line: its routes in order round the loop, and its stops.
    std::vector<std::size_t> busLine_;
    std::vector<BusStopSite> busStopSites_;

    TrafficStats stats_;

    // --- construction (TrafficBuild.cpp)
    void buildRoutes();
    std::size_t addRoute(std::size_t junction, int inArm, int inLane, int outArm, int outLane,
                         Turn turn, const Route& canonical, float lineFront, float mergeDistance,
                         const std::vector<Indication>& indications);
    void buildBusRoutes();
    void buildSuccessors();
    void buildBodyPaths();
    void findAllowedRoutes();
    void buildSharedSpans();
    void buildConflicts();
    void finishRoutes();
    void buildBusLine();
    void buildCrossingRefs();

    // --- placement and routing
    void placeVehiclesInTown();
    std::size_t chooseNextRoute(const Vehicle& vehicle, std::size_t routeIndex);
    void placeOnRoute(Vehicle& vehicle, std::size_t routeIndex, float distance, float speed);
    void enterRoute(Vehicle& vehicle, std::size_t routeIndex);
    void releaseClaims(Vehicle& vehicle);
    BodyFrame bodyFrame(std::size_t routeIndex, SizeClass sizeClass, float distance) const;

    // --- per-vehicle views of the route tables
    const RouteInfo& routeFor(const Vehicle& vehicle) const { return routes_[vehicle.routeIndex]; }
    float stopFor(const Vehicle& vehicle) const
    {
        return routeFor(vehicle).stopDistance[static_cast<std::size_t>(vehicle.sizeClass)];
    }
    float exitFor(const Vehicle& vehicle) const
    {
        return routeFor(vehicle).junctionExit[static_cast<std::size_t>(vehicle.sizeClass)];
    }
    bool needsCommitOn(const Vehicle& vehicle, std::size_t routeIndex) const
    {
        return routes_[routeIndex].needsCommit[static_cast<std::size_t>(vehicle.tier)];
    }
    const std::vector<ConflictRef>& zonesFor(const Vehicle& vehicle) const
    {
        return routeFor(vehicle).conflicts[static_cast<std::size_t>(vehicle.tier)];
    }
    // The route distances between which the vehicle's body is over a zone.
    static float zoneIn(const Vehicle& vehicle, const Conflict& conflict, int side)
    {
        return conflict.in[static_cast<std::size_t>(side)] - vehicle.frontReach;
    }
    static float zoneOut(const Vehicle& vehicle, const Conflict& conflict, int side)
    {
        return conflict.out[static_cast<std::size_t>(side)] + vehicle.rearReach;
    }

    // --- decisions
    struct Leader
    {
        const Vehicle* vehicle = nullptr;
        float gap = 1.0e9f;     // bumper to bumper, metres
        float speed = 0.0f;
    };

    // The nearest guest on a car's path: how far along the path the car's
    // body would first touch it, and how fast it moves along the path.
    struct GuestLimit
    {
        bool present = false;
        float gap = 1.0e9f;
        float speed = 0.0f;
    };
    GuestLimit guestAhead(std::size_t vehicleIndex) const;

    // The nearest crossing the vehicle must stop short of: one somebody is
    // on, or a zebra where somebody waits that it can comfortably stop for.
    // `gap` is how far its centre may still move.
    GuestLimit crossingAhead(std::size_t vehicleIndex) const;
    // Whether people at a crossing beyond the line hold the vehicle there.
    bool pedestriansHold(const Vehicle& vehicle) const;
    std::vector<CrossingState> crossingStates_;

    // Scratch space for one simulation step, kept so a step never allocates.
    std::vector<Leader> leaders_;
    std::vector<GuestLimit> guestLimits_;
    std::vector<GuestLimit> crossingLimits_;
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
    void updateLights(Vehicle& vehicle) const;

    // --- signals
    bool signalDemand(std::size_t junction, bool northSouth, bool leftTurnsOnly) const;
    bool signalPhaseOver(std::size_t junction) const;
    // Somebody waits to walk with this road's green.
    bool walkersWaiting(std::size_t junction, bool northSouth) const;
    // Everyone crossing with the ending green will be over before the
    // crossing traffic's green begins.
    bool walkersClear(std::size_t junction, bool northSouth) const;
    void advancePhase(std::size_t junction);

    // --- stats
    void measureBodies(float dt);
    OrientedBox bodyOf(const Vehicle& vehicle) const;

    unsigned int nextRandom();
};
