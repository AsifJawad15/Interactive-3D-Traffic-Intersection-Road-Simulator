// The running traffic: signals, the commit-and-claim junction rule, car
// following, routing through the city, spawning and the collision
// measurements. The one-off geometry (routes, shared lanes, conflict zones)
// and the self-test are in TrafficBuild.cpp.

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

    // After this long at the line, crossing traffic holds back for a car.
    constexpr float starvationSeconds = 20.0f;

    constexpr float leaderLookAhead = 80.0f;
    constexpr float spawnClearance = 18.0f;

    // Signal timing. Green and the left arrow are actuated: they end early
    // once their own queue is empty and someone waits on the other road, and
    // they never run past their maximum while anyone is waiting across.
    constexpr float minimumLeftArrowSeconds = 3.0f;
    // On the 4-lane crossroads the two opposing left turns pass each other
    // without crossing, so one short arrow serves both sides at once.
    constexpr float maximumLeftArrowSeconds = 7.0f;
    constexpr float minimumGreenSeconds = 6.0f;
    constexpr float maximumGreenSeconds = 16.0f;
    constexpr float yellowSeconds = 3.0f;
    constexpr float allRedSeconds = 2.5f;
    constexpr float demandRange = 35.0f;   // metres before the line

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

    bool armIsNorthSouth(int arm)
    {
        return arm == ArmNorth || arm == ArmSouth;
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

    const char* phaseText(TrafficPhase phase)
    {
        switch (phase)
        {
        case TrafficPhase::NorthSouthLeftArrow: return "N/S LEFT ARROW";
        case TrafficPhase::NorthSouthGreen: return "N/S GREEN";
        case TrafficPhase::NorthSouthYellow: return "N/S YELLOW";
        case TrafficPhase::EastWestLeftArrow: return "E/W LEFT ARROW";
        case TrafficPhase::EastWestGreen: return "E/W GREEN";
        case TrafficPhase::EastWestYellow: return "E/W YELLOW";
        default: return "ALL RED";
        }
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
    : network_(RoadNetwork::makeCity()), vehicleCount_(vehicleCount), seed_(seed)
{
    buildRoutes();
    buildSuccessors();
    buildSharedSpans();
    buildConflicts();
    finishRoutes();
    reset();
}

// ---------------------------------------------------------------------------
// Placement, routing and spawning
// ---------------------------------------------------------------------------

void TrafficSystem::reset()
{
    // Neighbouring signals start on opposite axes, so the city does not
    // change colour all at once.
    signals_.assign(network_.junctionCount(), SignalController {});
    for (std::size_t junction = 0; junction < signals_.size(); ++junction)
    {
        signals_[junction].phase = junction % 2 == 0 ? TrafficPhase::NorthSouthLeftArrow
                                                     : TrafficPhase::EastWestLeftArrow;
        signals_[junction].elapsed = 0.0f;
    }

    randomState_ = seed_;
    std::fill(claimCounts_.begin(), claimCounts_.end(), 0);
    vehicles_.clear();

    for (std::size_t index = 0; index < vehicleCount_; ++index)
    {
        Vehicle vehicle;
        vehicle.id = index;
        vehicle.color = palette[index % palette.size()];
        // 29 to 34 km/h: a city street, not a motorway.
        vehicle.maximumSpeed = 8.0f + static_cast<float>(index % 3) * 0.7f;
        vehicle.claims.reserve(24);
        vehicles_.push_back(vehicle);
    }

    placeVehiclesInTown();
    resetStats();
}

void TrafficSystem::resetStats()
{
    stats_ = TrafficStats {};
    stats_.junctionSeconds.assign(network_.junctionCount(), 0.0);
    for (Vehicle& vehicle : vehicles_)
        vehicle.stoppedSeconds = 0.0f;
}

unsigned int TrafficSystem::nextRandom()
{
    // Small deterministic generator, so every run with the same seed is
    // identical and a soak-test failure can be replayed.
    randomState_ = randomState_ * 1664525u + 1013904223u;
    return randomState_ >> 8;
}

void TrafficSystem::placeVehiclesInTown()
{
    // Scatter the vehicles over the whole network, each one well clear of
    // the others and short of its junction, so nobody starts inside a box.
    for (Vehicle& vehicle : vehicles_)
    {
        vehicle.active = false;
        vehicle.claims.clear();
        vehicle.committed = false;
    }

    for (Vehicle& vehicle : vehicles_)
    {
        for (int attempt = 0; attempt < 400; ++attempt)
        {
            const std::size_t routeIndex = nextRandom() % routes_.size();
            const RouteInfo& info = routes_[routeIndex];
            const float lowest = 3.0f;
            const float highest = info.needsCommit ? info.stopDistance - 6.0f : info.route.totalLength() - 3.0f;
            if (highest <= lowest)
                continue;
            const float fraction = static_cast<float>(nextRandom() % 10000) / 10000.0f;
            const float distance = lowest + fraction * (highest - lowest);
            const glm::vec3 position = info.route.sample(distance).position;

            bool clear = true;
            for (const Vehicle& other : vehicles_)
            {
                if (other.active && glm::length(other.position - position) < 15.0f)
                {
                    clear = false;
                    break;
                }
            }
            if (!clear)
                continue;

            vehicle.active = true;
            placeOnRoute(vehicle, routeIndex, distance, vehicle.maximumSpeed * 0.5f);
            break;
        }
    }
}

void TrafficSystem::enterRoute(Vehicle& vehicle, std::size_t routeIndex)
{
    releaseClaims(vehicle);
    vehicle.routeIndex = routeIndex;
    const RouteInfo& info = routes_[routeIndex];
    // A route with nothing to cross (a bend) is driven straight through.
    vehicle.committed = !info.needsCommit;
    vehicle.nextRouteIndex = info.leavesTown ? noRoute : chooseNextRoute(routeIndex);
}

void TrafficSystem::placeOnRoute(Vehicle& vehicle, std::size_t routeIndex, float distance, float speed)
{
    enterRoute(vehicle, routeIndex);
    vehicle.distance = distance;
    vehicle.currentSpeed = speed;
    vehicle.acceleration = 0.0f;
    vehicle.stoppedSeconds = 0.0f;
    vehicle.steerAngleDegrees = 0.0f;

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

std::size_t TrafficSystem::chooseNextRoute(std::size_t routeIndex)
{
    const std::vector<std::size_t>& candidates = routes_[routeIndex].successors;
    if (candidates.empty())
        return noRoute;

    // A random choice, weighted away from exits that many cars are already
    // heading for, so no single part of the city clogs up.
    std::array<float, 8> weights {};
    float total = 0.0f;
    for (std::size_t option = 0; option < candidates.size() && option < weights.size(); ++option)
    {
        const RouteInfo& candidate = routes_[candidates[option]];
        int load = 0;
        for (const Vehicle& other : vehicles_)
        {
            if (!other.active)
                continue;
            const RouteInfo& current = routes_[other.routeIndex];
            if (current.junction == candidate.junction && current.outArm == candidate.outArm)
                ++load;
            if (other.nextRouteIndex != noRoute)
            {
                const RouteInfo& next = routes_[other.nextRouteIndex];
                if (next.junction == candidate.junction && next.outArm == candidate.outArm)
                    ++load;
            }
        }
        weights[option] = 1.0f / (1.0f + 0.6f * static_cast<float>(load));
        total += weights[option];
    }

    float pick = static_cast<float>(nextRandom() % 10000) / 10000.0f * total;
    for (std::size_t option = 0; option < candidates.size() && option < weights.size(); ++option)
    {
        pick -= weights[option];
        if (pick <= 0.0f)
            return candidates[option];
    }
    return candidates.back();
}

bool TrafficSystem::trySpawn(Vehicle& vehicle)
{
    // Through-traffic comes into town on one of the eight lanes of the four
    // roads in, choosing the lane with the most room - but only if the gap to
    // the last car on it is comfortably larger than a stopping distance.
    const std::size_t count = townEntryRoutes_.size();
    if (count == 0)
        return false;
    const std::size_t first = nextRandom() % count;

    float bestClearance = -1.0f;
    std::size_t bestRoute = townEntryRoutes_[first];
    for (std::size_t offset = 0; offset < count; ++offset)
    {
        const std::size_t candidate = townEntryRoutes_[(first + offset) % count];
        float clearance = std::numeric_limits<float>::max();
        for (const Vehicle& other : vehicles_)
        {
            float along = 0.0f;
            if (!other.active || &other == &vehicle || !projectOnto(other, candidate, along))
                continue;
            clearance = std::min(clearance, along - other.halfLength - vehicle.halfLength);
        }
        if (clearance > bestClearance)
        {
            bestClearance = clearance;
            bestRoute = candidate;
        }
    }

    if (bestClearance < spawnClearance)
        return false;

    // Any route from that lane; they all share it up to the junction.
    std::size_t options[4] {};
    std::size_t optionCount = 0;
    for (std::size_t candidate : townEntryRoutes_)
    {
        if (sameStartLane(candidate, bestRoute) && optionCount < 4)
            options[optionCount++] = candidate;
    }

    const float speed = vehicle.maximumSpeed * glm::clamp((bestClearance - 10.0f) / 20.0f, 0.4f, 1.0f);
    vehicle.active = true;
    placeOnRoute(vehicle, options[nextRandom() % optionCount], 0.0f, speed);
    return true;
}

void TrafficSystem::releaseClaims(Vehicle& vehicle)
{
    for (std::size_t slot : vehicle.claims)
        --claimCounts_[slot];
    vehicle.claims.clear();
}

void TrafficSystem::interpolatePoses(float alpha, std::vector<VehiclePose>& poses) const
{
    alpha = glm::clamp(alpha, 0.0f, 1.0f);

    // Angles are blended along the shorter way round, so a heading of 179
    // degrees blends into -179 through 180 rather than back through 0.
    const auto blendAngle = [alpha](float from, float to)
    {
        const float difference = std::fmod(to - from + 540.0f, 360.0f) - 180.0f;
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

// ---------------------------------------------------------------------------
// Simulation step
// ---------------------------------------------------------------------------

void TrafficSystem::update(float dt)
{
    dt = glm::clamp(dt, 0.0f, 0.05f);
    if (dt <= 0.0f)
        return;
    stats_.simulatedSeconds += dt;

    for (std::size_t junction = 0; junction < signals_.size(); ++junction)
    {
        if (!network_.junctions()[junction].isSignalised())
            continue;
        signals_[junction].elapsed += dt;
        if (signalPhaseOver(junction))
            advancePhase(junction);
    }

    // Remember where everything was, for render interpolation.
    for (Vehicle& vehicle : vehicles_)
    {
        vehicle.previousPosition = vehicle.position;
        vehicle.previousYawDegrees = vehicle.yawDegrees;
        vehicle.previousWheelAngleDegrees = vehicle.wheelAngleDegrees;
        vehicle.previousSteerAngleDegrees = vehicle.steerAngleDegrees;
    }

    // 1. Vehicles waiting out of town come back when a road in has room.
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
    for (Vehicle& vehicle : vehicles_)
    {
        if (!vehicle.active || !vehicle.committed)
            continue;
        const RouteInfo& info = routeFor(vehicle);
        if (!network_.junctions()[info.junction].isSignalised())
            continue;
        const float toLine = info.stopDistance - vehicle.distance;
        const float comfortableStop =
            vehicle.currentSpeed * vehicle.currentSpeed / (2.0f * comfortableBraking);
        if (toLine > 0.0f && toLine >= comfortableStop && !movementPermitted(vehicle))
        {
            releaseClaims(vehicle);
            vehicle.committed = false;
        }
    }

    // 4. Junction decisions, cars standing at their lines first (longest wait
    //    first), then by distance to the line. Each commit takes its claims at
    //    once, so the next car already sees them: two cars can never commit
    //    into one zone.
    std::vector<std::size_t>& order = order_;
    order.resize(count);
    std::iota(order.begin(), order.end(), std::size_t {0});
    std::sort(order.begin(), order.end(), [this](std::size_t a, std::size_t b)
    {
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

        // The end of a route is the middle of the next road: carry on into
        // the next junction's route, or leave town and come back later on
        // one of the roads in.
        const float length = info.route.totalLength();
        if (vehicle.distance >= length)
        {
            ++stats_.trips;
            if (vehicle.nextRouteIndex == noRoute)
            {
                ++stats_.leftTown;
                releaseClaims(vehicle);
                vehicle.committed = false;
                vehicle.active = false;
                if (!trySpawn(vehicle))
                    continue;
            }
            else
            {
                const float overshoot = vehicle.distance - length;
                enterRoute(vehicle, vehicle.nextRouteIndex);
                vehicle.distance = overshoot;
            }
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

        stats_.junctionSeconds[current.junction] += dt;
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
    const float remaining = info.route.totalLength() - me.distance;
    Leader leader;

    for (std::size_t index = 0; index < vehicles_.size(); ++index)
    {
        const Vehicle& other = vehicles_[index];
        if (index == vehicleIndex || !other.active)
            continue;

        float ahead = -1.0f;
        float along = 0.0f;
        if (projectOnto(other, me.routeIndex, along))
        {
            // On a shared stretch, the other car only counts while I have not
            // yet turned off it myself.
            bool counts = other.routeIndex == me.routeIndex;
            if (!counts)
            {
                for (const SharedSpan& span : info.shared)
                {
                    if (span.other == other.routeIndex && me.distance <= span.to + span.tail &&
                        along >= span.from && along <= span.to + span.tail)
                        counts = true;
                }
            }
            if (counts)
                ahead = along - me.distance;
        }
        else if (me.nextRouteIndex != noRoute && remaining < leaderLookAhead &&
                 projectOnto(other, me.nextRouteIndex, along))
        {
            // Already past the middle of the next road, on the route I will
            // take (or one sharing its first lane).
            ahead = remaining + along;
        }

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
    const float lookAhead = std::min(50.0f, speed * speed / 2.0f + 5.0f);
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

SignalState TrafficSystem::phaseSignal(TrafficPhase phase, int arm, Turn turn)
{
    const bool northSouth = armIsNorthSouth(arm);
    if (turn == Turn::Left && ((northSouth && phase == TrafficPhase::NorthSouthLeftArrow) ||
                               (!northSouth && phase == TrafficPhase::EastWestLeftArrow)))
        return SignalState::Green;

    switch (phase)
    {
    case TrafficPhase::NorthSouthGreen: return northSouth ? SignalState::Green : SignalState::Red;
    case TrafficPhase::NorthSouthYellow: return northSouth ? SignalState::Yellow : SignalState::Red;
    case TrafficPhase::EastWestGreen: return northSouth ? SignalState::Red : SignalState::Green;
    case TrafficPhase::EastWestYellow: return northSouth ? SignalState::Red : SignalState::Yellow;
    default: return SignalState::Red;
    }
}

SignalState TrafficSystem::movementSignal(const RouteInfo& route) const
{
    if (!network_.junctions()[route.junction].isSignalised())
        return SignalState::Green;
    return phaseSignal(signals_[route.junction].phase, route.inArm, route.turn);
}

bool TrafficSystem::movementPermitted(const Vehicle& vehicle) const
{
    const RouteInfo& info = routeFor(vehicle);
    const SignalState signal = movementSignal(info);
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

bool TrafficSystem::decidesEarly(const RouteInfo& route) const
{
    // Signal-controlled traffic and the road with priority decide early
    // enough not to brake for nothing. Traffic that has to give way decides
    // close to the line, like a real driver looking for a gap.
    const Junction& junction = network_.junctions()[route.junction];
    return junction.isSignalised() ||
           (junction.type == JunctionType::GiveWayT && junction.majorArm[static_cast<std::size_t>(route.inArm)]);
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

float TrafficSystem::slowestClearingTime(const Vehicle& vehicle, float distance) const
{
    // A deliberately pessimistic drive: half a second before the car even
    // starts to speed up (the jerk limit), then only 1 m/s^2, never faster
    // than the corners allow. The real car can only do better.
    const RouteInfo& info = routeFor(vehicle);
    constexpr float step = 0.05f;
    float speed = vehicle.currentSpeed;
    float covered = 0.0f;
    float time = 0.0f;
    while (covered < distance)
    {
        if (time > 30.0f)
            return std::numeric_limits<float>::max();
        const float along = vehicle.distance + covered;
        const std::size_t metre = std::min(static_cast<std::size_t>(std::max(0.0f, along)), info.curveSpeed.size() - 1);
        const float limit = std::min(vehicle.maximumSpeed, info.curveSpeed[metre]);
        if (time >= 0.5f)
            speed = std::min(speed + 1.0f * step, limit);
        speed = std::min(speed, limit);
        covered += std::max(speed, 0.0f) * step;
        time += step;
    }
    return time;
}

bool TrafficSystem::canSlipIn(const Vehicle& vehicle, const Leader& leader,
                              const Conflict& conflict, int mine) const
{
    const int theirs = 1 - mine;
    const float clearDistance = conflict.out[static_cast<std::size_t>(mine)] - vehicle.distance;

    // Nothing ahead may hold the car up on its way through the zone.
    if (leader.vehicle != nullptr && leader.gap < clearDistance + 12.0f)
        return false;

    const float clearingTime = slowestClearingTime(vehicle, clearDistance) + 1.5f;

    // Every car holding the other side must be further off than that, even
    // if it accelerated flat out from now on.
    const std::size_t theirSlot = static_cast<std::size_t>(&conflict - conflicts_.data()) * 2 +
                                  static_cast<std::size_t>(theirs);
    for (const Vehicle& other : vehicles_)
    {
        if (!other.active || std::find(other.claims.begin(), other.claims.end(), theirSlot) == other.claims.end())
            continue;
        const float toZone = conflict.in[static_cast<std::size_t>(theirs)] - other.distance;
        if (toZone <= 0.0f)
            return false;   // already in it
        if (earliestArrival(toZone, other.currentSpeed, other.maximumSpeed) < clearingTime)
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

const char* TrafficSystem::commitBlocker(std::size_t vehicleIndex, const Leader& leader, bool takeTurns) const
{
    const Vehicle& vehicle = vehicles_[vehicleIndex];
    const RouteInfo& info = routeFor(vehicle);
    if (!info.needsCommit)
        return nullptr;

    // Decide early enough on green not to brake for nothing, but no earlier:
    // a claim taken far from the line blocks crossing traffic for nothing.
    const float toLine = info.stopDistance - vehicle.distance;
    const float speed = vehicle.currentSpeed;
    const float decisionDistance = decidesEarly(info)
        ? std::max(5.0f, speed * 1.5f + speed * speed / (2.0f * comfortableBraking))
        : std::max(6.0f, speed * speed / (2.0f * comfortableBraking) + 2.0f);
    if (toLine > decisionDistance)
        return "not at the line yet";

    if (!movementPermitted(vehicle))
        return "signal";

    // First in, first through: never commit past a car still waiting ahead
    // for this same junction. (A car queuing at the next junction is the
    // exit-room check's business, not this one's.)
    if (leader.vehicle != nullptr && !leader.vehicle->committed &&
        routeFor(*leader.vehicle).junction == info.junction)
        return "queued behind a waiting car";

    if (!exitHasRoom(vehicle))
        return "no room at the exit";

    for (const ConflictRef& ref : info.conflicts)
    {
        const Conflict& conflict = conflicts_[ref.conflict];
        const int mine = ref.side;
        const int theirs = 1 - mine;
        if (vehicle.distance > conflict.out[static_cast<std::size_t>(mine)])
            continue;

        // Someone on the crossing route already owns this zone. The car may
        // still slip in ahead of them, but only if it is certain to be out of
        // the zone before they could possibly get there.
        if (claimCounts_[ref.conflict * 2 + static_cast<std::size_t>(theirs)] > 0 &&
            !canSlipIn(vehicle, leader, conflict, mine))
            return "zone claimed by crossing traffic";

        // Give way to crossing traffic that has priority (or that is too close
        // to stop comfortably) and could reach the zone within the accepted gap.
        for (std::size_t index = 0; index < vehicles_.size(); ++index)
        {
            const Vehicle& other = vehicles_[index];
            if (!other.active || other.committed || other.routeIndex != conflict.route[static_cast<std::size_t>(theirs)])
                continue;
            if (!movementPermitted(other))
                continue;

            const RouteInfo& otherInfo = routeFor(other);
            const float otherToLine = otherInfo.stopDistance - other.distance;
            const bool otherWaitingAtLine = otherToLine < 0.5f && other.currentSpeed < 0.5f;

            // Starvation guard: a car that has stood at its line for a long
            // time goes before anyone else on a crossing route, so a steady
            // stream of claims from one direction cannot lock it out forever.
            // (A car at its own line is at the front, whatever lies ahead.)
            if (takeTurns && otherWaitingAtLine &&
                other.stoppedSeconds > std::max(starvationSeconds, vehicle.stoppedSeconds + 5.0f))
                return "letting a long-waiting car go first";

            // A car queued behind someone still waiting cannot go first.
            const Leader otherLeader = findLeader(index);
            if (otherLeader.vehicle != nullptr && !otherLeader.vehicle->committed &&
                routeFor(*otherLeader.vehicle).junction == otherInfo.junction)
                continue;

            // Equal priority is served in turn: whoever has been waiting at
            // the line longer goes first, so a steady stream on one side cannot
            // starve the other. But only if that car could actually go now -
            // giving way to someone who is stuck themselves helps nobody.
            if (takeTurns && conflict.prioritySide < 0 && otherToLine < 0.5f &&
                other.stoppedSeconds > vehicle.stoppedSeconds + 0.5f &&
                commitBlocker(index, otherLeader, false) == nullptr)
                return "taking turns with a car that waited longer";

            // ...and, the other way round, a car waiting much longer does not
            // hold back for a car standing at its own line: that car will see
            // this one's claims and wait.
            if (otherWaitingAtLine && vehicle.stoppedSeconds > other.stoppedSeconds + 5.0f)
                continue;

            const bool theyHavePriority = conflict.prioritySide == theirs;
            const bool theyCannotStop = other.currentSpeed > 1.0f &&
                otherToLine < other.currentSpeed * other.currentSpeed / (2.0f * comfortableBraking);
            if (!theyHavePriority && !theyCannotStop)
                continue;

            const float toZone = conflict.in[static_cast<std::size_t>(theirs)] - other.distance;
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
// Signals
// ---------------------------------------------------------------------------

bool TrafficSystem::signalDemand(std::size_t junction, bool northSouth, bool leftTurnsOnly) const
{
    for (std::size_t index = 0; index < vehicles_.size(); ++index)
    {
        const Vehicle& vehicle = vehicles_[index];
        if (!vehicle.active || vehicle.committed)
            continue;
        const RouteInfo& info = routeFor(vehicle);
        if (info.junction != junction || armIsNorthSouth(info.inArm) != northSouth ||
            info.stopDistance - vehicle.distance >= demandRange)
            continue;
        if (!leftTurnsOnly)
            return true;

        // The arrow is only worth holding for a left-turner that could use
        // it: one at the front of its queue, not stuck behind a straight car.
        if (info.turn != Turn::Left)
            continue;
        const Leader leader = findLeader(index);
        if (leader.vehicle == nullptr || leader.vehicle->committed ||
            routeFor(*leader.vehicle).junction != junction)
            return true;
    }
    return false;
}

bool TrafficSystem::signalPhaseOver(std::size_t junction) const
{
    const SignalController& controller = signals_[junction];
    const float elapsed = controller.elapsed;
    const bool servesNorthSouth = phaseServesNorthSouth(controller.phase);

    if (isLeftArrow(controller.phase))
    {
        if (elapsed >= maximumLeftArrowSeconds)
            return true;
        return elapsed >= minimumLeftArrowSeconds && !signalDemand(junction, servesNorthSouth, true);
    }

    if (isGreen(controller.phase))
    {
        // With nobody waiting across, the green simply rests.
        if (!signalDemand(junction, !servesNorthSouth, false))
            return false;
        if (elapsed >= maximumGreenSeconds)
            return true;
        return elapsed >= minimumGreenSeconds && !signalDemand(junction, servesNorthSouth, false);
    }

    const bool yellow = controller.phase == TrafficPhase::NorthSouthYellow ||
                        controller.phase == TrafficPhase::EastWestYellow;
    return elapsed >= (yellow ? yellowSeconds : allRedSeconds);
}

void TrafficSystem::advancePhase(std::size_t junction)
{
    SignalController& controller = signals_[junction];
    auto next = static_cast<TrafficPhase>((static_cast<int>(controller.phase) + 1) % 8);

    // The left-turn arrow only runs when someone is waiting to turn left.
    if ((next == TrafficPhase::NorthSouthLeftArrow && !signalDemand(junction, true, true)) ||
        (next == TrafficPhase::EastWestLeftArrow && !signalDemand(junction, false, true)))
        next = static_cast<TrafficPhase>(static_cast<int>(next) + 1);

    controller.phase = next;
    controller.elapsed = 0.0f;
}

void TrafficSystem::advancePhase()
{
    for (std::size_t junction = 0; junction < signals_.size(); ++junction)
    {
        if (network_.junctions()[junction].isSignalised())
            advancePhase(junction);
    }
}

SignalState TrafficSystem::signalFor(std::size_t junction, int arm) const
{
    return phaseSignal(signals_[junction].phase, arm, Turn::Straight);
}

SignalState TrafficSystem::leftArrowFor(std::size_t junction, int arm) const
{
    const TrafficPhase phase = signals_[junction].phase;
    return isLeftArrow(phase) && phaseServesNorthSouth(phase) == armIsNorthSouth(arm)
        ? SignalState::Green : SignalState::Red;
}

std::string TrafficSystem::phaseName() const
{
    const std::size_t centre = network_.centralJunction();
    return network_.junctions()[centre].name + " " + phaseText(signals_[centre].phase);
}

std::string TrafficSystem::describe() const
{
    std::string text = "signals:";
    for (std::size_t junction = 0; junction < signals_.size(); ++junction)
    {
        if (network_.junctions()[junction].isSignalised())
            text += " " + network_.junctions()[junction].name + " " + phaseText(signals_[junction].phase);
    }
    text += "\n";

    for (std::size_t index = 0; index < vehicles_.size(); ++index)
    {
        const Vehicle& vehicle = vehicles_[index];
        const RouteInfo& info = routeFor(vehicle);
        const Leader leader = vehicle.active ? findLeader(index) : Leader {};
        const char* blocker = vehicle.active && !vehicle.committed ? commitBlocker(index, leader) : nullptr;
        char line[300];
        std::snprintf(line, sizeof(line),
            "  car %2zu %s %-7s %s%s->%s%s route %3zu s %6.2f / stop %6.2f  v %4.2f a %5.2f cmd %5.2f  %s claims %zu  leader %s gap %.2f  stopped %.0f s  %s\n",
            index, vehicle.active ? "on " : "off", network_.junctions()[info.junction].name.c_str(),
            armName(info.inArm), info.inLane == 0 ? "i" : "o", armName(info.outArm), info.outLane == 0 ? "i" : "o",
            vehicle.routeIndex, vehicle.distance, info.stopDistance, vehicle.currentSpeed,
            vehicle.acceleration, vehicle.active ? commandedAcceleration(vehicle, leader) : 0.0f,
            vehicle.committed ? "COMMITTED" : "waiting", vehicle.claims.size(),
            leader.vehicle != nullptr ? std::to_string(leader.vehicle->id).c_str() : "-",
            leader.vehicle != nullptr ? leader.gap : 0.0f, vehicle.stoppedSeconds,
            blocker != nullptr ? blocker : "");
        text += line;

        // For a car locked out by claims, who holds them.
        if (blocker != nullptr && std::string(blocker) == "zone claimed by crossing traffic")
        {
            text += "        held by:";
            for (const ConflictRef& ref : info.conflicts)
            {
                const std::size_t theirSlot = ref.conflict * 2 + static_cast<std::size_t>(1 - ref.side);
                if (claimCounts_[theirSlot] == 0 || vehicle.distance > conflicts_[ref.conflict].out[static_cast<std::size_t>(ref.side)])
                    continue;
                for (const Vehicle& other : vehicles_)
                {
                    if (std::find(other.claims.begin(), other.claims.end(), theirSlot) != other.claims.end())
                        text += " car " + std::to_string(other.id) + " (zone " + std::to_string(ref.conflict) +
                                ", " + std::to_string(static_cast<int>(conflicts_[ref.conflict].in[static_cast<std::size_t>(1 - ref.side)] - other.distance)) + " m off)";
                }
            }
            text += "\n";
        }
    }
    return text;
}
