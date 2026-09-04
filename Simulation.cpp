#include "Simulation.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace
{
    constexpr float minimumCenterGap = 5.4f;
    constexpr float wheelRadius = 0.34f;

    // Normalised logistic easing, scaled so progress 0 maps exactly to 0 and
    // progress 1 maps exactly to 1. Speed changes therefore start and finish
    // smoothly, with no discontinuity in acceleration.
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

    // The project-wide heading convention: forward = (sin yaw, 0, cos yaw).
    glm::vec3 forwardVector(float yawDegrees)
    {
        const float yaw = glm::radians(yawDegrees);
        return {std::sin(yaw), 0.0f, std::cos(yaw)};
    }

    // Rotating a route by +90 degrees maps the northbound approach onto the
    // eastbound one, so this is the lane that rotation index r produces.
    Lane laneForRotation(int rotationIndex)
    {
        switch (rotationIndex)
        {
        case 0: return Lane::Northbound;
        case 1: return Lane::Eastbound;
        case 2: return Lane::Southbound;
        default: return Lane::Westbound;
        }
    }

    float signedAngleDifference(float a, float b)
    {
        float difference = a - b;
        while (difference > 180.0f) difference -= 360.0f;
        while (difference < -180.0f) difference += 360.0f;
        return difference;
    }

    float planarRadius(const glm::vec3& point)
    {
        return std::sqrt(point.x * point.x + point.z * point.z);
    }
}

TrafficSystem::TrafficSystem()
{
    buildRoutes();
    reset();
}

// ---------------------------------------------------------------------------
// Route construction
// ---------------------------------------------------------------------------

void TrafficSystem::buildRoutes()
{
    routes_.clear();

    const float lane = laneOffset;
    const float edge = roadHalfWidth;
    const float rim = spawnRadius;

    // ------------------------- Signalised routes -------------------------
    const float signalStop = rim - stopLineRadius;

    // Straight through.
    {
        Route route;
        route.addLine({-lane, -rim}, {-lane, rim});
        addRouteFamily(route, false, signalStop, 0.0f);
    }

    // Right turn. The radius is fixed by tangency rather than chosen by eye:
    // the arc has to touch the incoming lane centre and the outgoing lane
    // centre exactly at the road edge, which forces edge = lane + radius.
    {
        const float radius = edge - lane;
        Route route;
        route.addLine({-lane, -rim}, {-lane, -edge});
        route.addArc({-lane - radius, -edge}, radius, 0.0f, 90.0f);
        route.addLine({-edge, -lane}, {-rim, -lane});
        addRouteFamily(route, false, signalStop, 0.0f);
    }

    // Left turn: the wide arc that crosses the middle of the intersection.
    // The same tangency argument gives radius = edge + lane.
    {
        const float radius = edge + lane;
        Route route;
        route.addLine({-lane, -rim}, {-lane, -edge});
        route.addArc({edge, -edge}, radius, 180.0f, -90.0f);
        route.addLine({edge, lane}, {rim, lane});
        addRouteFamily(route, false, signalStop, 0.0f);
    }

    // ------------------------- Roundabout routes -------------------------
    // The entry arc curves right while the circulating ring curves left, so the
    // two circles are EXTERNALLY tangent: the gap between their centres is
    // ringRadius + entryRadius. The entry centre must lie one entry radius to
    // the driver's right of the approach lane, which fixes its x; tangency then
    // solves for its z. No magic numbers are needed.
    const float centreX = -(lane + entryRadius);
    const float centreSpan = ringRadius + entryRadius;
    const float centreZ = -std::sqrt(centreSpan * centreSpan - centreX * centreX);
    const glm::vec2 entryCentre {centreX, centreZ};

    // The tangent point lies on the line joining the two centres.
    const glm::vec2 mergePoint = entryCentre * (ringRadius / centreSpan);

    const float mergeAngle = glm::degrees(std::atan2(mergePoint.y, mergePoint.x));
    const float entrySweep = glm::degrees(
        std::atan2(mergePoint.y - centreZ, mergePoint.x - centreX));

    // The exit is the entry mirrored in z, so it leaves the ring heading +z.
    // Every other exit is this one rotated by a multiple of 90 degrees.
    Route exitTail;
    exitTail.addArc({centreX, -centreZ}, entryRadius, -entrySweep, entrySweep);
    exitTail.addLine({-lane, -centreZ}, {-lane, rim});

    const float giveWay = rim + centreZ;   // length of the approach straight
    const float mergeDistance = giveWay + entryRadius * glm::radians(entrySweep);

    for (int exit = 1; exit <= 3; ++exit)
    {
        const float exitRotation = (exit == 1) ? 270.0f : (exit == 2 ? 0.0f : 90.0f);

        // Circulation runs in the direction of DECREASING angle, which is what
        // keeps the island on the driver's left in right-hand traffic.
        float ringSweep = (-mergeAngle - exitRotation) - mergeAngle;
        while (ringSweep > 0.0f)
            ringSweep -= 360.0f;

        Route route;
        route.addLine({-lane, -rim}, {-lane, centreZ});
        route.addArc(entryCentre, entryRadius, 0.0f, entrySweep);
        route.addArc({0.0f, 0.0f}, ringRadius, mergeAngle, ringSweep);
        route.append(exitTail.rotated(exitRotation));

        addRouteFamily(route, true, giveWay, mergeDistance);
    }
}

void TrafficSystem::addRouteFamily(
    const Route& base, bool roundabout, float giveWayDistance, float mergeDistance)
{
    for (int rotation = 0; rotation < 4; ++rotation)
    {
        RouteInfo info;
        info.route = rotation == 0 ? base : base.rotated(90.0f * static_cast<float>(rotation));
        info.lane = laneForRotation(rotation);
        info.roundabout = roundabout;
        info.giveWayDistance = giveWayDistance;
        info.mergeDistance = mergeDistance;
        routes_.push_back(std::move(info));
    }
}

// ---------------------------------------------------------------------------
// Vehicle placement
// ---------------------------------------------------------------------------

void TrafficSystem::reset()
{
    phase_ = TrafficPhase::NorthSouthGreen;
    phaseElapsed_ = 0.0f;
    mode_ = IntersectionMode::Signals;
    islandHeight_ = 0.0f;
    randomState_ = 12345u;
    vehicles_.clear();

    // Six vehicles, matching the "three to six vehicles" of the proposal.
    const std::array<glm::vec3, 6> colors = {
        glm::vec3{0.82f, 0.06f, 0.035f}, glm::vec3{0.07f, 0.30f, 0.88f},
        glm::vec3{0.95f, 0.58f, 0.04f}, glm::vec3{0.13f, 0.62f, 0.34f},
        glm::vec3{0.52f, 0.10f, 0.74f}, glm::vec3{0.86f, 0.86f, 0.89f}
    };

    for (std::size_t index = 0; index < colors.size(); ++index)
    {
        Vehicle vehicle;
        vehicle.id = index;
        vehicle.color = colors[index];
        vehicle.maximumSpeed = 5.4f + static_cast<float>(index % 3) * 0.35f;
        vehicles_.push_back(vehicle);
    }

    placeVehiclesOnApproaches();
}

void TrafficSystem::placeVehiclesOnApproaches()
{
    // Two vehicles queue on each of two approaches and one on each of the
    // others, spaced well beyond the minimum following gap.
    static constexpr Lane order[6] = {
        Lane::Northbound, Lane::Eastbound, Lane::Southbound,
        Lane::Westbound, Lane::Northbound, Lane::Eastbound
    };
    static constexpr float starts[6] = {2.0f, 9.0f, 13.0f, 21.0f, 18.0f, 26.0f};

    for (std::size_t index = 0; index < vehicles_.size(); ++index)
    {
        Vehicle& vehicle = vehicles_[index];
        vehicle.lane = order[index % 6];
        vehicle.routeIndex = pickRoute(vehicle.lane);
        vehicle.distance = starts[index % 6];
        vehicle.currentSpeed = vehicle.maximumSpeed;
        vehicle.targetSpeed = vehicle.maximumSpeed;
        vehicle.transitionStartSpeed = vehicle.maximumSpeed;
        vehicle.transitionElapsed = vehicle.transitionDuration;
        vehicle.wheelAngleDegrees = 0.0f;
        vehicle.steerAngleDegrees = 0.0f;

        const RouteSample sample = routes_[vehicle.routeIndex].route.sample(vehicle.distance);
        vehicle.position = sample.position;
        vehicle.yawDegrees = sample.headingDegrees;
        vehicle.turnSign = sample.turnSign;
    }
}

unsigned int TrafficSystem::nextRandom()
{
    // Small deterministic generator, so every run of the demo is reproducible.
    randomState_ = randomState_ * 1664525u + 1013904223u;
    return randomState_;
}

std::size_t TrafficSystem::pickRoute(Lane lane)
{
    const bool wantRoundabout = mode_ == IntersectionMode::Roundabout;

    std::size_t candidates[8] {};
    std::size_t count = 0;
    for (std::size_t index = 0; index < routes_.size() && count < 8; ++index)
    {
        if (routes_[index].lane == lane && routes_[index].roundabout == wantRoundabout)
            candidates[count++] = index;
    }

    if (count == 0)
        return 0;
    return candidates[nextRandom() % count];
}

Lane TrafficSystem::leastCrowdedApproach() const
{
    static constexpr Lane lanes[4] = {
        Lane::Northbound, Lane::Eastbound, Lane::Southbound, Lane::Westbound
    };

    Lane best = lanes[0];
    float bestClearance = -1.0f;

    for (Lane lane : lanes)
    {
        glm::vec3 entry {0.0f};
        bool found = false;
        for (const RouteInfo& info : routes_)
        {
            if (info.lane == lane && info.roundabout == (mode_ == IntersectionMode::Roundabout))
            {
                entry = info.route.sample(0.0f).position;
                found = true;
                break;
            }
        }
        if (!found)
            continue;

        float clearance = std::numeric_limits<float>::max();
        for (const Vehicle& vehicle : vehicles_)
            clearance = std::min(clearance, glm::length(vehicle.position - entry));

        if (clearance > bestClearance)
        {
            bestClearance = clearance;
            best = lane;
        }
    }
    return best;
}

// ---------------------------------------------------------------------------
// Simulation step
// ---------------------------------------------------------------------------

void TrafficSystem::update(float dt)
{
    dt = glm::clamp(dt, 0.0f, 0.05f);

    // The signal cycle keeps running in both modes so that switching back is
    // seamless; in roundabout mode the vehicles simply ignore it.
    phaseElapsed_ += dt;
    const bool yellowPhase = phase_ == TrafficPhase::NorthSouthYellow ||
                             phase_ == TrafficPhase::EastWestYellow;
    if (phaseElapsed_ >= (yellowPhase ? yellowDuration_ : greenDuration_))
        advancePhase();

    // The island rises out of the road when the roundabout takes over.
    const float targetIslandHeight = mode_ == IntersectionMode::Roundabout ? 1.0f : 0.0f;
    const float islandStep = dt * 1.2f;
    if (islandHeight_ < targetIslandHeight)
        islandHeight_ = std::min(targetIslandHeight, islandHeight_ + islandStep);
    else
        islandHeight_ = std::max(targetIslandHeight, islandHeight_ - islandStep);

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

        float travel = vehicle.currentSpeed * dt;

        const float leaderGap = closestLeaderGap(index);
        if (leaderGap < std::numeric_limits<float>::max())
            travel = std::min(travel, std::max(leaderGap - minimumCenterGap, 0.0f));

        const float lineDistance = distanceToGiveWayLine(vehicle);
        const bool stopAtLine = mustStopAtLine(vehicle, index);
        if (stopAtLine && lineDistance > 0.0f)
            travel = std::min(travel, lineDistance);

        vehicle.distance += travel;
        vehicle.wheelAngleDegrees = std::fmod(
            vehicle.wheelAngleDegrees + glm::degrees(travel / wheelRadius), 360.0f);

        // Reaching the end of a route means leaving the scene: the vehicle
        // re-enters on whichever approach currently has the most room.
        if (vehicle.distance >= routeFor(vehicle).route.totalLength())
        {
            vehicle.lane = leastCrowdedApproach();
            vehicle.routeIndex = pickRoute(vehicle.lane);
            vehicle.distance = 0.0f;
            vehicle.currentSpeed = vehicle.maximumSpeed;
            vehicle.targetSpeed = vehicle.maximumSpeed;
            vehicle.transitionStartSpeed = vehicle.maximumSpeed;
            vehicle.transitionElapsed = vehicle.transitionDuration;
        }

        const RouteSample sample = routeFor(vehicle).route.sample(vehicle.distance);
        vehicle.position = sample.position;
        vehicle.yawDegrees = sample.headingDegrees;
        vehicle.turnSign = sample.turnSign;

        // The front wheels ease towards the steering angle the curve implies.
        // A right turn (positive sweep) decreases yaw, hence the minus sign.
        const float desiredSteer = -sample.turnSign * 24.0f;
        vehicle.steerAngleDegrees +=
            (desiredSteer - vehicle.steerAngleDegrees) * std::min(1.0f, dt * 6.0f);

        if ((stopAtLine && lineDistance > 0.0f && travel >= lineDistance - 0.0001f) ||
            (leaderGap <= minimumCenterGap + 0.001f && travel <= 0.0001f))
        {
            vehicle.currentSpeed = 0.0f;
            vehicle.targetSpeed = 0.0f;
            vehicle.transitionStartSpeed = 0.0f;
            vehicle.transitionElapsed = vehicle.transitionDuration;
        }
    }
}

// ---------------------------------------------------------------------------
// Decisions
// ---------------------------------------------------------------------------

float TrafficSystem::closestLeaderGap(std::size_t vehicleIndex) const
{
    const Vehicle& me = vehicles_[vehicleIndex];
    const glm::vec3 forward = forwardVector(me.yawDegrees);
    float closest = std::numeric_limits<float>::max();

    // One forward scan covers queueing behind a leader, turning cars crossing
    // each other, and circulating cars on the ring.
    for (std::size_t index = 0; index < vehicles_.size(); ++index)
    {
        if (index == vehicleIndex)
            continue;

        const glm::vec3 toOther = vehicles_[index].position - me.position;
        const float along = glm::dot(toOther, forward);
        if (along <= 0.0f)
            continue;

        const float lateral = std::abs(toOther.x * forward.z - toOther.z * forward.x);
        if (lateral > 2.2f)
            continue;

        closest = std::min(closest, along);
    }
    return closest;
}

bool TrafficSystem::ringConflict(std::size_t vehicleIndex) const
{
    if (mode_ != IntersectionMode::Roundabout)
        return false;

    const Vehicle& me = vehicles_[vehicleIndex];
    const RouteInfo& info = routeFor(me);
    if (!info.roundabout || me.distance >= info.giveWayDistance)
        return false;

    const glm::vec3 mergePoint = info.route.sample(info.mergeDistance).position;

    for (std::size_t index = 0; index < vehicles_.size(); ++index)
    {
        if (index == vehicleIndex)
            continue;

        const Vehicle& other = vehicles_[index];
        const RouteInfo& otherInfo = routeFor(other);
        if (!otherInfo.roundabout || other.distance < otherInfo.mergeDistance)
            continue;   // not circulating yet, so it cannot have priority

        const glm::vec3 toMerge = mergePoint - other.position;
        if (glm::length(toMerge) > 11.0f)
            continue;
        if (glm::dot(toMerge, forwardVector(other.yawDegrees)) <= 0.0f)
            continue;   // already past my merge point

        return true;
    }

    // Traffic already on the ring never yields, so this rule cannot deadlock.
    return false;
}

float TrafficSystem::distanceToGiveWayLine(const Vehicle& vehicle) const
{
    return routeFor(vehicle).giveWayDistance - vehicle.distance;
}

bool TrafficSystem::mustStopAtLine(const Vehicle& vehicle, std::size_t vehicleIndex) const
{
    const float lineDistance = distanceToGiveWayLine(vehicle);
    if (lineDistance <= 0.0f)
        return false;   // already committed to the intersection

    if (mode_ == IntersectionMode::Signals)
    {
        const SignalState signal = signalFor(vehicle.lane);
        return signal == SignalState::Red ||
               (signal == SignalState::Yellow && lineDistance > 4.8f);
    }

    return ringConflict(vehicleIndex);
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

    const float lineDistance = distanceToGiveWayLine(vehicle);
    if (mustStopAtLine(vehicle, vehicleIndex) && lineDistance > 0.0f && lineDistance < 18.0f)
    {
        if (lineDistance < 2.4f)
            target = 0.0f;
        else if (lineDistance < 5.5f)
            target = std::min(target, 1.4f);
        else if (lineDistance < 10.0f)
            target = std::min(target, 2.8f);
        else if (lineDistance < 14.0f)
            target = std::min(target, 4.2f);
        else
            target = std::min(target, 5.0f);
    }

    // Corner slowly. Reading the curve a few metres ahead means the car brakes
    // BEFORE the bend rather than in the middle of it.
    const RouteSample ahead = routeFor(vehicle).route.sample(vehicle.distance + 4.0f);
    if (std::abs(ahead.turnSign) > 0.5f || std::abs(vehicle.turnSign) > 0.5f)
        target = std::min(target, vehicle.maximumSpeed * 0.55f);

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

// ---------------------------------------------------------------------------
// Signals and modes
// ---------------------------------------------------------------------------

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

void TrafficSystem::setMode(IntersectionMode mode)
{
    if (mode == mode_)
        return;

    mode_ = mode;

    // Routes differ between the two modes, so traffic is re-formed on the
    // approaches rather than teleported onto a route it was never following.
    placeVehiclesOnApproaches();
}

void TrafficSystem::toggleMode()
{
    setMode(mode_ == IntersectionMode::Signals
                ? IntersectionMode::Roundabout
                : IntersectionMode::Signals);
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
    if (mode_ == IntersectionMode::Roundabout)
        return "GIVE WAY";

    switch (phase_)
    {
    case TrafficPhase::NorthSouthGreen:  return "N/S GREEN";
    case TrafficPhase::NorthSouthYellow: return "N/S YELLOW";
    case TrafficPhase::EastWestGreen:    return "E/W GREEN";
    case TrafficPhase::EastWestYellow:   return "E/W YELLOW";
    }
    return "UNKNOWN";
}

std::string TrafficSystem::modeName() const
{
    return mode_ == IntersectionMode::Roundabout ? "ROUNDABOUT" : "SIGNALS";
}

// ---------------------------------------------------------------------------
// Geometry self-test (no OpenGL context required)
// ---------------------------------------------------------------------------

std::string TrafficSystem::topDownPlot(bool roundabout) const
{
    // A square window of the middle of the scene, one character per metre in x
    // and two metres in z so the aspect looks roughly square in a terminal.
    constexpr int halfWidth = 26;
    constexpr int halfHeight = 13;
    constexpr int columns = halfWidth * 2 + 1;
    constexpr int rows = halfHeight * 2 + 1;

    std::vector<char> grid(static_cast<std::size_t>(columns) * rows, ' ');
    const auto plot = [&grid](int column, int row, char symbol)
    {
        if (column < 0 || column >= columns || row < 0 || row >= rows)
            return;
        char& cell = grid[static_cast<std::size_t>(row) * columns + column];
        if (cell == ' ' || symbol == '#')
            cell = symbol;
    };
    const auto toColumn = [](float x) { return static_cast<int>(std::lround(x)) + halfWidth; };
    const auto toRow = [](float z) { return static_cast<int>(std::lround(z * 0.5f)) + halfHeight; };

    // The island first, so route marks can overwrite nothing and a collision
    // between the two is immediately visible.
    for (int row = 0; row < rows; ++row)
    {
        for (int column = 0; column < columns; ++column)
        {
            const float x = static_cast<float>(column - halfWidth);
            const float z = static_cast<float>(row - halfHeight) * 2.0f;
            if (std::sqrt(x * x + z * z) <= islandRadius)
                plot(column, row, '#');
        }
    }

    for (const RouteInfo& info : routes_)
    {
        if (info.roundabout != roundabout)
            continue;

        const char symbol = info.lane == Lane::Northbound ? 'N'
                          : info.lane == Lane::Eastbound ? 'E'
                          : info.lane == Lane::Southbound ? 'S' : 'W';

        for (float distance = 0.0f; distance <= info.route.totalLength(); distance += 0.25f)
        {
            const glm::vec3 point = info.route.sample(distance).position;
            plot(toColumn(point.x), toRow(point.z), symbol);
        }
    }

    std::string output = roundabout ? "Roundabout routes (# = island)\n"
                                    : "Signalised routes (# = island footprint)\n";
    for (int row = 0; row < rows; ++row)
    {
        output.append(&grid[static_cast<std::size_t>(row) * columns], columns);
        output += "\n";
    }
    return output;
}

bool TrafficSystem::selfTest(std::string& report) const
{
    report.clear();
    bool passed = true;

    const auto fail = [&report, &passed](const std::string& message)
    {
        report += "FAIL: " + message + "\n";
        passed = false;
    };

    if (routes_.size() != 24)
        fail("expected 24 routes (2 modes x 4 approaches x 3 choices), got " +
             std::to_string(routes_.size()));

    // Both endpoints of every route must sit on a lane centre at the scene edge.
    const float expectedEndRadius =
        std::sqrt(spawnRadius * spawnRadius + laneOffset * laneOffset);

    for (std::size_t index = 0; index < routes_.size(); ++index)
    {
        const RouteInfo& info = routes_[index];
        const std::string name = "route " + std::to_string(index);

        if (info.route.totalLength() <= 0.0f)
        {
            fail(name + " has zero length");
            continue;
        }

        const RouteSample start = info.route.sample(0.0f);
        const RouteSample finish = info.route.sample(info.route.totalLength());

        if (std::abs(planarRadius(start.position) - expectedEndRadius) > 0.5f)
            fail(name + " does not start on a lane centre at the scene edge");
        if (std::abs(planarRadius(finish.position) - expectedEndRadius) > 0.5f)
            fail(name + " does not finish on a lane centre at the scene edge");

        // Walking the route in small steps must never jump in position or in
        // heading: that is what proves the segments actually join up.
        constexpr float step = 0.05f;
        RouteSample previous = start;
        float closestToCentre = std::numeric_limits<float>::max();

        for (float distance = step; distance <= info.route.totalLength(); distance += step)
        {
            const RouteSample current = info.route.sample(distance);

            const float moved = glm::length(current.position - previous.position);
            if (moved > step * 1.5f + 0.001f)
            {
                fail(name + " jumps in position at s = " + std::to_string(distance));
                break;
            }

            const float turned = std::abs(
                signedAngleDifference(current.headingDegrees, previous.headingDegrees));
            if (turned > 6.0f)
            {
                fail(name + " jumps in heading at s = " + std::to_string(distance));
                break;
            }

            closestToCentre = std::min(closestToCentre, planarRadius(current.position));
            previous = current;
        }

        // Circulating traffic must stay outside the raised island.
        if (info.roundabout && closestToCentre < islandRadius + 0.8f)
            fail(name + " passes too close to the island (" +
                 std::to_string(closestToCentre) + " m)");
    }

    // Opposing signal phases must never both be green.
    const std::array<TrafficPhase, 4> phases = {
        TrafficPhase::NorthSouthGreen, TrafficPhase::NorthSouthYellow,
        TrafficPhase::EastWestGreen, TrafficPhase::EastWestYellow
    };
    TrafficSystem probe;
    for (TrafficPhase phase : phases)
    {
        probe.phase_ = phase;
        const bool northGreen = probe.signalFor(Lane::Northbound) == SignalState::Green;
        const bool eastGreen = probe.signalFor(Lane::Eastbound) == SignalState::Green;
        if (northGreen && eastGreen)
            fail("north-south and east-west are green at the same time");
    }

    if (passed)
        report = "All route and signal checks passed.\n";
    return passed;
}
