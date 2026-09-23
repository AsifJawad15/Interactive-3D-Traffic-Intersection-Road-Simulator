// Everything TrafficSystem works out once, before the first vehicle moves:
// the routes through every junction of the city, how they chain together,
// where routes share a lane, where they conflict, and who has priority.
// Also the --self-test and --plot checks of that geometry.

#include "Simulation.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4996)
#include <stb_image_write.h>
#pragma warning(pop)

namespace
{
    constexpr float sampleStep = 0.25f;

    // Safety margin added around every body when conflict zones are measured.
    constexpr float conflictMargin = 0.30f;

    // How far past a split two routes keep watching each other as leader and
    // follower, while the cars are still close together.
    constexpr float splitTail = 5.0f;

    // Comfortable sideways acceleration in a corner, m/s^2.
    constexpr float lateralAcceleration = 2.0f;

    // Body used to measure conflicts and clearances: the largest vehicle.
    constexpr float bodyHalfWidth = 0.94f;
    constexpr float bodyHalfLength = 2.03f;

    // Where a car's centre waits so that its front bumper is at the line.
    constexpr float lineSetBack = bodyHalfLength + 0.3f;

    struct PathSample
    {
        glm::vec2 position {0.0f};
        float headingDegrees = 0.0f;
        float distance = 0.0f;
    };

    std::vector<PathSample> samplePath(const Route& route)
    {
        std::vector<PathSample> samples;
        const float length = route.totalLength();
        const int count = static_cast<int>(std::floor(length / sampleStep));
        samples.reserve(static_cast<std::size_t>(count) + 2);

        for (int index = 0; index <= count + 1; ++index)
        {
            const float distance = std::min(static_cast<float>(index) * sampleStep, length);
            const RouteSample sample = route.sample(distance);
            samples.push_back({{sample.position.x, sample.position.z}, sample.headingDegrees, distance});
            if (distance >= length)
                break;
        }
        return samples;
    }

    // A grid of half-metre cells over one route's samples, so the samples of
    // another route near a given point are found without trying them all.
    class SampleGrid
    {
    public:
        explicit SampleGrid(const std::vector<PathSample>& samples)
        {
            for (std::size_t index = 0; index < samples.size(); ++index)
                cells_[key(samples[index].position)].push_back(static_cast<int>(index));
        }

        template <typename Visit>
        void near(glm::vec2 point, float radius, Visit&& visit) const
        {
            const int reach = static_cast<int>(std::ceil(radius / cellSize));
            const int cx = static_cast<int>(std::floor(point.x / cellSize));
            const int cz = static_cast<int>(std::floor(point.y / cellSize));
            for (int dx = -reach; dx <= reach; ++dx)
            {
                for (int dz = -reach; dz <= reach; ++dz)
                {
                    const auto found = cells_.find(pack(cx + dx, cz + dz));
                    if (found == cells_.end())
                        continue;
                    for (int index : found->second)
                        visit(index);
                }
            }
        }

    private:
        static constexpr float cellSize = 0.5f;
        std::unordered_map<std::int64_t, std::vector<int>> cells_;

        static std::int64_t pack(int x, int z)
        {
            return (static_cast<std::int64_t>(x) << 32) ^ static_cast<std::int64_t>(static_cast<std::uint32_t>(z));
        }
        static std::int64_t key(glm::vec2 p)
        {
            return pack(static_cast<int>(std::floor(p.x / cellSize)), static_cast<int>(std::floor(p.y / cellSize)));
        }
    };

    float signedAngleDifference(float a, float b)
    {
        float difference = a - b;
        while (difference > 180.0f) difference -= 360.0f;
        while (difference < -180.0f) difference += 360.0f;
        return difference;
    }

    // Signal and T-junction priority: straight on beats a right turn, which
    // beats a left turn.
    int turnRank(Turn turn)
    {
        switch (turn)
        {
        case Turn::Straight: return 2;
        case Turn::Right: return 1;
        default: return 0;
        }
    }

    const char* turnName(Turn turn)
    {
        switch (turn)
        {
        case Turn::Straight: return "straight";
        case Turn::Right: return "right";
        default: return "left";
        }
    }

    const char* typeName(JunctionType type)
    {
        switch (type)
        {
        case JunctionType::SignalCross: return "signalised crossroads";
        case JunctionType::SignalT: return "signalised T-junction";
        case JunctionType::GiveWayT: return "give-way T-junction";
        case JunctionType::Roundabout: return "roundabout";
        default: return "bend";
        }
    }

    bool armIsNorthSouth(int arm)
    {
        return arm == ArmNorth || arm == ArmSouth;
    }

    // ---- Canonical routes ----------------------------------------------------
    // Every route is authored for a car entering from the SOUTH arm, heading
    // north (+z), in the lane `laneOffset` metres to the right of the
    // centreline (x = -laneOffset), and then rotated onto its real arm. The
    // exits in this frame: north = straight on, west = right, east = left.

    // Intersections and bends: straight, or a turn whose arc is concentric
    // with the kerb fillet (or the bend), so every lane keeps its distance
    // from the kerb all the way round. `reach` is where the turn starts.
    Route turnRoute(Turn turn, float lane, float lengthIn, float lengthOut, float reach)
    {
        Route route;
        if (turn == Turn::Straight)
        {
            route.addLine({-lane, -lengthIn}, {-lane, lengthOut});
        }
        else if (turn == Turn::Right)
        {
            route.addLine({-lane, -lengthIn}, {-lane, -reach});
            route.addArc({-reach, -reach}, reach - lane, 0.0f, 90.0f);
            route.addLine({-reach, -lane}, {-lengthOut, -lane});
        }
        else
        {
            route.addLine({-lane, -lengthIn}, {-lane, -reach});
            route.addArc({reach, -reach}, reach + lane, 180.0f, -90.0f);
            route.addLine({reach, lane}, {lengthOut, lane});
        }
        return route;
    }

    // Roundabout entry for one lane. The entry arc curves right while the
    // ring curves left, so the two circles are EXTERNALLY tangent: the gap
    // between their centres is ringRadius + entryRadius. The entry centre lies
    // one entry radius to the driver's right of the lane, which fixes its x;
    // tangency then solves for its z.
    struct EntryGeometry
    {
        glm::vec2 centre {0.0f};
        float mergeAngle = 0.0f;   // degrees, on the ring
        float sweep = 0.0f;        // degrees, along the entry arc
    };

    EntryGeometry entryGeometry(float lane)
    {
        constexpr float ring = RoadNetwork::ringRadius;
        constexpr float entry = RoadNetwork::entryRadius;
        EntryGeometry geometry;
        const float centreX = -(lane + entry);
        const float span = ring + entry;
        const float centreZ = -std::sqrt(span * span - centreX * centreX);
        geometry.centre = {centreX, centreZ};
        const glm::vec2 merge = geometry.centre * (ring / span);
        geometry.mergeAngle = glm::degrees(std::atan2(merge.y, merge.x));
        geometry.sweep = glm::degrees(std::atan2(merge.y - centreZ, merge.x - centreX));
        return geometry;
    }

    // `exitRotation` turns the north exit onto the real exit within the
    // canonical frame: 0 = north (straight), 270 = west (first exit), 90 = east.
    Route roundaboutRoute(float laneIn, float laneOut, float exitRotation,
                          float lengthIn, float lengthOut, float& lineDistance, float& mergeDistance)
    {
        constexpr float ring = RoadNetwork::ringRadius;
        constexpr float entry = RoadNetwork::entryRadius;
        const EntryGeometry in = entryGeometry(laneIn);
        const EntryGeometry out = entryGeometry(laneOut);

        // The exit is the entry of the exit lane mirrored in z, so it leaves
        // the ring heading north; it is then rotated onto the real exit.
        Route exitTail;
        exitTail.addArc({out.centre.x, -out.centre.y}, entry, -out.sweep, out.sweep);
        exitTail.addLine({-laneOut, -out.centre.y}, {-laneOut, lengthOut});

        // Circulation runs in the direction of DECREASING angle, which keeps
        // the island on the driver's left in right-hand traffic.
        float ringSweep = (-out.mergeAngle - exitRotation) - in.mergeAngle;
        while (ringSweep > 0.0f)
            ringSweep -= 360.0f;
        while (ringSweep <= -360.0f)
            ringSweep += 360.0f;

        Route route;
        route.addLine({-laneIn, -lengthIn}, {-laneIn, in.centre.y});
        route.addArc(in.centre, entry, 0.0f, in.sweep);
        route.addArc({0.0f, 0.0f}, ring, in.mergeAngle, ringSweep);
        route.append(exitTail.rotated(exitRotation));

        lineDistance = lengthIn + in.centre.y;
        mergeDistance = lineDistance + entry * glm::radians(in.sweep);
        return route;
    }

    bool pointInPolygon(glm::vec2 point, const std::vector<glm::vec2>& polygon)
    {
        bool inside = false;
        for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++)
        {
            const glm::vec2& a = polygon[i];
            const glm::vec2& b = polygon[j];
            if ((a.y > point.y) != (b.y > point.y) &&
                point.x < (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x)
                inside = !inside;
        }
        return inside;
    }
}

// ---------------------------------------------------------------------------
// Routes
// ---------------------------------------------------------------------------

void TrafficSystem::buildRoutes()
{
    routes_.clear();
    constexpr std::array<float, 2> lanes = RoadNetwork::laneOffsets;

    for (std::size_t index = 0; index < network_.junctionCount(); ++index)
    {
        const Junction& junction = network_.junctions()[index];
        for (int inArm = 0; inArm < 4; ++inArm)
        {
            if (!junction.hasArm[static_cast<std::size_t>(inArm)])
                continue;
            const int quarterTurns = (inArm - ArmSouth + 4) % 4;

            for (int outArm = 0; outArm < 4; ++outArm)
            {
                if (outArm == inArm || !junction.hasArm[static_cast<std::size_t>(outArm)])
                    continue;

                // The exit as seen from the canonical south entry.
                const int canonicalExit = (outArm - quarterTurns + 4) % 4;
                const Turn turn = canonicalExit == ArmNorth ? Turn::Straight
                                : canonicalExit == ArmWest ? Turn::Right : Turn::Left;
                const float lengthIn = junction.armLength[static_cast<std::size_t>(inArm)];
                const float lengthOut = junction.armLength[static_cast<std::size_t>(outArm)];

                if (junction.type == JunctionType::Roundabout)
                {
                    // Lane discipline: the outer lane takes the first and second
                    // exits, the inner lane the second and third, and each
                    // leaves into the same side of the road it entered from.
                    const float exitRotation = turn == Turn::Right ? 270.0f : (turn == Turn::Straight ? 0.0f : 90.0f);
                    for (int lane = 0; lane < 2; ++lane)
                    {
                        const bool allowed = (lane == 1 && turn != Turn::Left) || (lane == 0 && turn != Turn::Right);
                        if (!allowed)
                            continue;
                        float lineDistance = 0.0f;
                        float mergeDistance = 0.0f;
                        const Route route = roundaboutRoute(lanes[static_cast<std::size_t>(lane)], lanes[static_cast<std::size_t>(lane)],
                                                            exitRotation, lengthIn, lengthOut, lineDistance, mergeDistance);
                        addRoute(index, inArm, lane, outArm, lane, turn, route, lineDistance, mergeDistance);
                    }
                    continue;
                }

                // Intersections and bends: the inner lane goes straight or
                // left, the outer lane straight or right. A bend has only one
                // way to go, which both lanes follow.
                const bool bend = junction.type == JunctionType::Bend;
                const float reach = bend ? RoadNetwork::bendRadius : RoadNetwork::boxHalf;
                for (int lane = 0; lane < 2; ++lane)
                {
                    const bool allowed = bend || turn == Turn::Straight ||
                                         (turn == Turn::Right && lane == 1) || (turn == Turn::Left && lane == 0);
                    if (!allowed)
                        continue;
                    const Route route = turnRoute(turn, lanes[static_cast<std::size_t>(lane)], lengthIn, lengthOut, reach);
                    const float lineDistance = bend ? route.totalLength()
                                                     : lengthIn - RoadNetwork::stopLine - lineSetBack;
                    addRoute(index, inArm, lane, outArm, lane, turn, route, lineDistance, 0.0f);
                }
            }
        }
    }

    townEntryRoutes_.clear();
    for (std::size_t index = 0; index < routes_.size(); ++index)
    {
        if (routes_[index].entersTown)
            townEntryRoutes_.push_back(index);
    }
}

void TrafficSystem::addRoute(std::size_t junction, int inArm, int inLane, int outArm, int outLane,
                             Turn turn, const Route& canonical, float lineDistance, float mergeDistance)
{
    const Junction& j = network_.junctions()[junction];
    const int quarterTurns = (inArm - ArmSouth + 4) % 4;

    RouteInfo info;
    info.route = canonical.rotated(90.0f * static_cast<float>(quarterTurns)).translated(j.centre);
    info.junction = junction;
    info.inArm = inArm;
    info.inLane = inLane;
    info.outArm = outArm;
    info.outLane = outLane;
    info.turn = turn;
    info.entersTown = j.leavesTown[static_cast<std::size_t>(inArm)];
    info.leavesTown = j.leavesTown[static_cast<std::size_t>(outArm)];
    info.stopDistance = lineDistance;
    info.mergeDistance = mergeDistance;
    info.priorityRank = turnRank(turn) +
        ((j.type == JunctionType::GiveWayT && j.majorArm[static_cast<std::size_t>(inArm)]) ? 10 : 0);
    routes_.push_back(std::move(info));
}

void TrafficSystem::buildSuccessors()
{
    // A route's successors are the routes of the next junction that start
    // exactly where it ends, in the same lane.
    for (RouteInfo& info : routes_)
    {
        info.successors.clear();
        if (info.leavesTown)
            continue;
        const RouteSample end = info.route.sample(info.route.totalLength());
        for (std::size_t other = 0; other < routes_.size(); ++other)
        {
            if (routes_[other].junction == info.junction)
                continue;
            const RouteSample start = routes_[other].route.sample(0.0f);
            if (glm::length(start.position - end.position) < 0.05f &&
                std::abs(signedAngleDifference(start.headingDegrees, end.headingDegrees)) < 1.0f)
                info.successors.push_back(other);
        }
    }
}

bool TrafficSystem::sameStartLane(std::size_t a, std::size_t b) const
{
    const RouteInfo& first = routes_[a];
    const RouteInfo& second = routes_[b];
    return first.junction == second.junction && first.inArm == second.inArm && first.inLane == second.inLane;
}

// ---------------------------------------------------------------------------
// Shared lanes
// ---------------------------------------------------------------------------

void TrafficSystem::buildSharedSpans()
{
    std::vector<std::vector<PathSample>> samples;
    std::vector<SampleGrid> grids;
    samples.reserve(routes_.size());
    grids.reserve(routes_.size());
    for (RouteInfo& info : routes_)
    {
        info.shared.clear();
        samples.push_back(samplePath(info.route));
        grids.emplace_back(samples.back());
    }

    for (std::size_t a = 0; a < routes_.size(); ++a)
    {
        for (std::size_t b = a + 1; b < routes_.size(); ++b)
        {
            // Routes of different junctions only touch at the middle of a road.
            if (routes_[a].junction != routes_[b].junction)
                continue;

            const std::vector<PathSample>& pathA = samples[a];
            const std::vector<PathSample>& pathB = samples[b];

            // Walk route A. Wherever a sample lies on route B, facing the same
            // way, the two routes are driving along the same line. Runs of such
            // samples with a constant distance offset form one shared span.
            bool open = false;
            float from = 0.0f;
            float to = 0.0f;
            float offset = 0.0f;

            const auto closeSpan = [&]()
            {
                if (!open)
                    return;
                open = false;
                if (to - from < 1.0f)
                    return;

                const float lengthA = routes_[a].route.totalLength();
                const float lengthB = routes_[b].route.totalLength();
                const bool splits = to < lengthA - 0.5f && to + offset < lengthB - 0.5f;
                const float tail = splits ? splitTail : 0.0f;

                routes_[a].shared.push_back({b, from, to, tail, offset});
                routes_[b].shared.push_back({a, from + offset, to + offset, tail, -offset});
            };

            for (const PathSample& sampleA : pathA)
            {
                bool matched = false;
                float matchOffset = 0.0f;

                grids[b].near(sampleA.position, 0.2f, [&](int index)
                {
                    if (matched)
                        return;
                    const PathSample& sampleB = pathB[static_cast<std::size_t>(index)];
                    const glm::vec2 difference = sampleA.position - sampleB.position;
                    if (glm::dot(difference, difference) > 0.15f * 0.15f)
                        return;
                    if (std::abs(signedAngleDifference(sampleA.headingDegrees, sampleB.headingDegrees)) > 3.0f)
                        return;

                    // Correct for the two sample grids not lining up exactly.
                    const float yaw = glm::radians(sampleB.headingDegrees);
                    const glm::vec2 forwardB {std::sin(yaw), std::cos(yaw)};
                    matchOffset = sampleB.distance + glm::dot(difference, forwardB) - sampleA.distance;
                    matched = true;
                });

                if (!matched)
                {
                    closeSpan();
                    continue;
                }

                if (open && std::abs(matchOffset - offset) < 0.2f)
                {
                    to = sampleA.distance;
                }
                else
                {
                    closeSpan();
                    open = true;
                    from = sampleA.distance;
                    to = sampleA.distance;
                    offset = matchOffset;
                }
            }
            closeSpan();
        }
    }
}

// ---------------------------------------------------------------------------
// Conflict zones
// ---------------------------------------------------------------------------

void TrafficSystem::buildConflicts()
{
    conflicts_.clear();
    for (RouteInfo& info : routes_)
        info.conflicts.clear();

    const glm::vec2 inflated {bodyHalfWidth + conflictMargin, bodyHalfLength + conflictMargin};

    // Only the stretch of each route near its junction is searched.
    std::vector<std::vector<PathSample>> samples;
    std::vector<SampleGrid> grids;
    samples.reserve(routes_.size());
    grids.reserve(routes_.size());
    for (const RouteInfo& info : routes_)
    {
        const Junction& junction = network_.junctions()[info.junction];
        const float radius = junction.type == JunctionType::Roundabout ? 34.0f : 28.0f;
        std::vector<PathSample> nearMiddle;
        for (const PathSample& sample : samplePath(info.route))
        {
            if (glm::length(sample.position - junction.centre) < radius)
                nearMiddle.push_back(sample);
        }
        samples.push_back(std::move(nearMiddle));
        grids.emplace_back(samples.back());
    }

    for (std::size_t a = 0; a < routes_.size(); ++a)
    {
        for (std::size_t b = a + 1; b < routes_.size(); ++b)
        {
            const RouteInfo& infoA = routes_[a];
            const RouteInfo& infoB = routes_[b];
            if (infoA.junction != infoB.junction)
                continue;

            // Routes from the same lane queue one behind the other and then
            // split; following keeps them apart, and they never meet again.
            if (sameStartLane(a, b))
                continue;

            const std::vector<PathSample>& pathA = samples[a];
            const std::vector<PathSample>& pathB = samples[b];
            const std::size_t countB = pathB.size();
            if (pathA.empty() || pathB.empty())
                continue;

            // Two positions that both lie on a lane the routes share are the
            // follower-and-leader case, not a conflict.
            const auto following = [&infoA, b](float distanceA, float distanceB)
            {
                for (const SharedSpan& span : infoA.shared)
                {
                    if (span.other != b)
                        continue;
                    if (distanceA >= span.from && distanceA <= span.to + span.tail &&
                        distanceB >= span.from + span.offset &&
                        distanceB <= span.to + span.tail + span.offset)
                        return true;
                }
                return false;
            };

            // Mark every pair of centre positions at which the two bodies touch.
            std::unordered_set<std::uint64_t> touching;
            for (std::size_t i = 0; i < pathA.size(); ++i)
            {
                const OrientedBox boxA = makeOrientedBox(pathA[i].position, pathA[i].headingDegrees, inflated);
                grids[b].near(pathA[i].position, 6.0f, [&](int index)
                {
                    const std::size_t j = static_cast<std::size_t>(index);
                    const glm::vec2 between = pathA[i].position - pathB[j].position;
                    if (glm::dot(between, between) > 36.0f)
                        return;
                    if (following(pathA[i].distance, pathB[j].distance))
                        return;
                    const OrientedBox boxB = makeOrientedBox(pathB[j].position, pathB[j].headingDegrees, inflated);
                    if (boxesOverlap(boxA, boxB))
                        touching.insert(static_cast<std::uint64_t>(i) * countB + j);
                });
            }
            if (touching.empty())
                continue;

            // Each connected patch of touching pairs is one conflict zone. Its
            // extent on each route is the interval of centre positions involved.
            std::unordered_set<std::uint64_t> visited;
            std::vector<std::uint64_t> stack;
            for (const std::uint64_t start : touching)
            {
                if (visited.count(start) != 0)
                    continue;

                std::size_t lowA = pathA.size(), highA = 0, lowB = countB, highB = 0;
                stack.assign(1, start);
                visited.insert(start);
                while (!stack.empty())
                {
                    const std::uint64_t cell = stack.back();
                    stack.pop_back();
                    const std::size_t i = static_cast<std::size_t>(cell / countB);
                    const std::size_t j = static_cast<std::size_t>(cell % countB);
                    lowA = std::min(lowA, i);
                    highA = std::max(highA, i);
                    lowB = std::min(lowB, j);
                    highB = std::max(highB, j);

                    for (int di = -1; di <= 1; ++di)
                    {
                        for (int dj = -1; dj <= 1; ++dj)
                        {
                            const long long ni = static_cast<long long>(i) + di;
                            const long long nj = static_cast<long long>(j) + dj;
                            if (ni < 0 || nj < 0 || ni >= static_cast<long long>(pathA.size()) ||
                                nj >= static_cast<long long>(countB))
                                continue;
                            const std::uint64_t next = static_cast<std::uint64_t>(ni) * countB + static_cast<std::uint64_t>(nj);
                            if (touching.count(next) != 0 && visited.insert(next).second)
                                stack.push_back(next);
                        }
                    }
                }

                Conflict conflict;
                conflict.route = {a, b};
                conflict.in = {pathA[lowA].distance - sampleStep, pathB[lowB].distance - sampleStep};
                conflict.out = {pathA[highA].distance + sampleStep, pathB[highB].distance + sampleStep};

                const Junction& junction = network_.junctions()[infoA.junction];
                if (junction.type == JunctionType::Roundabout)
                {
                    // Traffic already on the ring has priority over traffic entering.
                    const bool circulatingA = conflict.in[0] > infoA.mergeDistance + 0.5f;
                    const bool circulatingB = conflict.in[1] > infoB.mergeDistance + 0.5f;
                    conflict.prioritySide = circulatingA == circulatingB ? -1 : (circulatingA ? 0 : 1);
                }
                else
                {
                    conflict.prioritySide = infoA.priorityRank > infoB.priorityRank ? 0
                                          : (infoB.priorityRank > infoA.priorityRank ? 1 : -1);
                }

                const std::size_t index = conflicts_.size();
                conflicts_.push_back(conflict);
                routes_[a].conflicts.push_back({index, 0});
                routes_[b].conflicts.push_back({index, 1});
            }
        }
    }

    claimCounts_.assign(conflicts_.size() * 2, 0);
}

void TrafficSystem::finishRoutes()
{
    for (RouteInfo& info : routes_)
    {
        const Junction& junction = network_.junctions()[info.junction];

        // Wait before the first conflict zone, whatever the painted line says,
        // so a car standing at the line can never be touched by crossing traffic.
        float firstZone = std::numeric_limits<float>::max();
        float lastZone = 0.0f;
        for (const ConflictRef& ref : info.conflicts)
        {
            firstZone = std::min(firstZone, conflicts_[ref.conflict].in[ref.side]);
            lastZone = std::max(lastZone, conflicts_[ref.conflict].out[ref.side]);
        }

        // Nothing to cross and no signal (a bend, or a free-flowing turn):
        // the car simply drives through.
        info.needsCommit = !info.conflicts.empty() || junction.isSignalised();
        if (!info.needsCommit)
            info.stopDistance = info.route.totalLength();
        else
            info.stopDistance = std::min(info.stopDistance, firstZone - 0.4f);
        info.junctionExit = lastZone > 0.0f ? lastZone : std::min(info.stopDistance + 12.0f, info.route.totalLength());

        // Comfortable cornering speed at every metre: v = sqrt(a * r).
        const int metres = static_cast<int>(std::ceil(info.route.totalLength())) + 1;
        info.curveSpeed.assign(static_cast<std::size_t>(metres), 99.0f);
        for (int metre = 0; metre < metres; ++metre)
        {
            const float curvature = info.route.sample(static_cast<float>(metre)).curvature;
            if (curvature > 1.0e-4f)
                info.curveSpeed[static_cast<std::size_t>(metre)] = std::sqrt(lateralAcceleration / curvature);
        }
    }
}

std::vector<StopMarking> TrafficSystem::stopMarkings() const
{
    // One line per approach lane that has to wait: the earliest stop of all
    // the routes leaving from that lane (they share it up to the line).
    std::vector<StopMarking> markings;
    std::vector<bool> done(routes_.size(), false);
    for (std::size_t index = 0; index < routes_.size(); ++index)
    {
        const RouteInfo& info = routes_[index];
        if (done[index] || !info.needsCommit)
            continue;

        const Junction& junction = network_.junctions()[info.junction];
        const bool major = junction.type == JunctionType::GiveWayT &&
                           junction.majorArm[static_cast<std::size_t>(info.inArm)];
        float stop = info.stopDistance;
        for (std::size_t other = index; other < routes_.size(); ++other)
        {
            if (sameStartLane(index, other))
            {
                stop = std::min(stop, routes_[other].stopDistance);
                done[other] = true;
            }
        }
        if (major)
            continue;   // the road with priority has no line to wait at

        const RouteSample sample = info.route.sample(stop + lineSetBack);
        const float yaw = glm::radians(sample.headingDegrees);
        markings.push_back({{sample.position.x, sample.position.z}, {std::sin(yaw), std::cos(yaw)},
                            !junction.isSignalised()});
    }
    return markings;
}

// ---------------------------------------------------------------------------
// --plot
// ---------------------------------------------------------------------------

std::string TrafficSystem::networkReport() const
{
    std::string output;
    char line[200];
    std::snprintf(line, sizeof(line), "%zu junctions, %zu routes, %zu conflict zones, %zu town entries\n",
                  network_.junctionCount(), routes_.size(), conflicts_.size(), townEntryRoutes_.size());
    output += line;

    for (std::size_t junction = 0; junction < network_.junctionCount(); ++junction)
    {
        const Junction& j = network_.junctions()[junction];
        std::size_t zones = 0;
        for (const Conflict& conflict : conflicts_)
            zones += routes_[conflict.route[0]].junction == junction ? 1 : 0;
        std::snprintf(line, sizeof(line), "\n%s (%s) at (%.0f, %.0f): %zu conflict zones\n",
                      j.name.c_str(), typeName(j.type), j.centre.x, j.centre.y, zones);
        output += line;

        for (std::size_t index = 0; index < routes_.size(); ++index)
        {
            const RouteInfo& info = routes_[index];
            if (info.junction != junction)
                continue;
            std::snprintf(line, sizeof(line),
                "  %3zu %s%s -> %s%s %-8s length %6.1f  stop %6.1f  zones %2zu  next %zu%s%s\n",
                index, armName(info.inArm), info.inLane == 0 ? "i" : "o",
                armName(info.outArm), info.outLane == 0 ? "i" : "o", turnName(info.turn),
                info.route.totalLength(), info.needsCommit ? info.stopDistance : -1.0f,
                info.conflicts.size(), info.successors.size(),
                info.entersTown ? "  (from out of town)" : "", info.leavesTown ? "  (out of town)" : "");
            output += line;
        }
    }
    return output;
}

bool TrafficSystem::writeNetworkImage(const std::string& path) const
{
    // Half a metre per pixel over 600 m x 600 m, north up.
    constexpr int size = 1200;
    constexpr float metresPerPixel = 0.5f;
    std::vector<unsigned char> pixels(static_cast<std::size_t>(size) * size * 3, 0);
    for (std::size_t index = 0; index < pixels.size(); index += 3)
    {
        pixels[index] = 22;
        pixels[index + 1] = 30;
        pixels[index + 2] = 26;
    }

    const auto plot = [&pixels](glm::vec2 world, std::array<unsigned char, 3> color, int radius)
    {
        const int cx = static_cast<int>(std::lround(world.x / metresPerPixel)) + size / 2;
        const int cy = size / 2 - static_cast<int>(std::lround(world.y / metresPerPixel));
        for (int dy = -radius; dy <= radius; ++dy)
        {
            for (int dx = -radius; dx <= radius; ++dx)
            {
                const int x = cx + dx;
                const int y = cy + dy;
                if (x < 0 || y < 0 || x >= size || y >= size)
                    continue;
                unsigned char* pixel = &pixels[(static_cast<std::size_t>(y) * size + static_cast<std::size_t>(x)) * 3];
                pixel[0] = color[0];
                pixel[1] = color[1];
                pixel[2] = color[2];
            }
        }
    };
    const auto polyline = [&plot](const std::vector<glm::vec2>& points, std::array<unsigned char, 3> color, bool closed)
    {
        for (std::size_t index = 0; index < points.size(); ++index)
        {
            if (index + 1 == points.size() && !closed)
                break;
            const glm::vec2 a = points[index];
            const glm::vec2 b = points[(index + 1) % points.size()];
            const int steps = std::max(1, static_cast<int>(glm::length(b - a) / 0.25f));
            for (int step = 0; step <= steps; ++step)
                plot(glm::mix(a, b, static_cast<float>(step) / static_cast<float>(steps)), color, 0);
        }
    };

    // Kerbs of the city blocks, and the islands.
    for (std::size_t block = 0; block < network_.blockCount(); ++block)
        polyline(network_.blockOutline(block, 0.0f), {150, 150, 150}, true);
    for (std::size_t junction = 0; junction < network_.junctionCount(); ++junction)
    {
        const Junction& j = network_.junctions()[junction];
        if (j.type != JunctionType::Roundabout)
            continue;
        for (int arm = 0; arm < 4; ++arm)
            polyline(network_.splitterIsland(junction, arm), {150, 150, 150}, true);
        std::vector<glm::vec2> island;
        for (int step = 0; step < 64; ++step)
        {
            const float angle = static_cast<float>(step) / 64.0f * 6.2831853f;
            island.push_back(j.centre + RoadNetwork::islandRadius * glm::vec2{std::cos(angle), std::sin(angle)});
        }
        polyline(island, {90, 170, 90}, true);
    }

    // Every route: inner lanes cyan, outer lanes orange.
    for (const RouteInfo& info : routes_)
    {
        const std::array<unsigned char, 3> color = info.inLane == 0
            ? std::array<unsigned char, 3>{70, 200, 230} : std::array<unsigned char, 3>{240, 160, 60};
        for (float distance = 0.0f; distance <= info.route.totalLength(); distance += 0.25f)
        {
            const glm::vec3 p = info.route.sample(distance).position;
            plot({p.x, p.z}, color, 0);
        }
    }

    // Stop lines white, conflict-zone middles red.
    for (const StopMarking& marking : stopMarkings())
        plot(marking.position, {255, 255, 255}, 1);
    for (const Conflict& conflict : conflicts_)
    {
        const glm::vec3 p = routes_[conflict.route[0]].route.sample(0.5f * (conflict.in[0] + conflict.out[0])).position;
        plot({p.x, p.z}, {235, 50, 50}, 1);
    }

    return stbi_write_png(path.c_str(), size, size, 3, pixels.data(), size * 3) != 0;
}

// ---------------------------------------------------------------------------
// --self-test
// ---------------------------------------------------------------------------

bool TrafficSystem::selfTest(std::string& report) const
{
    report.clear();
    bool passed = true;
    std::size_t checks = 0;
    std::size_t failures = 0;

    const auto fail = [&report, &passed, &failures](const std::string& message)
    {
        if (++failures <= 40)
            report += "FAIL: " + message + "\n";
        passed = false;
    };
    const auto routeName = [this](std::size_t index)
    {
        const RouteInfo& info = routes_[index];
        return network_.junctions()[info.junction].name + " route " + std::to_string(index) + " " +
               armName(info.inArm) + (info.inLane == 0 ? "i" : "o") + "->" +
               armName(info.outArm) + (info.outLane == 0 ? "i" : "o");
    };

    // Four roads into town, two lanes each.
    std::size_t entryLanes = 0;
    for (std::size_t index = 0; index < townEntryRoutes_.size(); ++index)
    {
        bool firstOfLane = true;
        for (std::size_t earlier = 0; earlier < index; ++earlier)
            firstOfLane = firstOfLane && !sameStartLane(townEntryRoutes_[earlier], townEntryRoutes_[index]);
        entryLanes += firstOfLane ? 1 : 0;
    }
    if (routes_.size() < 40 || entryLanes != 8)
        fail("unexpected network size: " + std::to_string(routes_.size()) + " routes, " +
             std::to_string(entryLanes) + " lanes into town (expected 8)");
    ++checks;

    // Everything a car's body could hit: raised blocks, splitter islands and
    // the roundabout islands.
    std::vector<std::vector<glm::vec2>> obstacles;
    for (std::size_t block = 0; block < network_.blockCount(); ++block)
        obstacles.push_back(network_.blockOutline(block, 0.0f));
    for (std::size_t junction = 0; junction < network_.junctionCount(); ++junction)
    {
        if (network_.junctions()[junction].type != JunctionType::Roundabout)
            continue;
        for (int arm = 0; arm < 4; ++arm)
            obstacles.push_back(network_.splitterIsland(junction, arm));
    }

    std::vector<std::size_t> predecessors(routes_.size(), 0);
    for (const RouteInfo& info : routes_)
    {
        for (std::size_t next : info.successors)
            ++predecessors[next];
    }

    for (std::size_t index = 0; index < routes_.size(); ++index)
    {
        const RouteInfo& info = routes_[index];
        const std::string name = routeName(index);
        const float length = info.route.totalLength();
        if (length <= 0.0f)
        {
            fail(name + " has zero length");
            continue;
        }

        // Every route begins where others end (or at the edge of town), and
        // ends where others begin (or at the edge of town): lanes join up.
        if (!info.entersTown && predecessors[index] == 0)
            fail(name + " cannot be reached: no route ends where it starts");
        if (!info.leavesTown && info.successors.empty())
            fail(name + " is a dead end: no route starts where it ends");
        checks += 2;

        // Walking the route in small steps must never jump in position or in
        // heading: that is what proves the segments actually join up.
        constexpr float step = 0.05f;
        RouteSample previous = info.route.sample(0.0f);
        for (float distance = step; distance <= length; distance += step)
        {
            const RouteSample current = info.route.sample(distance);
            if (glm::length(current.position - previous.position) > step * 1.5f + 0.001f)
            {
                fail(name + " jumps in position at s = " + std::to_string(distance));
                break;
            }
            if (std::abs(signedAngleDifference(current.headingDegrees, previous.headingDegrees)) > 6.0f)
            {
                fail(name + " jumps in heading at s = " + std::to_string(distance));
                break;
            }
            previous = current;
        }
        checks += 2;

        // The car body, anywhere on the route, stays on the road: clear of
        // every kerb, splitter island and roundabout island.
        const Junction& junction = network_.junctions()[info.junction];
        bool clear = true;
        for (float distance = 0.0f; distance <= length && clear; distance += 0.5f)
        {
            const RouteSample sample = info.route.sample(distance);
            const glm::vec2 centre {sample.position.x, sample.position.z};
            const float yaw = glm::radians(sample.headingDegrees);
            const glm::vec2 forward {std::sin(yaw), std::cos(yaw)};
            const glm::vec2 side {forward.y, -forward.x};
            for (float along : {-bodyHalfLength, 0.0f, bodyHalfLength})
            {
                for (float across : {-bodyHalfWidth, bodyHalfWidth})
                {
                    const glm::vec2 point = centre + forward * along + side * across;
                    for (const std::vector<glm::vec2>& obstacle : obstacles)
                    {
                        if (pointInPolygon(point, obstacle))
                        {
                            fail(name + " runs over a kerb or island at s = " + std::to_string(distance));
                            clear = false;
                            break;
                        }
                    }
                    if (clear && junction.type == JunctionType::Roundabout &&
                        glm::length(point - junction.centre) < RoadNetwork::islandRadius + 0.3f)
                    {
                        fail(name + " runs onto the roundabout island at s = " + std::to_string(distance));
                        clear = false;
                    }
                    if (!clear)
                        break;
                }
                if (!clear)
                    break;
            }
        }
        ++checks;

        // A car waiting at its line must be clear of every conflict zone, or
        // crossing traffic could touch it while it waits.
        for (const ConflictRef& ref : info.conflicts)
        {
            if (info.stopDistance >= conflicts_[ref.conflict].in[ref.side])
                fail(name + " waits inside conflict zone " + std::to_string(ref.conflict));
            ++checks;
        }
        if (info.needsCommit && info.stopDistance < 5.0f)
            fail(name + " stops implausibly early (s = " + std::to_string(info.stopDistance) + ")");
        ++checks;
    }

    // The whole network is one piece: from any lane a car can reach any
    // other, counting a drive out of town as re-entering on any road in.
    {
        const auto reachable = [this](bool forwards)
        {
            std::vector<bool> seen(routes_.size(), false);
            std::deque<std::size_t> queue {0};
            seen[0] = true;
            while (!queue.empty())
            {
                const std::size_t current = queue.front();
                queue.pop_front();
                for (std::size_t other = 0; other < routes_.size(); ++other)
                {
                    const RouteInfo& from = forwards ? routes_[current] : routes_[other];
                    const std::size_t to = forwards ? other : current;
                    const bool linked =
                        std::find(from.successors.begin(), from.successors.end(), to) != from.successors.end() ||
                        (from.leavesTown && routes_[to].entersTown);
                    if (linked && !seen[other])
                    {
                        seen[other] = true;
                        queue.push_back(other);
                    }
                }
            }
            return static_cast<std::size_t>(std::count(seen.begin(), seen.end(), true));
        };
        if (reachable(true) != routes_.size() || reachable(false) != routes_.size())
            fail("the network is not strongly connected");
        ++checks;
    }

    // Every conflict must be listed by both of its routes, once each, on
    // opposite sides, and only ever pair routes of the same junction.
    for (std::size_t index = 0; index < conflicts_.size(); ++index)
    {
        const Conflict& conflict = conflicts_[index];
        const std::string name = "conflict " + std::to_string(index);
        if (routes_[conflict.route[0]].junction != routes_[conflict.route[1]].junction)
            fail(name + " pairs routes of different junctions");
        for (int side = 0; side < 2; ++side)
        {
            const RouteInfo& info = routes_[conflict.route[static_cast<std::size_t>(side)]];
            const auto listed = std::count_if(info.conflicts.begin(), info.conflicts.end(),
                [index, side](const ConflictRef& ref) { return ref.conflict == index && ref.side == side; });
            if (listed != 1)
                fail(name + " is not listed exactly once by route " + std::to_string(conflict.route[static_cast<std::size_t>(side)]));
            if (!(conflict.in[static_cast<std::size_t>(side)] < conflict.out[static_cast<std::size_t>(side)]))
                fail(name + " has an empty interval");
        }
        checks += 3;
    }

    // At the signalised junctions a left turn crosses the opposing straight
    // lanes, so that pair must exist, with the straight-on car given priority.
    for (std::size_t junction = 0; junction < network_.junctionCount(); ++junction)
    {
        if (network_.junctions()[junction].type != JunctionType::SignalCross)
            continue;
        bool found = false;
        for (const Conflict& conflict : conflicts_)
        {
            for (int side = 0; side < 2; ++side)
            {
                const RouteInfo& left = routes_[conflict.route[static_cast<std::size_t>(side)]];
                const RouteInfo& other = routes_[conflict.route[static_cast<std::size_t>(1 - side)]];
                if (left.junction == junction && left.turn == Turn::Left && other.turn == Turn::Straight &&
                    other.inArm == (left.inArm + 2) % 4)
                {
                    found = true;
                    if (conflict.prioritySide != 1 - side)
                        fail("a left turn does not give way to the opposing straight-on car");
                }
            }
        }
        if (!found)
            fail(network_.junctions()[junction].name + ": left turn / opposing straight conflict not found");
        ++checks;
    }

    // No two conflicting signal movements may run together unless one clearly
    // gives way (or both are left turns, which go first come, first served).
    // Movements on crossing axes must never be allowed at the same time at all.
    for (int phaseIndex = 0; phaseIndex < 8; ++phaseIndex)
    {
        const auto phase = static_cast<TrafficPhase>(phaseIndex);
        for (const Conflict& conflict : conflicts_)
        {
            const RouteInfo& infoA = routes_[conflict.route[0]];
            const RouteInfo& infoB = routes_[conflict.route[1]];
            if (!network_.junctions()[infoA.junction].isSignalised())
                continue;

            const bool goA = phaseSignal(phase, infoA.inArm, infoA.turn) != SignalState::Red;
            const bool goB = phaseSignal(phase, infoB.inArm, infoB.turn) != SignalState::Red;
            if (!goA || !goB)
                continue;

            const std::string pair = network_.junctions()[infoA.junction].name + " " +
                                     armName(infoA.inArm) + " " + turnName(infoA.turn) + " / " +
                                     armName(infoB.inArm) + " " + turnName(infoB.turn);
            if (armIsNorthSouth(infoA.inArm) != armIsNorthSouth(infoB.inArm))
                fail("crossing axes allowed together: " + pair);
            else if (conflict.prioritySide < 0 && !(infoA.turn == Turn::Left && infoB.turn == Turn::Left))
                fail("equal-priority conflict allowed together: " + pair);
            ++checks;
        }
    }

    // Route helpers: reversing twice gives the route back, and a reversed route
    // starts where the original ends, facing the other way.
    for (std::size_t index = 0; index < routes_.size(); index += 7)
    {
        const Route& route = routes_[index].route;
        const Route back = route.reversed();
        const Route again = back.reversed();
        const float length = route.totalLength();

        const RouteSample end = route.sample(length);
        const RouteSample backStart = back.sample(0.0f);
        if (glm::length(end.position - backStart.position) > 0.01f ||
            std::abs(signedAngleDifference(backStart.headingDegrees, end.headingDegrees + 180.0f)) > 0.5f)
            fail("reversed() of route " + std::to_string(index) + " does not start at the old end");

        for (float distance = 0.0f; distance <= length; distance += 1.7f)
        {
            if (glm::length(again.sample(distance).position - route.sample(distance).position) > 0.01f)
            {
                fail("reversed() twice does not give route " + std::to_string(index) + " back");
                break;
            }
        }

        const Route moved = route.translated({100.0f, -40.0f});
        const glm::vec3 shift = moved.sample(length * 0.5f).position - route.sample(length * 0.5f).position;
        if (glm::length(shift - glm::vec3{100.0f, 0.0f, -40.0f}) > 0.01f)
            fail("translated() of route " + std::to_string(index) + " is wrong");
        checks += 3;
    }

    if (failures > 40)
        report += "... and " + std::to_string(failures - 40) + " more failures\n";
    if (passed)
        report = "All " + std::to_string(checks) + " network, route, conflict and signal checks passed (" +
                 std::to_string(network_.junctionCount()) + " junctions, " + std::to_string(routes_.size()) +
                 " routes, " + std::to_string(conflicts_.size()) + " conflict zones).\n";
    return passed;
}
