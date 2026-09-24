// The running traffic: signals, the commit-and-claim junction rule, car
// following, routing round the city, the bus line's stops, the vehicles'
// lights and the collision measurements. The one-off geometry (routes, how
// each size of vehicle swings through them, shared lanes, conflict zones) and
// the self-test are in TrafficBuild.cpp.

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
    // The wheel the simulation rolls; the renderer rescales the angle to
    // each kind's real wheel.
    constexpr float wheelRadius = 0.34f;

    // Intelligent Driver Model (Treiber, Hennecke & Helbing, 2000). The
    // acceleration a, braking b and time gap T belong to each vehicle.
    constexpr float standstillGap = 2.0f;         // s0, bumper to bumper
    constexpr float emergencyBraking = 8.0f;

    // The safety net under the model: a car never gets closer than this to the
    // car in front, whatever the acceleration says.
    constexpr float hardMinimumGap = 0.6f;

    // A car only takes its turn if priority traffic is further away than this.
    constexpr float acceptedGapSeconds = 3.5f;

    // After this long at the line, crossing traffic holds back for a car.
    constexpr float starvationSeconds = 20.0f;

    constexpr float leaderLookAhead = 80.0f;

    // Half the length of an ordinary car. The tail that keeps a follower
    // watching its leader after their routes split is measured for a car;
    // a longer leader is watched until its rear, not its middle, is past it.
    constexpr float ordinaryHalfLength = 2.35f;

    // The bus line: how long a bus stands at a stop, and how long its doors
    // take to open or close.
    constexpr float busDwellSeconds = 8.0f;
    constexpr float doorSeconds = 1.0f;

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

    // Pedestrian crossings. A vehicle held by a crossing stops this far
    // short of touching it (the touching distance already has 0.7 m of
    // margin between the body and the painted band).
    constexpr float crossingClearance = 0.5f;
    // After standing this long for people, a vehicle stops giving way to
    // those still at the kerb, and they let it go first.
    constexpr float pedestrianPatience = 7.0f;
    // A green holds at most this long past its end for people still crossing.
    constexpr float walkerHoldSeconds = 12.0f;
    // How far ahead a driver looks for people waiting at a zebra.
    constexpr float zebraLookAhead = 40.0f;

    // Distance a vehicle needs to stop comfortably, with half a second to react.
    float comfortableStop(const Vehicle& vehicle)
    {
        const float speed = vehicle.currentSpeed;
        return speed * speed / (2.0f * vehicle.comfortableBraking) + 0.5f * speed;
    }

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

    // Soonest a vehicle could cover `distance` if it accelerated flat out.
    float earliestArrival(float distance, float speed, float topSpeed, float acceleration)
    {
        if (distance <= 0.0f)
            return 0.0f;
        speed = std::min(speed, topSpeed);
        const float timeToTop = (topSpeed - speed) / acceleration;
        const float distanceToTop = speed * timeToTop + 0.5f * acceleration * timeToTop * timeToTop;
        if (distance <= distanceToTop)
            return (-speed + std::sqrt(speed * speed + 2.0f * acceleration * distance)) / acceleration;
        return timeToTop + (distance - distanceToTop) / topSpeed;
    }

    // IDM acceleration towards a car (or a stop line) `gap` metres ahead.
    float followingAcceleration(const Vehicle& vehicle, float speed, float desiredSpeed, float gap, float closingSpeed)
    {
        const float a = vehicle.maximumAcceleration;
        const float b = vehicle.comfortableBraking;
        const float desiredGap = standstillGap + std::max(0.0f,
            speed * vehicle.timeHeadway + speed * closingSpeed / (2.0f * std::sqrt(a * b)));
        const float freeTerm = std::pow(speed / desiredSpeed, 4.0f);
        const float gapTerm = desiredGap / std::max(gap, 0.1f);
        return a * (1.0f - freeTerm - gapTerm * gapTerm);
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
}

TrafficSystem::TrafficSystem(std::size_t vehicleCount, unsigned int seed)
    : network_(RoadNetwork::makeCity()), vehicleCount_(vehicleCount), seed_(seed)
{
    buildRoutes();
    buildSuccessors();
    buildBusLine();
    buildBodyPaths();
    findAllowedRoutes();
    buildSharedSpans();
    buildConflicts();
    buildCrossingRefs();
    finishRoutes();
    reset();
}

// ---------------------------------------------------------------------------
// Placement and routing
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
    std::fill(crossingStates_.begin(), crossingStates_.end(), CrossingState {});
    vehicles_.clear();

    const std::vector<VehicleKind> mix = trafficMix(vehicleCount_);
    for (std::size_t index = 0; index < vehicleCount_; ++index)
    {
        const VehicleSpec& spec = vehicleSpec(mix[index]);
        const std::size_t sizeClass = static_cast<std::size_t>(spec.sizeClass);
        Vehicle vehicle;
        vehicle.id = index;
        vehicle.kind = mix[index];
        vehicle.sizeClass = spec.sizeClass;
        vehicle.tier = widthTier(spec.sizeClass);
        vehicle.color = spec.palette[(index * 5 + index / spec.palette.size()) % spec.palette.size()];
        vehicle.halfLength = 0.5f * spec.length;
        vehicle.halfWidth = 0.5f * spec.width;
        vehicle.rearReach = rearReach_[sizeClass];
        vehicle.frontReach = frontReach_[sizeClass];
        // A little spread in cruising speed, so traffic does not move in step.
        vehicle.maximumSpeed = spec.cruiseSpeed + (static_cast<float>(index % 3) - 1.0f) * 0.35f;
        vehicle.maximumAcceleration = spec.acceleration;
        vehicle.comfortableBraking = spec.braking;
        vehicle.timeHeadway = spec.timeHeadway;
        vehicle.lineBus = mix[index] == VehicleKind::Bus && !busLine_.empty();
        vehicle.claims.reserve(48);
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
    // Scatter the vehicles over the whole network, each on a route its size
    // may drive, well clear of the others and short of its junction, so
    // nobody starts inside a box. The buses go on their line.
    for (Vehicle& vehicle : vehicles_)
    {
        vehicle.active = false;
        vehicle.claims.clear();
        vehicle.committed = false;
    }

    for (Vehicle& vehicle : vehicles_)
    {
        const std::size_t sizeClass = static_cast<std::size_t>(vehicle.sizeClass);
        for (int attempt = 0; attempt < 800; ++attempt)
        {
            const std::size_t routeIndex = nextRandom() % routes_.size();
            const RouteInfo& info = routes_[routeIndex];
            if (!info.allowed[sizeClass])
                continue;
            const float lowest = 3.0f;
            const float highest = needsCommitOn(vehicle, routeIndex) ? info.stopDistance[sizeClass] - 6.0f
                                                                      : info.route.totalLength() - 3.0f;
            if (highest <= lowest)
                continue;
            const float fraction = static_cast<float>(nextRandom() % 10000) / 10000.0f;
            const float distance = lowest + fraction * (highest - lowest);
            const glm::vec3 position = info.route.sample(distance).position;

            bool clear = true;
            for (const Vehicle& other : vehicles_)
            {
                const float spacing = std::max(15.0f, vehicle.halfLength + other.halfLength + 6.0f);
                if (other.active && glm::length(other.position - position) < spacing)
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
    // A route with nothing to cross (a bend) is driven straight through.
    vehicle.committed = !needsCommitOn(vehicle, routeIndex);
    vehicle.nextRouteIndex = chooseNextRoute(vehicle, routeIndex);
    vehicle.stopServed = false;
    vehicle.dwellSeconds = 0.0f;
}

void TrafficSystem::placeOnRoute(Vehicle& vehicle, std::size_t routeIndex, float distance, float speed)
{
    enterRoute(vehicle, routeIndex);
    vehicle.distance = distance;
    vehicle.currentSpeed = speed;
    vehicle.acceleration = 0.0f;
    vehicle.stoppedSeconds = 0.0f;

    const BodyFrame frame = bodyFrame(routeIndex, vehicle.sizeClass, distance);
    vehicle.position = frame.centre;
    vehicle.yawDegrees = frame.yawDegrees;
    vehicle.steerAngleDegrees = frame.steerDegrees;

    // A placed car appears where it is; it must not be blended in from
    // wherever it was before (it would streak across the scene).
    vehicle.previousPosition = vehicle.position;
    vehicle.previousYawDegrees = vehicle.yawDegrees;
    vehicle.previousWheelAngleDegrees = vehicle.wheelAngleDegrees;
    vehicle.previousSteerAngleDegrees = vehicle.steerAngleDegrees;
    vehicle.previousDoorOpen = vehicle.doorOpen;
}

std::size_t TrafficSystem::chooseNextRoute(const Vehicle& vehicle, std::size_t routeIndex)
{
    const std::size_t sizeClass = static_cast<std::size_t>(vehicle.sizeClass);
    const std::vector<std::size_t>& candidates = routes_[routeIndex].successors;

    // A random choice among the routes this size may drive, weighted away
    // from exits that many cars are already heading for, so no single part of
    // the city clogs up. (A bus has exactly one: the next leg of its line.)
    std::array<float, 8> weights {};
    float total = 0.0f;
    std::size_t last = noRoute;
    for (std::size_t option = 0; option < candidates.size() && option < weights.size(); ++option)
    {
        const RouteInfo& candidate = routes_[candidates[option]];
        if (!candidate.allowed[sizeClass])
            continue;
        last = candidates[option];
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
        // Changing lane is the less likely choice, so traffic does not weave.
        if (candidate.inLane != candidate.outLane)
            weights[option] *= 0.5f;
        total += weights[option];
    }
    if (last == noRoute)
        return noRoute;

    float pick = static_cast<float>(nextRandom() % 10000) / 10000.0f * total;
    for (std::size_t option = 0; option < candidates.size() && option < weights.size(); ++option)
    {
        if (weights[option] <= 0.0f)
            continue;
        pick -= weights[option];
        if (pick <= 0.0f)
            return candidates[option];
    }
    return last;
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
        pose.id = vehicle.id;
        pose.kind = vehicle.kind;
        pose.active = vehicle.active;
        pose.color = vehicle.color;
        pose.position = glm::mix(vehicle.previousPosition, vehicle.position, alpha);
        pose.yawDegrees = blendAngle(vehicle.previousYawDegrees, vehicle.yawDegrees);
        pose.wheelAngleDegrees = blendAngle(vehicle.previousWheelAngleDegrees, vehicle.wheelAngleDegrees);
        pose.steerAngleDegrees = glm::mix(vehicle.previousSteerAngleDegrees, vehicle.steerAngleDegrees, alpha);
        pose.doorOpen = glm::mix(vehicle.previousDoorOpen, vehicle.doorOpen, alpha);
        pose.braking = vehicle.braking;
        pose.indicator = vehicle.indicator;
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
        SignalController& controller = signals_[junction];
        controller.elapsed += dt;
        if (!signalPhaseOver(junction))
            continue;
        // A green due to end first closes the walk (the walkers' lights
        // flash), then holds until everyone crossing with it will be over
        // before the crossing traffic's green.
        if (isGreen(controller.phase) && controller.heldSeconds < walkerHoldSeconds &&
            !walkersClear(junction, phaseServesNorthSouth(controller.phase)))
        {
            controller.walkClosed = true;
            controller.heldSeconds += dt;
            continue;
        }
        advancePhase(junction);
    }

    // Remember where everything was, for render interpolation.
    for (Vehicle& vehicle : vehicles_)
    {
        vehicle.previousPosition = vehicle.position;
        vehicle.previousYawDegrees = vehicle.yawDegrees;
        vehicle.previousWheelAngleDegrees = vehicle.wheelAngleDegrees;
        vehicle.previousSteerAngleDegrees = vehicle.steerAngleDegrees;
        vehicle.previousDoorOpen = vehicle.doorOpen;
    }

    // 1. Everyone looks at the traffic as it stands at the start of the step.
    //    Leaders only ever move forward, so this view is on the safe side.
    const std::size_t count = vehicles_.size();
    std::vector<Leader>& leaders = leaders_;
    leaders.assign(count, Leader {});
    guestLimits_.assign(count, GuestLimit {});
    crossingLimits_.assign(count, GuestLimit {});
    for (std::size_t index = 0; index < count; ++index)
    {
        if (!vehicles_[index].active)
            continue;
        leaders[index] = findLeader(index);
        if (!guests_.empty())
            guestLimits_[index] = guestAhead(index);
        crossingLimits_[index] = crossingAhead(index);
    }

    // 2. A car let through on green that has not reached its line when the
    //    light changes stops after all if it comfortably can. It is still
    //    outside every zone, so giving its claims back is always safe.
    for (Vehicle& vehicle : vehicles_)
    {
        if (!vehicle.active || !vehicle.committed)
            continue;
        const RouteInfo& info = routeFor(vehicle);
        if (!network_.junctions()[info.junction].isSignalised())
            continue;
        const float toLine = stopFor(vehicle) - vehicle.distance;
        const float comfortableStop =
            vehicle.currentSpeed * vehicle.currentSpeed / (2.0f * vehicle.comfortableBraking);
        if (toLine > 0.0f && toLine >= comfortableStop && !movementPermitted(vehicle))
        {
            releaseClaims(vehicle);
            vehicle.committed = false;
        }
    }

    // 3. Junction decisions, cars standing at their lines first (longest wait
    //    first), then by distance to the line. Each commit takes its claims at
    //    once, so the next car already sees them: two cars can never commit
    //    into one zone.
    std::vector<std::size_t>& order = order_;
    order.resize(count);
    std::iota(order.begin(), order.end(), std::size_t {0});
    std::sort(order.begin(), order.end(), [this](std::size_t a, std::size_t b)
    {
        const float toLineA = stopFor(vehicles_[a]) - vehicles_[a].distance;
        const float toLineB = stopFor(vehicles_[b]) - vehicles_[b].distance;
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

    // 4. Move.
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
        // front, never past the stop line without having committed, and a
        // bus never past its stop before it has stopped there.
        float limit = std::numeric_limits<float>::max();
        if (leader.vehicle != nullptr)
            limit = std::max(0.0f, leader.gap - hardMinimumGap);
        // The player: the first touching position was found in half-metre
        // steps with a 0.3 m margin, so stopping half a metre short of it
        // always leaves a gap.
        if (guestLimits_[index].present)
            limit = std::min(limit, std::max(0.0f, guestLimits_[index].gap - 0.5f));
        if (!vehicle.committed)
            limit = std::min(limit, std::max(0.0f, stopFor(vehicle) - vehicle.distance));
        // Never onto a crossing somebody is on (or short of a zebra it has
        // chosen to stop at). Being held here while still moving at a
        // walking pace or more is counted: the rules should never need it.
        const GuestLimit& crossingLimit = crossingLimits_[index];
        if (crossingLimit.present && crossingLimit.gap < limit)
        {
            limit = std::max(0.0f, crossingLimit.gap);
            if (travel > limit && speed > 1.0f)
                ++stats_.crossingHardStops;
        }
        const bool stopAhead = vehicle.lineBus && info.busStop >= 0.0f && !vehicle.stopServed &&
                               info.busStop >= vehicle.distance - 0.01f;
        if (stopAhead)
            limit = std::min(limit, std::max(0.0f, info.busStop - vehicle.distance));
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

        // A line bus standing at its stop: doors open, passengers, doors
        // close, and off it goes.
        if (stopAhead && info.busStop - vehicle.distance < 0.05f && vehicle.currentSpeed < 0.05f)
        {
            vehicle.dwellSeconds += dt;
            if (vehicle.dwellSeconds >= busDwellSeconds)
            {
                vehicle.stopServed = true;
                vehicle.dwellSeconds = 0.0f;
                ++stats_.busStopsServed;
            }
        }
        const float doorTarget = vehicle.dwellSeconds > 0.3f && vehicle.dwellSeconds < busDwellSeconds - doorSeconds - 0.3f
                                 ? 1.0f : 0.0f;
        vehicle.doorOpen += glm::clamp(doorTarget - vehicle.doorOpen, -dt / doorSeconds, dt / doorSeconds);

        // Let go of every zone the body has now completely left.
        for (std::size_t claim = 0; claim < vehicle.claims.size();)
        {
            const std::size_t slot = vehicle.claims[claim];
            if (vehicle.distance > zoneOut(vehicle, conflicts_[slot / 2], static_cast<int>(slot % 2)))
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
        // the next junction's route. The network is closed and every route
        // has a successor this size may drive (the self-test checks it), so
        // vehicles drive for ever.
        const float length = info.route.totalLength();
        if (vehicle.distance >= length)
        {
            ++stats_.trips;
            const float overshoot = vehicle.distance - length;
            enterRoute(vehicle, vehicle.nextRouteIndex);
            vehicle.distance = overshoot;
        }

        // The body: front axle on the lane, rear axle trailing behind it,
        // front wheels pointing along the lane.
        const RouteInfo& current = routeFor(vehicle);
        const BodyFrame frame = bodyFrame(vehicle.routeIndex, vehicle.sizeClass, vehicle.distance);
        vehicle.position = frame.centre;
        vehicle.yawDegrees = frame.yawDegrees;
        vehicle.steerAngleDegrees = frame.steerDegrees;

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

        // Standing for people: held short of a crossing, or at the line
        // because of people at a crossing beyond it.
        vehicle.heldByPedestrians =
            (crossingLimit.present && crossingLimit.gap < 1.0f) ||
            (!vehicle.committed && stopFor(vehicle) - vehicle.distance < 1.0f && pedestriansHold(vehicle));
        if (vehicle.currentSpeed < 0.05f && vehicle.heldByPedestrians)
            vehicle.pedestrianWaitSeconds += dt;
        else if (vehicle.currentSpeed > 0.5f)
            vehicle.pedestrianWaitSeconds = 0.0f;

        updateLights(vehicle);
    }

    measureBodies(dt);
}

void TrafficSystem::updateLights(Vehicle& vehicle) const
{
    // Brake lights while slowing down, and while standing (foot on the brake).
    vehicle.braking = vehicle.acceleration < -0.45f || vehicle.currentSpeed < 0.3f;

    // The indicator, from well before a turn or a lane change until it is done.
    vehicle.indicator = 0;
    for (const Indication& indication : routeFor(vehicle).indications)
    {
        if (vehicle.distance >= indication.from && vehicle.distance <= indication.to)
            vehicle.indicator = indication.direction;
    }
    // A bus about to pull away from its stop indicates too.
    if (vehicle.lineBus && vehicle.dwellSeconds > busDwellSeconds - 3.0f)
        vehicle.indicator = -1;
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

    // A long vehicle is watched until its rear is past the split.
    const float slack = std::max(0.0f, other.halfLength - ordinaryHalfLength);
    for (const SharedSpan& span : routes_[routeIndex].shared)
    {
        if (span.other != other.routeIndex)
            continue;
        const float mapped = other.distance - span.offset;
        if (mapped >= span.from && mapped <= span.to + span.tail + slack)
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
                const float slack = std::max(0.0f, other.halfLength - ordinaryHalfLength);
                for (const SharedSpan& span : info.shared)
                {
                    if (span.other == other.routeIndex && me.distance <= span.to + span.tail &&
                        along >= span.from && along <= span.to + span.tail + slack)
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
    float acceleration = vehicle.maximumAcceleration * (1.0f - std::pow(speed / desired, 4.0f));

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
            followingAcceleration(vehicle, speed, desired, leader.gap, speed - leader.speed));

    // The player, like a car in front.
    if (vehicle.id < guestLimits_.size() && guestLimits_[vehicle.id].present)
    {
        const GuestLimit& guest = guestLimits_[vehicle.id];
        acceleration = std::min(acceleration,
            followingAcceleration(vehicle, speed, desired, std::max(guest.gap - 0.5f, 0.05f), speed - guest.speed));
    }

    // A crossing it must stop short of, like a stop line.
    if (vehicle.id < crossingLimits_.size() && crossingLimits_[vehicle.id].present)
    {
        acceleration = std::min(acceleration,
            followingAcceleration(vehicle, speed, desired, crossingLimits_[vehicle.id].gap + standstillGap, speed));
    }

    // Until it has been let through, the stop line acts as a stationary car.
    // The standstill gap is added so the model settles with the car centre
    // exactly on its waiting position. A bus's next stop acts the same way.
    if (!vehicle.committed)
    {
        const float toLine = stopFor(vehicle) - vehicle.distance;
        acceleration = std::min(acceleration,
            followingAcceleration(vehicle, speed, desired, toLine + standstillGap, speed));
    }
    if (vehicle.lineBus && info.busStop >= 0.0f && !vehicle.stopServed && info.busStop >= vehicle.distance - 0.01f)
    {
        acceleration = std::min(acceleration,
            followingAcceleration(vehicle, speed, desired, info.busStop - vehicle.distance + standstillGap, speed));
    }

    return glm::clamp(acceleration, -emergencyBraking, vehicle.maximumAcceleration);
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
    const float toLine = stopFor(vehicle) - vehicle.distance;
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
    // Never enter the box unless there is space to leave it: a vehicle stuck
    // inside would block every crossing route.
    const float exit = exitFor(vehicle);
    const float needed = exit + 2.0f * vehicle.halfLength + standstillGap;

    for (const Vehicle& other : vehicles_)
    {
        if (!other.active || &other == &vehicle || other.currentSpeed > 1.0f)
            continue;
        float along = 0.0f;
        if (!projectOnto(other, vehicle.routeIndex, along))
            continue;

        // Only a slow vehicle standing in the space just beyond the box counts.
        if (along + other.halfLength > exit && along - other.halfLength < needed)
            return false;
    }
    return true;
}

float TrafficSystem::slowestClearingTime(const Vehicle& vehicle, float distance) const
{
    // A deliberately pessimistic drive: half a second before the vehicle even
    // starts to speed up (the jerk limit), then only 5/8 of its full
    // acceleration (1 m/s^2 for a car), never faster than the corners allow.
    // The real vehicle can only do better.
    const RouteInfo& info = routeFor(vehicle);
    const float slowAcceleration = 0.625f * vehicle.maximumAcceleration;
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
            speed = std::min(speed + slowAcceleration * step, limit);
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
    const float clearDistance = zoneOut(vehicle, conflict, mine) - vehicle.distance;

    // Nothing ahead may hold the car up on its way through the zone.
    if (leader.vehicle != nullptr && leader.gap < clearDistance + 12.0f)
        return false;

    const float clearingTime = slowestClearingTime(vehicle, clearDistance) + 1.5f;

    // Every vehicle holding the other side must be further off than that,
    // even if it accelerated flat out from now on.
    const std::size_t theirSlot = static_cast<std::size_t>(&conflict - conflicts_.data()) * 2 +
                                  static_cast<std::size_t>(theirs);
    for (const Vehicle& other : vehicles_)
    {
        if (!other.active || std::find(other.claims.begin(), other.claims.end(), theirSlot) == other.claims.end())
            continue;
        const float toZone = zoneIn(other, conflict, theirs) - other.distance;
        if (toZone <= 0.0f)
            return false;   // already in it
        if (earliestArrival(toZone, other.currentSpeed, other.maximumSpeed, other.maximumAcceleration) < clearingTime)
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
    for (const ConflictRef& ref : zonesFor(vehicle))
    {
        if (vehicle.distance > zoneOut(vehicle, conflicts_[ref.conflict], ref.side))
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
    if (!needsCommitOn(vehicle, vehicle.routeIndex))
        return nullptr;

    // Decide early enough on green not to brake for nothing, but no earlier:
    // a claim taken far from the line blocks crossing traffic for nothing.
    const float toLine = stopFor(vehicle) - vehicle.distance;
    const float speed = vehicle.currentSpeed;
    const float braking = vehicle.comfortableBraking;
    const float decisionDistance = decidesEarly(info)
        ? std::max(5.0f, speed * 1.5f + speed * speed / (2.0f * braking))
        : std::max(6.0f, speed * speed / (2.0f * braking) + 2.0f);
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

    // People on a crossing on its way through, or waiting to cross it with
    // WALK or at a zebra: turning traffic gives way.
    if (pedestriansHold(vehicle))
        return "giving way to pedestrians";

    // A zebra before the line (a roundabout's entry) is dealt with first: a
    // vehicle let into the junction must never stop for people after that,
    // or crossing traffic timed to pass behind it could run into it. Once
    // its body is over the zebra nobody steps onto it any more.
    for (const CrossingRef& ref : info.crossings[static_cast<std::size_t>(vehicle.sizeClass)])
    {
        if (!ref.afterLine && vehicle.distance < ref.in)
            return "not over the zebra yet";
    }

    // Never into a junction the player is standing or parked in.
    if (vehicleIndex < guestLimits_.size() && guestLimits_[vehicleIndex].present &&
        guestLimits_[vehicleIndex].gap < exitFor(vehicle) - vehicle.distance + 2.0f * vehicle.halfLength)
        return "waiting for the player";

    for (const ConflictRef& ref : zonesFor(vehicle))
    {
        const Conflict& conflict = conflicts_[ref.conflict];
        const int mine = ref.side;
        const int theirs = 1 - mine;
        if (vehicle.distance > zoneOut(vehicle, conflict, mine))
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
            if (!other.active || other.committed ||
                other.routeIndex != conflict.route[static_cast<std::size_t>(theirs)] ||
                other.tier != conflict.tier[static_cast<std::size_t>(theirs)])
                continue;
            if (!movementPermitted(other))
                continue;

            const float otherToLine = stopFor(other) - other.distance;
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
                routeFor(*otherLeader.vehicle).junction == routeFor(other).junction)
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
                otherToLine < other.currentSpeed * other.currentSpeed / (2.0f * other.comfortableBraking);
            if (!theyHavePriority && !theyCannotStop)
                continue;

            const float toZone = zoneIn(other, conflict, theirs) - other.distance;
            if (toZone < 0.0f)
                continue;
            if (earliestArrival(toZone, other.currentSpeed, other.maximumSpeed, other.maximumAcceleration) < acceptedGapSeconds)
                return theyHavePriority ? "giving way to priority traffic" : "giving way to a car that cannot stop";
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Measurements
// ---------------------------------------------------------------------------

TrafficSystem::GuestLimit TrafficSystem::guestAhead(std::size_t vehicleIndex) const
{
    GuestLimit result;
    const Vehicle& me = vehicles_[vehicleIndex];
    const float reach = std::min(60.0f, me.currentSpeed * me.currentSpeed / (2.0f * me.comfortableBraking) + 14.0f);
    const glm::vec2 here {me.position.x, me.position.z};

    bool anyNear = false;
    for (const Guest& guest : guests_)
        anyNear = anyNear || glm::length(guest.body.centre - here) < reach + me.halfLength + 6.0f;
    if (!anyNear)
        return result;

    // Slide the vehicle's body (0.3 m larger all round) along its path, into
    // the next route, and stop at the first place it would touch a guest.
    const RouteInfo& info = routeFor(me);
    const float length = info.route.totalLength();
    const glm::vec2 inflated {me.halfWidth + 0.3f, me.halfLength + 0.3f};
    for (float ahead = 0.0f; ahead <= reach; ahead += 0.5f)
    {
        float along = me.distance + ahead;
        std::size_t routeIndex = me.routeIndex;
        if (along > length)
        {
            if (me.nextRouteIndex == noRoute)
                break;
            routeIndex = me.nextRouteIndex;
            along -= length;
            if (along > routes_[routeIndex].route.totalLength())
                break;
        }
        const BodyFrame frame = bodyFrame(routeIndex, me.sizeClass, along);
        const OrientedBox body = makeOrientedBox({frame.centre.x, frame.centre.z}, frame.yawDegrees, inflated);
        for (const Guest& guest : guests_)
        {
            if (!boxesOverlap(body, guest.body))
                continue;
            const float yaw = glm::radians(frame.yawDegrees);
            result.present = true;
            result.gap = ahead;
            result.speed = std::max(0.0f, glm::dot(guest.velocity, glm::vec2 {std::sin(yaw), std::cos(yaw)}));
            return result;
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Pedestrian crossings
// ---------------------------------------------------------------------------

void TrafficSystem::setCrossingStates(const std::vector<CrossingState>& states)
{
    if (states.size() == crossingStates_.size())
        crossingStates_ = states;
}

bool TrafficSystem::walkAllowed(std::size_t crossing) const
{
    const Crossing& info = network_.crossings()[crossing];
    if (!info.signalised)
        return true;
    const SignalController& controller = signals_[info.junction];
    return isGreen(controller.phase) && !controller.walkClosed &&
           phaseServesNorthSouth(controller.phase) == info.walksWithNorthSouth;
}

WalkLight TrafficSystem::walkLight(std::size_t crossing) const
{
    const Crossing& info = network_.crossings()[crossing];
    if (!info.signalised)
        return WalkLight::None;
    if (walkAllowed(crossing))
        return WalkLight::Walk;
    const SignalController& controller = signals_[info.junction];
    const bool parallelGreen = isGreen(controller.phase) &&
                               phaseServesNorthSouth(controller.phase) == info.walksWithNorthSouth;
    return parallelGreen && controller.walkClosed ? WalkLight::Flashing : WalkLight::DontWalk;
}

bool TrafficSystem::walkersClear(std::size_t junction, bool northSouth) const
{
    // Anyone still crossing will be over within the yellow and the all-red,
    // with a second to spare.
    const std::vector<Crossing>& crossings = network_.crossings();
    for (std::size_t index = 0; index < crossings.size() && index < crossingStates_.size(); ++index)
    {
        if (crossings[index].junction != junction || crossings[index].walksWithNorthSouth != northSouth)
            continue;
        const CrossingState& state = crossingStates_[index];
        if (state.onBand > 0 && state.clearSeconds > yellowSeconds + allRedSeconds - 1.0f)
            return false;
    }
    return true;
}

TrafficSystem::GuestLimit TrafficSystem::crossingAhead(std::size_t vehicleIndex) const
{
    GuestLimit result;
    if (crossingStates_.empty())
        return result;
    const Vehicle& me = vehicles_[vehicleIndex];
    const std::size_t sizeClass = static_cast<std::size_t>(me.sizeClass);
    const float canStopIn = comfortableStop(me);

    // Along this route and on into the next one, nearest first.
    float offset = 0.0f;
    std::size_t routeIndex = me.routeIndex;
    for (int pass = 0; pass < 2; ++pass)
    {
        if (pass == 1)
        {
            if (me.nextRouteIndex == noRoute)
                break;
            offset = routes_[me.routeIndex].route.totalLength();
            routeIndex = me.nextRouteIndex;
        }
        for (const CrossingRef& ref : routes_[routeIndex].crossings[sizeClass])
        {
            const float in = ref.in + offset;
            if (me.distance >= in || in - me.distance > leaderLookAhead)
                continue;   // on it or past it already (it was clear then), or far off
            const CrossingState& state = crossingStates_[ref.crossing];
            const float stopAt = in - crossingClearance;
            const float toStop = stopAt - me.distance;

            // Somebody is on it: stop short of it, whatever else is going on.
            bool hold = state.onBand > 0;
            // Somebody waits at a zebra on the approach (not one beyond the
            // line, which the junction decision deals with): give way if
            // that is a comfortable stop, and not for ever.
            if (!hold && state.waiting > 0 && !ref.afterLine && walkAllowed(ref.crossing) &&
                me.pedestrianWaitSeconds < pedestrianPatience && toStop >= canStopIn - 0.05f &&
                in - me.distance < zebraLookAhead)
                hold = true;
            if (!hold)
                continue;

            result.present = true;
            // Already inside the clearance: stop just short of touching.
            result.gap = toStop >= 0.0f ? toStop : std::max(0.0f, in - 0.1f - me.distance);
            result.speed = 0.0f;
            return result;
        }
    }
    return result;
}

bool TrafficSystem::pedestriansHold(const Vehicle& vehicle) const
{
    if (vehicle.committed || crossingStates_.empty())
        return false;
    const float toLine = stopFor(vehicle) - vehicle.distance;
    const bool canStop = toLine >= comfortableStop(vehicle) - 0.05f;
    for (const CrossingRef& ref : routeFor(vehicle).crossings[static_cast<std::size_t>(vehicle.sizeClass)])
    {
        if (!ref.afterLine || ref.out < vehicle.distance)
            continue;
        const CrossingState& state = crossingStates_[ref.crossing];
        if (state.onBand > 0)
            return true;
        if (state.waiting > 0 && canStop && walkAllowed(ref.crossing) &&
            vehicle.pedestrianWaitSeconds < pedestrianPatience)
            return true;
    }
    return false;
}

const Vehicle* TrafficSystem::crossingBlocker(std::size_t crossing, const char** reason) const
{
    const auto blocked = [reason](const Vehicle& vehicle, const char* why)
    {
        if (reason != nullptr)
            *reason = why;
        return &vehicle;
    };
    for (const Vehicle& vehicle : vehicles_)
    {
        if (!vehicle.active)
            continue;
        const std::size_t sizeClass = static_cast<std::size_t>(vehicle.sizeClass);
        const bool longWait = vehicle.pedestrianWaitSeconds >= pedestrianPatience;
        float offset = 0.0f;
        std::size_t routeIndex = vehicle.routeIndex;
        for (int pass = 0; pass < 2; ++pass)
        {
            if (pass == 1)
            {
                if (vehicle.nextRouteIndex == noRoute)
                    break;
                offset = routes_[vehicle.routeIndex].route.totalLength();
                routeIndex = vehicle.nextRouteIndex;
            }
            const RouteInfo& info = routes_[routeIndex];
            for (const CrossingRef& ref : info.crossings[sizeClass])
            {
                if (ref.crossing != crossing)
                    continue;
                const float in = ref.in + offset;
                const float out = ref.out + offset;
                if (vehicle.distance > out)
                    continue;              // gone past
                if (vehicle.distance >= in)
                    return blocked(vehicle, "on the crossing");

                if (ref.afterLine)
                {
                    // Beyond its line: one already let into the junction
                    // is let clear. One still before the line is held there
                    // while anyone is on the crossing - it cannot enter the
                    // junction - so it only matters if it would be let in
                    // now and could no longer stop comfortably at the line,
                    // or if it has waited its turn.
                    if (pass == 0 && vehicle.committed)
                        return blocked(vehicle, "turning onto it");
                    // (A vehicle already braking for the line is not too
                    // close: it only has to brake a little firmer at most.)
                    const float toLine = info.stopDistance[sizeClass] + offset - vehicle.distance;
                    const float firmStop = vehicle.currentSpeed * vehicle.currentSpeed / (2.6f * vehicle.comfortableBraking);
                    if (pass == 0 && toLine < firmStop && movementPermitted(vehicle))
                        return blocked(vehicle, "too close to its line to stop");
                    if (longWait && toLine < 2.0f)
                        return blocked(vehicle, "has waited its turn");
                    continue;
                }

                // On the approach: it must still be able to stop comfortably
                // short of it, or have waited its turn.
                const float toStop = in - crossingClearance - vehicle.distance;
                if (toStop < comfortableStop(vehicle) + 0.3f)
                    return blocked(vehicle, "too close to stop");
                if (longWait && toStop < 3.0f)
                    return blocked(vehicle, "has waited its turn");
            }
        }
    }
    return nullptr;
}

void TrafficSystem::bodies(std::vector<OrientedBox>& out) const
{
    out.clear();
    for (const Vehicle& vehicle : vehicles_)
    {
        if (vehicle.active)
            out.push_back(bodyOf(vehicle));
    }
}

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
            // Two bodies further apart than both their half lengths and a
            // bit cannot be close to touching.
            const float reach = vehicles_[a].halfLength + vehicles_[b].halfLength + 1.5f;
            const glm::vec3 between = vehicles_[a].position - vehicles_[b].position;
            if (glm::dot(between, between) > reach * reach)
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
    // People waiting to walk with this green (they pressed the button).
    if (!leftTurnsOnly)
    {
        const std::vector<Crossing>& crossings = network_.crossings();
        for (std::size_t index = 0; index < crossings.size() && index < crossingStates_.size(); ++index)
        {
            if (crossings[index].junction == junction && crossings[index].walksWithNorthSouth == northSouth &&
                crossingStates_[index].waiting > 0)
                return true;
        }
    }

    for (std::size_t index = 0; index < vehicles_.size(); ++index)
    {
        const Vehicle& vehicle = vehicles_[index];
        if (!vehicle.active || vehicle.committed)
            continue;
        const RouteInfo& info = routeFor(vehicle);
        if (info.junction != junction || armIsNorthSouth(info.inArm) != northSouth ||
            stopFor(vehicle) - vehicle.distance >= demandRange)
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
    controller.walkClosed = false;
    controller.heldSeconds = 0.0f;
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
        char line[360];
        std::snprintf(line, sizeof(line),
            "  %-10s %2zu %s %-7s %s%s->%s%s route %3zu s %6.2f / stop %6.2f  v %4.2f a %5.2f cmd %5.2f  %s claims %zu  leader %s gap %.2f  stopped %.0f s  %s%s\n",
            vehicleKindName(vehicle.kind), index, vehicle.active ? "on " : "off",
            network_.junctions()[info.junction].name.c_str(),
            armName(info.inArm), info.inLane == 0 ? "i" : "o", armName(info.outArm), info.outLane == 0 ? "i" : "o",
            vehicle.routeIndex, vehicle.distance, stopFor(vehicle), vehicle.currentSpeed,
            vehicle.acceleration, vehicle.active ? commandedAcceleration(vehicle, leader) : 0.0f,
            vehicle.committed ? "COMMITTED" : "waiting", vehicle.claims.size(),
            leader.vehicle != nullptr ? std::to_string(leader.vehicle->id).c_str() : "-",
            leader.vehicle != nullptr ? leader.gap : 0.0f, vehicle.stoppedSeconds,
            blocker != nullptr ? blocker : "",
            vehicle.dwellSeconds > 0.0f ? " (at the bus stop)" : "");
        text += line;

        // For a car locked out by claims, who holds them.
        if (blocker != nullptr && std::string(blocker) == "zone claimed by crossing traffic")
        {
            text += "        held by:";
            for (const ConflictRef& ref : zonesFor(vehicle))
            {
                const std::size_t theirSlot = ref.conflict * 2 + static_cast<std::size_t>(1 - ref.side);
                if (claimCounts_[theirSlot] == 0 || vehicle.distance > zoneOut(vehicle, conflicts_[ref.conflict], ref.side))
                    continue;
                for (const Vehicle& other : vehicles_)
                {
                    if (std::find(other.claims.begin(), other.claims.end(), theirSlot) != other.claims.end())
                        text += " " + std::string(vehicleKindName(other.kind)) + " " + std::to_string(other.id) +
                                " (zone " + std::to_string(ref.conflict) + ", " +
                                std::to_string(static_cast<int>(zoneIn(other, conflicts_[ref.conflict], 1 - ref.side) - other.distance)) +
                                " m off)";
                }
            }
            text += "\n";
        }
    }
    return text;
}
