// The running traffic: signals, the commit-and-claim junction rule, car
// following, spawning and the collision measurements. The one-off geometry
// (routes, shared lanes, conflict zones) and the self-test are in TrafficBuild.cpp.

#include "Simulation.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <numeric>

namespace
{
    constexpr float wheelRadius = 0.34f;
    constexpr float wheelBase = 2.5f;

    // Intelligent Driver Model (Treiber, Hennecke & Helbing, 2000).
    constexpr float maximumAcceleration = 1.6f;   // a
    constexpr float comfortableBraking = 2.2f;    // b
    constexpr float standstillGap = 2.0f;         // s0, bumper to bumper
    constexpr float timeHeadway = 1.1f;           // T, seconds
    constexpr float emergencyBraking = 8.0f;

    // The safety net under the model: a car never gets closer than this to the
    // car in front, whatever the acceleration says.
    constexpr float hardMinimumGap = 0.6f;

    // A car only takes its turn if priority traffic is further away than this.
    constexpr float acceptedGapSeconds = 3.5f;

    constexpr float leaderLookAhead = 80.0f;
    constexpr float spawnClearance = 16.0f;

    // Signal timing. Green and the left arrow are actuated: they end early
    // once their own queue is empty and someone waits on the other road, and
    // they never run past their maximum while anyone is waiting across.
    constexpr float minimumLeftArrowSeconds = 3.0f;
    // Long enough for the two opposing left turns to go one after the other:
    // their arcs cross in the middle, so they cannot go together.
    constexpr float maximumLeftArrowSeconds = 12.0f;
    constexpr float minimumGreenSeconds = 6.0f;
    constexpr float maximumGreenSeconds = 16.0f;
    constexpr float yellowSeconds = 2.5f;
    constexpr float allRedSeconds = 2.5f;
    constexpr float demandRange = 30.0f;   // metres before the line

    // Normalised logistic curve, scaled so 0 maps exactly to 0 and 1 exactly to
    // 1. It shapes how fast the acceleration may change (the jerk), so small
    // corrections are gentle and large ones are quick but never a step.
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

    std::size_t laneIndex(Lane lane)
    {
        return static_cast<std::size_t>(lane);
    }

    // Soonest a car could cover `distance` if it accelerated flat out.
    float earliestArrival(float distance, float speed, float topSpeed)
    {
        if (distance <= 0.0f)
            return 0.0f;
        speed = std::min(speed, topSpeed);
        const float timeToTop = (topSpeed - speed) / maximumAcceleration;
        const float distanceToTop = speed * timeToTop + 0.5f * maximumAcceleration * timeToTop * timeToTop;
        if (distance <= distanceToTop)
            return (-speed + std::sqrt(speed * speed + 2.0f * maximumAcceleration * distance)) / maximumAcceleration;
        return timeToTop + (distance - distanceToTop) / topSpeed;
    }

    // IDM acceleration towards a car (or a stop line) `gap` metres ahead.
    float followingAcceleration(float speed, float desiredSpeed, float gap, float closingSpeed)
    {
        const float desiredGap = standstillGap + std::max(0.0f,
            speed * timeHeadway + speed * closingSpeed / (2.0f * std::sqrt(maximumAcceleration * comfortableBraking)));
        const float freeTerm = std::pow(speed / desiredSpeed, 4.0f);
        const float gapTerm = desiredGap / std::max(gap, 0.1f);
        return maximumAcceleration * (1.0f - freeTerm - gapTerm * gapTerm);
    }

    bool isLeftArrow(TrafficPhase phase)
    {
        return phase == TrafficPhase::NorthSouthLeftArrow || phase == TrafficPhase::EastWestLeftArrow;
    }

    bool isGreen(TrafficPhase phase)
    {
        return phase == TrafficPhase::NorthSouthGreen || phase == TrafficPhase::EastWestGreen;
    }

    bool phaseServesNorthSouth(TrafficPhase phase)
    {
        return static_cast<int>(phase) < static_cast<int>(TrafficPhase::AllRedBeforeEastWest);
    }

    const std::array<glm::vec3, 12> palette = {
        glm::vec3{0.82f, 0.06f, 0.035f}, glm::vec3{0.07f, 0.30f, 0.88f},
        glm::vec3{0.95f, 0.58f, 0.04f}, glm::vec3{0.13f, 0.62f, 0.34f},
        glm::vec3{0.52f, 0.10f, 0.74f}, glm::vec3{0.86f, 0.86f, 0.89f},
        glm::vec3{0.05f, 0.62f, 0.70f}, glm::vec3{0.16f, 0.17f, 0.19f},
        glm::vec3{0.90f, 0.78f, 0.22f}, glm::vec3{0.55f, 0.08f, 0.14f},
        glm::vec3{0.40f, 0.45f, 0.50f}, glm::vec3{0.95f, 0.40f, 0.55f}
    };
}

TrafficSystem::TrafficSystem(std::size_t vehicleCount, unsigned int seed)
    : vehicleCount_(vehicleCount), seed_(seed)
{
    buildRoutes();
    buildSharedSpans();
    buildConflicts();
    finishRoutes();
    reset();
}

// ---------------------------------------------------------------------------
// Placement and spawning
// ---------------------------------------------------------------------------

void TrafficSystem::reset()
{
    phase_ = TrafficPhase::NorthSouthLeftArrow;
    phaseElapsed_ = 0.0f;
    mode_ = IntersectionMode::Signals;
    islandHeight_ = 0.0f;
    previousIslandHeight_ = 0.0f;
    randomState_ = seed_;
    vehicles_.clear();

    for (std::size_t index = 0; index < vehicleCount_; ++index)
    {
        Vehicle vehicle;
        vehicle.id = index;
        vehicle.color = palette[index % palette.size()];
        vehicle.maximumSpeed = 5.4f + static_cast<float>(index % 3) * 0.35f;
        vehicle.claims.reserve(16);
        vehicles_.push_back(vehicle);
    }

    placeVehiclesOnApproaches();
    resetStats();
}

void TrafficSystem::resetStats()
{
    stats_ = TrafficStats {};
    for (Vehicle& vehicle : vehicles_)
        vehicle.stoppedSeconds = 0.0f;
}

void TrafficSystem::placeVehiclesOnApproaches()
{
    // Every claim belongs to the old routes, so all of them go.
    std::fill(claimCounts_.begin(), claimCounts_.end(), 0);

    // Queue the vehicles on the four approaches in turn, one car-length and a
    // good gap apart, all before their stop lines.
    static constexpr Lane order[4] = {
        Lane::Northbound, Lane::Eastbound, Lane::Southbound, Lane::Westbound
    };

    for (std::size_t index = 0; index < vehicles_.size(); ++index)
    {
        Vehicle& vehicle = vehicles_[index];
        vehicle.claims.clear();
        vehicle.committed = false;

        const std::size_t row = index / 4;
        const std::size_t routeIndex = pickRoute(order[index % 4]);
        const float distance = 2.0f + 13.0f * static_cast<float>(row);
        if (distance > routes_[routeIndex].stopDistance - 3.0f)
        {
            vehicle.active = false;   // no room yet: it enters once a gap opens
            continue;
        }

        vehicle.active = true;
        placeOnRoute(vehicle, routeIndex, distance, vehicle.maximumSpeed * 0.6f);
    }
}

void TrafficSystem::placeOnRoute(Vehicle& vehicle, std::size_t routeIndex, float distance, float speed)
{
    vehicle.routeIndex = routeIndex;
    vehicle.lane = routes_[routeIndex].lane;
    vehicle.distance = distance;
    vehicle.currentSpeed = speed;
    vehicle.acceleration = 0.0f;
    vehicle.stoppedSeconds = 0.0f;
    vehicle.steerAngleDegrees = 0.0f;
    vehicle.committed = false;

    const RouteSample sample = routes_[routeIndex].route.sample(distance);
    vehicle.position = sample.position;
    vehicle.yawDegrees = sample.headingDegrees;
    vehicle.turnSign = sample.turnSign;

    // A placed car appears where it is; it must not be blended in from
    // wherever it was before (it would streak across the scene).
    vehicle.previousPosition = vehicle.position;
    vehicle.previousYawDegrees = vehicle.yawDegrees;
    vehicle.previousWheelAngleDegrees = vehicle.wheelAngleDegrees;
    vehicle.previousSteerAngleDegrees = vehicle.steerAngleDegrees;
}

void TrafficSystem::interpolatePoses(float alpha, std::vector<VehiclePose>& poses) const
{
    alpha = glm::clamp(alpha, 0.0f, 1.0f);

    // Angles are blended along the shorter way round, so a heading of 179
    // degrees blends into -179 through 180 rather than back through 0.
    const auto blendAngle = [alpha](float from, float to)
    {
        float difference = std::fmod(to - from + 540.0f, 360.0f) - 180.0f;
        return from + difference * alpha;
    };

    poses.resize(vehicles_.size());
    for (std::size_t index = 0; index < vehicles_.size(); ++index)
    {
        const Vehicle& vehicle = vehicles_[index];
        VehiclePose& pose = poses[index];
        pose.active = vehicle.active;
        pose.color = vehicle.color;
        pose.position = glm::mix(vehicle.previousPosition, vehicle.position, alpha);
        pose.yawDegrees = blendAngle(vehicle.previousYawDegrees, vehicle.yawDegrees);
        pose.wheelAngleDegrees = blendAngle(vehicle.previousWheelAngleDegrees, vehicle.wheelAngleDegrees);
        pose.steerAngleDegrees = glm::mix(vehicle.previousSteerAngleDegrees, vehicle.steerAngleDegrees, alpha);
    }
}

float TrafficSystem::islandHeight(float alpha) const
{
    return glm::mix(previousIslandHeight_, islandHeight_, glm::clamp(alpha, 0.0f, 1.0f));
}

unsigned int TrafficSystem::nextRandom()
{
    // Small deterministic generator, so every run with the same seed is
    // identical and a soak-test failure can be replayed.
    randomState_ = randomState_ * 1664525u + 1013904223u;
    return randomState_ >> 8;
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

bool TrafficSystem::trySpawn(Vehicle& vehicle)
{
    // Re-enter on the approach with the most room, but only if the gap to the
    // last car on it is comfortably larger than a stopping distance.
    static constexpr Lane lanes[4] = {
        Lane::Northbound, Lane::Eastbound, Lane::Southbound, Lane::Westbound
    };
    const bool roundabout = mode_ == IntersectionMode::Roundabout;
    const std::size_t first = nextRandom() % 4;

    float bestClearance = -1.0f;
    Lane bestLane = Lane::Northbound;
    for (std::size_t offset = 0; offset < 4; ++offset)
    {
        const Lane lane = lanes[(first + offset) % 4];
        float clearance = std::numeric_limits<float>::max();
        for (const Vehicle& other : vehicles_)
        {
            if (!other.active || &other == &vehicle)
                continue;
            const RouteInfo& info = routeFor(other);
            if (info.lane != lane || info.roundabout != roundabout)
                continue;
            // Routes from one approach share their first stretch exactly, so
            // distance along any of them is distance along the approach lane.
            clearance = std::min(clearance, other.distance - other.halfLength - vehicle.halfLength);
        }
        if (clearance > bestClearance)
        {
            bestClearance = clearance;
            bestLane = lane;
        }
    }

    if (bestClearance < spawnClearance)
        return false;

    const float speed = vehicle.maximumSpeed *
        glm::clamp((bestClearance - 10.0f) / 15.0f, 0.4f, 1.0f);
    vehicle.active = true;
    vehicle.claims.clear();
    placeOnRoute(vehicle, pickRoute(bestLane), 0.0f, speed);
    return true;
}

void TrafficSystem::releaseClaims(Vehicle& vehicle)
{
    for (std::size_t slot : vehicle.claims)
        --claimCounts_[slot];
    vehicle.claims.clear();
}

// ---------------------------------------------------------------------------
// Simulation step
// ---------------------------------------------------------------------------

void TrafficSystem::update(float dt)
{
    dt = glm::clamp(dt, 0.0f, 0.05f);
    if (dt <= 0.0f)
        return;
    stats_.simulatedSeconds += dt;

    // The signal cycle keeps running in both modes so that switching back is
    // seamless; in roundabout mode the vehicles simply ignore it.
    phaseElapsed_ += dt;
    if (signalPhaseOver())
        advancePhase();

    // Remember where everything was, for render interpolation.
    previousIslandHeight_ = islandHeight_;
    for (Vehicle& vehicle : vehicles_)
    {
        vehicle.previousPosition = vehicle.position;
        vehicle.previousYawDegrees = vehicle.yawDegrees;
        vehicle.previousWheelAngleDegrees = vehicle.wheelAngleDegrees;
        vehicle.previousSteerAngleDegrees = vehicle.steerAngleDegrees;
    }

    // The island rises out of the road when the roundabout takes over.
    const float targetIslandHeight = mode_ == IntersectionMode::Roundabout ? 1.0f : 0.0f;
    const float islandStep = dt * 1.2f;
    if (islandHeight_ < targetIslandHeight)
        islandHeight_ = std::min(targetIslandHeight, islandHeight_ + islandStep);
    else
        islandHeight_ = std::max(targetIslandHeight, islandHeight_ - islandStep);

    // 1. Vehicles waiting off-screen come back when there is room.
    for (Vehicle& vehicle : vehicles_)
    {
        if (!vehicle.active)
            trySpawn(vehicle);
    }

    // 2. Everyone looks at the traffic as it stands at the start of the step.
    //    Leaders only ever move forward, so this view is on the safe side.
    const std::size_t count = vehicles_.size();
    std::vector<Leader>& leaders = leaders_;
    leaders.assign(count, Leader {});
    for (std::size_t index = 0; index < count; ++index)
    {
        if (vehicles_[index].active)
            leaders[index] = findLeader(index);
    }

    // 3. A car let through on green that has not reached its line when the
    //    light changes stops after all if it comfortably can. It is still
    //    outside every zone, so giving its claims back is always safe.
    if (mode_ == IntersectionMode::Signals)
    {
        for (Vehicle& vehicle : vehicles_)
        {
            if (!vehicle.active || !vehicle.committed)
                continue;
            const RouteInfo& info = routeFor(vehicle);
            const float toLine = info.stopDistance - vehicle.distance;
            const float comfortableStop =
                vehicle.currentSpeed * vehicle.currentSpeed / (2.0f * comfortableBraking);
            if (toLine > 0.0f && toLine >= comfortableStop && !movementPermitted(vehicle))
            {
                releaseClaims(vehicle);
                vehicle.committed = false;
            }
        }
    }

    // 4. Junction decisions, nearest to the line first so a queue is served in
    //    order. Each commit takes its claims at once, so the next car in the
    //    loop already sees them: two cars can never commit into one zone.
    std::vector<std::size_t>& order = order_;
    order.resize(count);
    std::iota(order.begin(), order.end(), std::size_t {0});
    std::sort(order.begin(), order.end(), [this](std::size_t a, std::size_t b)
    {
        // Cars already standing at their lines go first, longest wait first;
        // then everyone else by distance to the line.
        const float toLineA = routes_[vehicles_[a].routeIndex].stopDistance - vehicles_[a].distance;
        const float toLineB = routes_[vehicles_[b].routeIndex].stopDistance - vehicles_[b].distance;
        const bool atLineA = toLineA < 0.5f;
        const bool atLineB = toLineB < 0.5f;
        if (atLineA != atLineB)
            return atLineA;
        if (atLineA && vehicles_[a].stoppedSeconds != vehicles_[b].stoppedSeconds)
            return vehicles_[a].stoppedSeconds > vehicles_[b].stoppedSeconds;
        if (toLineA != toLineB)
            return toLineA < toLineB;
        return a < b;
    });
    for (std::size_t index : order)
    {
        const Vehicle& vehicle = vehicles_[index];
        if (vehicle.active && !vehicle.committed)
            tryCommit(index, leaders[index]);
    }

    // 5. Move.
    for (std::size_t index = 0; index < count; ++index)
    {
        Vehicle& vehicle = vehicles_[index];
        if (!vehicle.active)
            continue;

        const Leader& leader = leaders[index];
        const RouteInfo& info = routeFor(vehicle);

        // The acceleration follows the model's command at a limited rate, and
        // the limit itself eases in along the sigmoid. Braking may change
        // faster than speeding up.
        const float command = commandedAcceleration(vehicle, leader);
        const float change = command - vehicle.acceleration;
        const float jerkLimit = (change < 0.0f ? 24.0f : 5.0f) *
                                (0.25f + 0.75f * sigmoidProgress(std::abs(change) / 3.0f));
        vehicle.acceleration += glm::clamp(change, -jerkLimit * dt, jerkLimit * dt);

        float speed = std::max(0.0f, vehicle.currentSpeed + vehicle.acceleration * dt);
        float travel = speed * dt;

        // The safety net: never closer than the hard minimum to the car in
        // front, and never past the stop line without having committed.
        float limit = std::numeric_limits<float>::max();
        if (leader.vehicle != nullptr)
            limit = std::max(0.0f, leader.gap - hardMinimumGap);
        if (!vehicle.committed)
            limit = std::min(limit, std::max(0.0f, info.stopDistance - vehicle.distance));
        if (travel > limit)
        {
            travel = limit;
            speed = std::min(speed, travel / dt);
        }
        if (speed <= 0.0f && vehicle.acceleration < 0.0f)
            vehicle.acceleration = 0.0f;

        vehicle.distance += travel;
        vehicle.currentSpeed = speed;
        vehicle.wheelAngleDegrees = std::fmod(
            vehicle.wheelAngleDegrees + glm::degrees(travel / wheelRadius), 360.0f);

        // Let go of every zone the car has now completely left.
        for (std::size_t claim = 0; claim < vehicle.claims.size();)
        {
            const std::size_t slot = vehicle.claims[claim];
            if (vehicle.distance > conflicts_[slot / 2].out[slot % 2])
            {
                --claimCounts_[slot];
                vehicle.claims[claim] = vehicle.claims.back();
                vehicle.claims.pop_back();
            }
            else
            {
                ++claim;
            }
        }

        // Reaching the end of a route means leaving the scene; the vehicle
        // re-enters on an approach with room, now or on a later step.
        if (vehicle.distance >= info.route.totalLength())
        {
            ++stats_.trips;
            ++stats_.tripsPerApproach[laneIndex(info.lane)];
            releaseClaims(vehicle);
            vehicle.committed = false;
            vehicle.active = false;
            if (!trySpawn(vehicle))
                continue;
        }

        const RouteInfo& current = routeFor(vehicle);
        const RouteSample sample = current.route.sample(vehicle.distance);
        vehicle.position = sample.position;
        vehicle.yawDegrees = sample.headingDegrees;
        vehicle.turnSign = sample.turnSign;

        // The front wheels ease towards the angle the curve needs,
        // atan(wheelbase / radius). A right turn (positive sweep) decreases
        // yaw, hence the minus sign.
        const float desiredSteer = -sample.turnSign *
            std::min(38.0f, glm::degrees(std::atan(wheelBase * sample.curvature)));
        vehicle.steerAngleDegrees +=
            (desiredSteer - vehicle.steerAngleDegrees) * std::min(1.0f, dt * 6.0f);

        if (vehicle.currentSpeed < 0.05f)
        {
            vehicle.stoppedSeconds += dt;
            stats_.longestStop = std::max(stats_.longestStop, vehicle.stoppedSeconds);
        }
        else
        {
            vehicle.stoppedSeconds = 0.0f;
        }
    }

    measureBodies(dt);
}

// ---------------------------------------------------------------------------
// Following
// ---------------------------------------------------------------------------

bool TrafficSystem::projectOnto(const Vehicle& other, std::size_t routeIndex, float& distanceOnRoute) const
{
    if (other.routeIndex == routeIndex)
    {
        distanceOnRoute = other.distance;
        return true;
    }

    for (const SharedSpan& span : routes_[routeIndex].shared)
    {
        if (span.other != other.routeIndex)
            continue;
        const float mapped = other.distance - span.offset;
        if (mapped >= span.from && mapped <= span.to + span.tail)
        {
            distanceOnRoute = mapped;
            return true;
        }
    }
    return false;
}

TrafficSystem::Leader TrafficSystem::findLeader(std::size_t vehicleIndex) const
{
    const Vehicle& me = vehicles_[vehicleIndex];
    const RouteInfo& info = routeFor(me);
    Leader leader;

    for (std::size_t index = 0; index < vehicles_.size(); ++index)
    {
        const Vehicle& other = vehicles_[index];
        if (index == vehicleIndex || !other.active)
            continue;

        float along = 0.0f;
        if (!projectOnto(other, me.routeIndex, along))
            continue;

        // On a shared stretch, the other car only counts while I have not yet
        // turned off it myself.
        if (other.routeIndex != me.routeIndex)
        {
            bool stillShared = false;
            for (const SharedSpan& span : info.shared)
            {
                if (span.other == other.routeIndex && me.distance <= span.to + span.tail &&
                    along >= span.from && along <= span.to + span.tail)
                    stillShared = true;
            }
            if (!stillShared)
                continue;
        }

        const float ahead = along - me.distance;
        if (ahead <= 0.0f || ahead > leaderLookAhead)
            continue;

        const float gap = ahead - me.halfLength - other.halfLength;
        if (gap < leader.gap)
        {
            leader.vehicle = &other;
            leader.gap = gap;
            leader.speed = other.currentSpeed;
        }
    }
    return leader;
}

float TrafficSystem::commandedAcceleration(const Vehicle& vehicle, const Leader& leader) const
{
    const RouteInfo& info = routeFor(vehicle);
    const float speed = vehicle.currentSpeed;
    const auto curveSpeedAt = [&info](float distance)
    {
        const std::size_t metre = static_cast<std::size_t>(std::max(0.0f, distance));
        return info.curveSpeed[std::min(metre, info.curveSpeed.size() - 1)];
    };

    // Free road: the driver's own top speed, or the corner speed here.
    const float desired = std::max(0.5f, std::min(vehicle.maximumSpeed, curveSpeedAt(vehicle.distance) + 0.2f));
    float acceleration = maximumAcceleration * (1.0f - std::pow(speed / desired, 4.0f));

    // Corners ahead: brake early and evenly so the car arrives at each one at
    // its corner speed, v^2 = vc^2 + 2 a d.
    const float lookAhead = std::min(40.0f, speed * speed / 2.0f + 5.0f);
    for (float ahead = 1.0f; ahead <= lookAhead; ahead += 1.0f)
    {
        const float cornerSpeed = curveSpeedAt(vehicle.distance + ahead);
        if (speed > cornerSpeed)
            acceleration = std::min(acceleration, (cornerSpeed * cornerSpeed - speed * speed) / (2.0f * ahead));
    }

    if (leader.vehicle != nullptr)
        acceleration = std::min(acceleration,
            followingAcceleration(speed, desired, leader.gap, speed - leader.speed));

    // Until it has been let through, the stop line acts as a stationary car.
    // The standstill gap is added so the model settles with the car centre
    // exactly on its waiting position.
    if (!vehicle.committed)
    {
        const float toLine = info.stopDistance - vehicle.distance;
        acceleration = std::min(acceleration,
            followingAcceleration(speed, desired, toLine + standstillGap, speed));
    }

    return glm::clamp(acceleration, -emergencyBraking, maximumAcceleration);
}

// ---------------------------------------------------------------------------
// Junction decisions: commit and claim
// ---------------------------------------------------------------------------

SignalState TrafficSystem::movementSignal(Lane lane, Turn turn) const
{
    if (turn == Turn::Left && leftArrowFor(lane) == SignalState::Green)
        return SignalState::Green;
    return signalFor(lane);
}

bool TrafficSystem::movementPermitted(const Vehicle& vehicle) const
{
    if (mode_ == IntersectionMode::Roundabout)
        return true;

    const RouteInfo& info = routeFor(vehicle);
    const SignalState signal = movementSignal(info.lane, info.turn);
    if (signal == SignalState::Green)
        return true;
    if (signal == SignalState::Red)
        return false;

    // Yellow: a left-turner already waiting at the front of its lane clears
    // the junction now, while oncoming traffic stops. Everyone else goes only
    // when stopping would need harder braking than is safe.
    const float toLine = info.stopDistance - vehicle.distance;
    if (info.turn == Turn::Left && toLine < 0.5f && vehicle.currentSpeed < 0.5f)
        return true;
    return vehicle.currentSpeed > 1.0f &&
           toLine < vehicle.currentSpeed * vehicle.currentSpeed / (2.0f * 4.0f);
}

bool TrafficSystem::exitHasRoom(const Vehicle& vehicle) const
{
    // Never enter the box unless there is space to leave it: a car stuck
    // inside would block every crossing route.
    const RouteInfo& info = routeFor(vehicle);
    const float needed = info.junctionExit + 2.0f * vehicle.halfLength + standstillGap;

    for (const Vehicle& other : vehicles_)
    {
        if (!other.active || &other == &vehicle || other.currentSpeed > 1.0f)
            continue;
        float along = 0.0f;
        if (!projectOnto(other, vehicle.routeIndex, along))
            continue;

        // Only a slow car standing in the space just beyond the box counts.
        if (along + other.halfLength > info.junctionExit && along - other.halfLength < needed)
            return false;
    }
    return true;
}

bool TrafficSystem::tryCommit(std::size_t vehicleIndex, const Leader& leader)
{
    if (commitBlocker(vehicleIndex, leader) != nullptr)
        return false;

    // Commit: claim every zone still ahead, in one go.
    Vehicle& vehicle = vehicles_[vehicleIndex];
    vehicle.committed = true;
    for (const ConflictRef& ref : routeFor(vehicle).conflicts)
    {
        if (vehicle.distance > conflicts_[ref.conflict].out[ref.side])
            continue;
        const std::size_t slot = ref.conflict * 2 + static_cast<std::size_t>(ref.side);
        ++claimCounts_[slot];
        vehicle.claims.push_back(slot);
    }
    return true;
}

const char* TrafficSystem::commitBlocker(std::size_t vehicleIndex, const Leader& leader) const
{
    const Vehicle& vehicle = vehicles_[vehicleIndex];
    const RouteInfo& info = routeFor(vehicle);
    const bool roundabout = mode_ == IntersectionMode::Roundabout;

    // Decide early enough on a green light not to brake for nothing, but no
    // earlier: a claim taken far from the line blocks crossing traffic for
    // nothing. At a roundabout, decide close to the line like a real driver.
    const float toLine = info.stopDistance - vehicle.distance;
    const float speed = vehicle.currentSpeed;
    const float decisionDistance = roundabout
        ? std::max(8.0f, speed * speed / 4.0f + 6.0f)
        : std::max(5.0f, speed * 1.5f + speed * speed / (2.0f * comfortableBraking));
    if (toLine > decisionDistance)
        return "not at the line yet";

    if (!movementPermitted(vehicle))
        return "signal";

    // First in, first through: never commit past a car still waiting ahead.
    if (leader.vehicle != nullptr && !leader.vehicle->committed)
        return "queued behind a waiting car";

    if (!exitHasRoom(vehicle))
        return "no room at the exit";

    for (const ConflictRef& ref : info.conflicts)
    {
        const Conflict& conflict = conflicts_[ref.conflict];
        const int mine = ref.side;
        const int theirs = 1 - mine;
        if (vehicle.distance > conflict.out[mine])
            continue;

        // Someone on the crossing route already owns this zone.
        if (claimCounts_[ref.conflict * 2 + static_cast<std::size_t>(theirs)] > 0)
            return "zone claimed by crossing traffic";

        // Give way to crossing traffic that has priority (or that is too close
        // to stop comfortably) and could reach the zone within the accepted gap.
        for (std::size_t index = 0; index < vehicles_.size(); ++index)
        {
            const Vehicle& other = vehicles_[index];
            if (!other.active || other.committed || other.routeIndex != conflict.route[theirs])
                continue;
            if (!movementPermitted(other))
                continue;

            const RouteInfo& otherInfo = routeFor(other);
            const float otherToLine = otherInfo.stopDistance - other.distance;

            // A car queued behind someone still waiting cannot go first.
            const Leader otherLeader = findLeader(index);
            if (otherLeader.vehicle != nullptr && !otherLeader.vehicle->committed)
                continue;

            // Equal priority is served in turn: whoever has been waiting at
            // the line longer goes first, so a steady stream on one side cannot
            // starve the other.
            if (conflict.prioritySide < 0 && otherToLine < 0.5f &&
                other.stoppedSeconds > vehicle.stoppedSeconds + 0.5f)
                return "taking turns with a car that waited longer";

            const bool theyHavePriority = conflict.prioritySide == theirs;
            const bool theyCannotStop = other.currentSpeed > 1.0f &&
                otherToLine < other.currentSpeed * other.currentSpeed / (2.0f * comfortableBraking);
            if (!theyHavePriority && !theyCannotStop)
                continue;

            const float toZone = conflict.in[theirs] - other.distance;
            if (toZone < 0.0f)
                continue;
            if (earliestArrival(toZone, other.currentSpeed, other.maximumSpeed) < acceptedGapSeconds)
                return theyHavePriority ? "giving way to priority traffic" : "giving way to a car that cannot stop";
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Measurements
// ---------------------------------------------------------------------------

OrientedBox TrafficSystem::bodyOf(const Vehicle& vehicle) const
{
    return makeOrientedBox({vehicle.position.x, vehicle.position.z}, vehicle.yawDegrees,
                           {vehicle.halfWidth, vehicle.halfLength});
}

void TrafficSystem::measureBodies(float)
{
    // Every step, every pair of real vehicle outlines is tested. This is the
    // invariant the whole junction design exists for: it must stay at zero.
    std::size_t overlapping = 0;
    for (std::size_t a = 0; a < vehicles_.size(); ++a)
    {
        if (!vehicles_[a].active)
            continue;
        const OrientedBox boxA = bodyOf(vehicles_[a]);
        for (std::size_t b = a + 1; b < vehicles_.size(); ++b)
        {
            if (!vehicles_[b].active)
                continue;
            const glm::vec3 between = vehicles_[a].position - vehicles_[b].position;
            if (glm::dot(between, between) > 100.0f)
                continue;

            const float separation = boxSeparation(boxA, bodyOf(vehicles_[b]));
            stats_.closestBodyGap = std::min(stats_.closestBodyGap, separation);
            if (separation < 0.0f)
                ++overlapping;
        }
    }
    stats_.overlapPairsNow = overlapping;
    if (overlapping > 0)
        ++stats_.overlapSteps;
}

// ---------------------------------------------------------------------------
// Signals and modes
// ---------------------------------------------------------------------------

bool TrafficSystem::signalDemand(bool northSouth, bool leftTurnsOnly) const
{
    // Signal mode only: roundabout routes never wait for a light.
    if (mode_ != IntersectionMode::Signals)
        return false;

    for (std::size_t index = 0; index < vehicles_.size(); ++index)
    {
        const Vehicle& vehicle = vehicles_[index];
        if (!vehicle.active || vehicle.committed)
            continue;
        const RouteInfo& info = routeFor(vehicle);
        if (laneIsNorthSouth(info.lane) != northSouth ||
            info.stopDistance - vehicle.distance >= demandRange)
            continue;
        if (!leftTurnsOnly)
            return true;

        // The arrow is only worth holding for a left-turner that could use
        // it: one at the front of its queue, not stuck behind a straight car.
        if (info.turn != Turn::Left)
            continue;
        const Leader leader = findLeader(index);
        if (leader.vehicle == nullptr || leader.vehicle->committed)
            return true;
    }
    return false;
}

bool TrafficSystem::signalPhaseOver() const
{
    const float elapsed = phaseElapsed_;
    const bool servesNorthSouth = phaseServesNorthSouth(phase_);

    if (isLeftArrow(phase_))
    {
        if (elapsed >= maximumLeftArrowSeconds)
            return true;
        return elapsed >= minimumLeftArrowSeconds && !signalDemand(servesNorthSouth, true);
    }

    if (isGreen(phase_))
    {
        // With nobody waiting across, the green simply rests. In roundabout
        // mode there is never demand, so it falls back to the maximum and the
        // (dark) cycle keeps turning.
        const bool crossDemand = signalDemand(!servesNorthSouth, false);
        if (mode_ != IntersectionMode::Signals)
            return elapsed >= maximumGreenSeconds;
        if (!crossDemand)
            return false;
        if (elapsed >= maximumGreenSeconds)
            return true;
        return elapsed >= minimumGreenSeconds && !signalDemand(servesNorthSouth, false);
    }

    const bool yellow = phase_ == TrafficPhase::NorthSouthYellow || phase_ == TrafficPhase::EastWestYellow;
    return elapsed >= (yellow ? yellowSeconds : allRedSeconds);
}

void TrafficSystem::advancePhase()
{
    auto next = static_cast<TrafficPhase>((static_cast<int>(phase_) + 1) % 8);

    // The left-turn arrow only runs when someone is waiting to turn left.
    if ((next == TrafficPhase::NorthSouthLeftArrow && !signalDemand(true, true)) ||
        (next == TrafficPhase::EastWestLeftArrow && !signalDemand(false, true)))
        next = static_cast<TrafficPhase>(static_cast<int>(next) + 1);

    phase_ = next;
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
    switch (phase_)
    {
    case TrafficPhase::NorthSouthGreen: return northSouth ? SignalState::Green : SignalState::Red;
    case TrafficPhase::NorthSouthYellow: return northSouth ? SignalState::Yellow : SignalState::Red;
    case TrafficPhase::EastWestGreen: return northSouth ? SignalState::Red : SignalState::Green;
    case TrafficPhase::EastWestYellow: return northSouth ? SignalState::Red : SignalState::Yellow;
    default: return SignalState::Red;
    }
}

SignalState TrafficSystem::leftArrowFor(Lane lane) const
{
    const bool northSouth = laneIsNorthSouth(lane);
    if ((northSouth && phase_ == TrafficPhase::NorthSouthLeftArrow) ||
        (!northSouth && phase_ == TrafficPhase::EastWestLeftArrow))
        return SignalState::Green;
    return SignalState::Red;
}

std::string TrafficSystem::phaseName() const
{
    if (mode_ == IntersectionMode::Roundabout)
        return "GIVE WAY";

    switch (phase_)
    {
    case TrafficPhase::NorthSouthLeftArrow: return "N/S LEFT ARROW";
    case TrafficPhase::NorthSouthGreen: return "N/S GREEN";
    case TrafficPhase::NorthSouthYellow: return "N/S YELLOW";
    case TrafficPhase::AllRedBeforeEastWest: return "ALL RED";
    case TrafficPhase::EastWestLeftArrow: return "E/W LEFT ARROW";
    case TrafficPhase::EastWestGreen: return "E/W GREEN";
    case TrafficPhase::EastWestYellow: return "E/W YELLOW";
    case TrafficPhase::AllRedBeforeNorthSouth: return "ALL RED";
    }
    return "UNKNOWN";
}

std::string TrafficSystem::modeName() const
{
    return mode_ == IntersectionMode::Roundabout ? "ROUNDABOUT" : "SIGNALS";
}

std::string TrafficSystem::describe() const
{
    std::string text = "phase " + phaseName() + "\n";
    for (std::size_t index = 0; index < vehicles_.size(); ++index)
    {
        const Vehicle& vehicle = vehicles_[index];
        const RouteInfo& info = routeFor(vehicle);
        const Leader leader = vehicle.active ? findLeader(index) : Leader {};
        const char* blocker = vehicle.active && !vehicle.committed ? commitBlocker(index, leader) : nullptr;
        char line[240];
        std::snprintf(line, sizeof(line),
            "  car %2zu %s route %2zu s %6.2f / stop %5.2f  v %4.2f  %s claims %zu  leader %s gap %.2f  stopped %.0f s  %s\n",
            index, vehicle.active ? "on " : "off", vehicle.routeIndex, vehicle.distance,
            info.stopDistance, vehicle.currentSpeed, vehicle.committed ? "COMMITTED" : "waiting",
            vehicle.claims.size(),
            leader.vehicle != nullptr ? std::to_string(leader.vehicle->id).c_str() : "-",
            leader.vehicle != nullptr ? leader.gap : 0.0f, vehicle.stoppedSeconds,
            blocker != nullptr ? blocker : "");
        text += line;
    }
    return text;
}
