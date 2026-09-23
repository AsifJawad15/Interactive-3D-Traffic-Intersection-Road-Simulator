// Everything TrafficSystem works out once, before the first vehicle moves:
// the routes, where routes share a lane, where they conflict, and who has
// priority. Also the --self-test and --plot checks of that geometry.

#include "Simulation.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace
{
    constexpr float sampleStep = 0.25f;

    // Conflicts are only searched for near the middle; farther out every route
    // is either alone in its lane or sharing it with a route it follows.
    constexpr float junctionSearchRadius = 20.0f;

    // Safety margin added around every body when conflict zones are measured.
    constexpr float conflictMargin = 0.30f;

    // How far past a split two routes keep watching each other as leader and
    // follower, while the cars are still close together.
    constexpr float splitTail = 5.0f;

    // Comfortable sideways acceleration in a corner, m/s^2.
    constexpr float lateralAcceleration = 2.0f;

    // Body used to measure conflicts: the largest vehicle in the scene.
    constexpr float conflictHalfWidth = 0.94f;
    constexpr float conflictHalfLength = 2.03f;

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

    bool laneIsNorthSouth(Lane lane)
    {
        return lane == Lane::Northbound || lane == Lane::Southbound;
    }

    // Signal priority: straight on beats a right turn, which beats a left turn.
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

    const char* laneName(Lane lane)
    {
        switch (lane)
        {
        case Lane::Northbound: return "N";
        case Lane::Southbound: return "S";
        case Lane::Eastbound: return "E";
        default: return "W";
        }
    }
}

// ---------------------------------------------------------------------------
// Routes
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
        addRouteFamily(route, Turn::Straight, false, signalStop, 0.0f);
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
        addRouteFamily(route, Turn::Right, false, signalStop, 0.0f);
    }

    // Left turn: the wide arc that crosses the middle of the intersection.
    // The same tangency argument gives radius = edge + lane.
    {
        const float radius = edge + lane;
        Route route;
        route.addLine({-lane, -rim}, {-lane, -edge});
        route.addArc({edge, -edge}, radius, 180.0f, -90.0f);
        route.addLine({edge, lane}, {rim, lane});
        addRouteFamily(route, Turn::Left, false, signalStop, 0.0f);
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

    // First exit = right turn, second = straight on, third = left turn.
    static constexpr Turn exitTurns[3] = {Turn::Right, Turn::Straight, Turn::Left};

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

        addRouteFamily(route, exitTurns[exit - 1], true, giveWay, mergeDistance);
    }
}

void TrafficSystem::addRouteFamily(
    const Route& base, Turn turn, bool roundabout, float lineDistance, float mergeDistance)
{
    for (int rotation = 0; rotation < 4; ++rotation)
    {
        RouteInfo info;
        info.route = rotation == 0 ? base : base.rotated(90.0f * static_cast<float>(rotation));
        info.lane = laneForRotation(rotation);
        info.turn = turn;
        info.roundabout = roundabout;
        info.stopDistance = lineDistance;
        info.mergeDistance = mergeDistance;
        routes_.push_back(std::move(info));
    }
}

// ---------------------------------------------------------------------------
// Shared lanes
// ---------------------------------------------------------------------------

void TrafficSystem::buildSharedSpans()
{
    std::vector<std::vector<PathSample>> samples;
    samples.reserve(routes_.size());
    for (RouteInfo& info : routes_)
    {
        info.shared.clear();
        samples.push_back(samplePath(info.route));
    }

    for (std::size_t a = 0; a < routes_.size(); ++a)
    {
        for (std::size_t b = a + 1; b < routes_.size(); ++b)
        {
            if (routes_[a].roundabout != routes_[b].roundabout)
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

                for (const PathSample& sampleB : pathB)
                {
                    const glm::vec2 difference = sampleA.position - sampleB.position;
                    if (glm::dot(difference, difference) > 0.15f * 0.15f)
                        continue;
                    if (std::abs(signedAngleDifference(sampleA.headingDegrees, sampleB.headingDegrees)) > 3.0f)
                        continue;

                    // Correct for the two sample grids not lining up exactly.
                    const float yaw = glm::radians(sampleB.headingDegrees);
                    const glm::vec2 forwardB {std::sin(yaw), std::cos(yaw)};
                    matchOffset = sampleB.distance + glm::dot(difference, forwardB) - sampleA.distance;
                    matched = true;
                    break;
                }

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

    const glm::vec2 inflated {conflictHalfWidth + conflictMargin, conflictHalfLength + conflictMargin};

    // Only the stretch of each route near the middle is searched.
    std::vector<std::vector<PathSample>> samples;
    samples.reserve(routes_.size());
    for (const RouteInfo& info : routes_)
    {
        std::vector<PathSample> nearMiddle;
        for (const PathSample& sample : samplePath(info.route))
        {
            if (glm::length(sample.position) < junctionSearchRadius)
                nearMiddle.push_back(sample);
        }
        samples.push_back(std::move(nearMiddle));
    }

    for (std::size_t a = 0; a < routes_.size(); ++a)
    {
        for (std::size_t b = a + 1; b < routes_.size(); ++b)
        {
            const RouteInfo& infoA = routes_[a];
            const RouteInfo& infoB = routes_[b];
            if (infoA.roundabout != infoB.roundabout)
                continue;

            // Routes from the same approach queue in one lane and then split;
            // following keeps them apart, and they never meet again.
            if (infoA.lane == infoB.lane)
                continue;

            const std::vector<PathSample>& pathA = samples[a];
            const std::vector<PathSample>& pathB = samples[b];
            const std::size_t countA = pathA.size();
            const std::size_t countB = pathB.size();
            if (countA == 0 || countB == 0)
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
            std::vector<unsigned char> touching(countA * countB, 0);
            bool any = false;
            for (std::size_t i = 0; i < countA; ++i)
            {
                const OrientedBox boxA = makeOrientedBox(pathA[i].position, pathA[i].headingDegrees, inflated);
                for (std::size_t j = 0; j < countB; ++j)
                {
                    const glm::vec2 between = pathA[i].position - pathB[j].position;
                    if (glm::dot(between, between) > 36.0f)
                        continue;
                    if (following(pathA[i].distance, pathB[j].distance))
                        continue;

                    const OrientedBox boxB = makeOrientedBox(pathB[j].position, pathB[j].headingDegrees, inflated);
                    if (boxesOverlap(boxA, boxB))
                    {
                        touching[i * countB + j] = 1;
                        any = true;
                    }
                }
            }
            if (!any)
                continue;

            // Each connected patch of touching pairs is one conflict zone. Its
            // extent on each route is the interval of centre positions involved.
            std::vector<unsigned char> visited(countA * countB, 0);
            std::vector<std::size_t> stack;
            for (std::size_t start = 0; start < touching.size(); ++start)
            {
                if (touching[start] == 0 || visited[start] != 0)
                    continue;

                std::size_t lowA = countA, highA = 0, lowB = countB, highB = 0;
                stack.assign(1, start);
                visited[start] = 1;
                while (!stack.empty())
                {
                    const std::size_t cell = stack.back();
                    stack.pop_back();
                    const std::size_t i = cell / countB;
                    const std::size_t j = cell % countB;
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
                            if (ni < 0 || nj < 0 || ni >= static_cast<long long>(countA) ||
                                nj >= static_cast<long long>(countB))
                                continue;
                            const std::size_t next = static_cast<std::size_t>(ni) * countB +
                                                     static_cast<std::size_t>(nj);
                            if (touching[next] != 0 && visited[next] == 0)
                            {
                                visited[next] = 1;
                                stack.push_back(next);
                            }
                        }
                    }
                }

                Conflict conflict;
                conflict.route = {a, b};
                conflict.in = {pathA[lowA].distance - sampleStep, pathB[lowB].distance - sampleStep};
                conflict.out = {pathA[highA].distance + sampleStep, pathB[highB].distance + sampleStep};

                if (!infoA.roundabout)
                {
                    const int rankA = turnRank(infoA.turn);
                    const int rankB = turnRank(infoB.turn);
                    conflict.prioritySide = rankA > rankB ? 0 : (rankB > rankA ? 1 : -1);
                }
                else
                {
                    // Traffic already on the ring has priority over traffic entering.
                    const bool circulatingA = conflict.in[0] > infoA.mergeDistance + 0.5f;
                    const bool circulatingB = conflict.in[1] > infoB.mergeDistance + 0.5f;
                    conflict.prioritySide = circulatingA == circulatingB ? -1 : (circulatingA ? 0 : 1);
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
        // Wait before the first conflict zone, whatever the painted line says,
        // so a car standing at the line can never be touched by crossing traffic.
        float firstZone = std::numeric_limits<float>::max();
        float lastZone = 0.0f;
        for (const ConflictRef& ref : info.conflicts)
        {
            firstZone = std::min(firstZone, conflicts_[ref.conflict].in[ref.side]);
            lastZone = std::max(lastZone, conflicts_[ref.conflict].out[ref.side]);
        }
        info.stopDistance = std::min(info.stopDistance, firstZone - 0.4f);
        info.junctionExit = lastZone > 0.0f ? lastZone : info.stopDistance + 12.0f;

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

// ---------------------------------------------------------------------------
// --plot
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
        if (cell == ' ' || symbol == '#' || symbol == '*')
            cell = symbol;
    };
    const auto toColumn = [](float x) { return static_cast<int>(std::lround(x)) + halfWidth; };
    const auto toRow = [](float z) { return static_cast<int>(std::lround(z * 0.5f)) + halfHeight; };

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

    // The middle of every conflict zone, as seen from its first route.
    std::size_t conflictCount = 0;
    for (const Conflict& conflict : conflicts_)
    {
        const RouteInfo& info = routes_[conflict.route[0]];
        if (info.roundabout != roundabout)
            continue;
        ++conflictCount;
        const glm::vec3 point = info.route.sample(0.5f * (conflict.in[0] + conflict.out[0])).position;
        plot(toColumn(point.x), toRow(point.z), '*');
    }

    std::string output = roundabout ? "Roundabout routes (# = island, * = conflict zone)\n"
                                    : "Signalised routes (# = island footprint, * = conflict zone)\n";
    for (int row = 0; row < rows; ++row)
    {
        output.append(&grid[static_cast<std::size_t>(row) * columns], columns);
        output += "\n";
    }

    output += std::to_string(conflictCount) + " conflict zones. Per route: stop line at s, zones, shared lanes\n";
    for (std::size_t index = 0; index < routes_.size(); ++index)
    {
        const RouteInfo& info = routes_[index];
        if (info.roundabout != roundabout)
            continue;
        char line[160];
        std::snprintf(line, sizeof(line), "  %2zu %s %-8s stop %5.1f m  exit %5.1f m  zones %2zu  shared %2zu\n",
                      index, laneName(info.lane), turnName(info.turn), info.stopDistance,
                      info.junctionExit, info.conflicts.size(), info.shared.size());
        output += line;
    }
    return output;
}

// ---------------------------------------------------------------------------
// --self-test
// ---------------------------------------------------------------------------

bool TrafficSystem::selfTest(std::string& report) const
{
    report.clear();
    bool passed = true;
    std::size_t checks = 0;

    const auto fail = [&report, &passed](const std::string& message)
    {
        report += "FAIL: " + message + "\n";
        passed = false;
    };

    if (routes_.size() != 24)
        fail("expected 24 routes (2 modes x 4 approaches x 3 choices), got " +
             std::to_string(routes_.size()));
    ++checks;

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
        checks += 2;

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
        checks += 2;

        // Circulating traffic must stay outside the raised island.
        if (info.roundabout && closestToCentre < islandRadius + 0.8f)
            fail(name + " passes too close to the island (" +
                 std::to_string(closestToCentre) + " m)");
        ++checks;

        // A car waiting at its line must be clear of every conflict zone, or
        // crossing traffic could touch it while it waits.
        for (const ConflictRef& ref : info.conflicts)
        {
            if (info.stopDistance >= conflicts_[ref.conflict].in[ref.side])
                fail(name + " waits inside conflict zone " + std::to_string(ref.conflict));
            ++checks;
        }
        if (info.stopDistance < 20.0f)
            fail(name + " stops implausibly early (s = " + std::to_string(info.stopDistance) + ")");
        ++checks;
    }

    // Every conflict must be listed by both of its routes, once each, on
    // opposite sides, and only ever pair routes of the same mode.
    std::size_t signalConflicts = 0;
    std::size_t roundaboutConflicts = 0;
    for (std::size_t index = 0; index < conflicts_.size(); ++index)
    {
        const Conflict& conflict = conflicts_[index];
        const RouteInfo& infoA = routes_[conflict.route[0]];
        const RouteInfo& infoB = routes_[conflict.route[1]];
        const std::string name = "conflict " + std::to_string(index);

        if (infoA.roundabout != infoB.roundabout)
            fail(name + " pairs a signal route with a roundabout route");
        (infoA.roundabout ? roundaboutConflicts : signalConflicts) += 1;

        for (int side = 0; side < 2; ++side)
        {
            const RouteInfo& info = side == 0 ? infoA : infoB;
            const auto listed = std::count_if(info.conflicts.begin(), info.conflicts.end(),
                [index, side](const ConflictRef& ref) { return ref.conflict == index && ref.side == side; });
            if (listed != 1)
                fail(name + " is not listed exactly once by route " + std::to_string(conflict.route[side]));
            if (!(conflict.in[side] < conflict.out[side]))
                fail(name + " has an empty interval");
        }
        checks += 3;
    }
    if (signalConflicts == 0 || roundaboutConflicts == 0)
        fail("no conflict zones found in one of the modes");
    ++checks;

    // A left turn crosses the opposing straight lane, so that pair must have
    // been found, with the straight-on car given priority.
    bool foundLeftAcrossStraight = false;
    for (const Conflict& conflict : conflicts_)
    {
        for (int side = 0; side < 2; ++side)
        {
            const RouteInfo& left = routes_[conflict.route[side]];
            const RouteInfo& other = routes_[conflict.route[1 - side]];
            if (!left.roundabout && left.turn == Turn::Left && other.turn == Turn::Straight &&
                laneIsNorthSouth(left.lane) == laneIsNorthSouth(other.lane))
            {
                foundLeftAcrossStraight = true;
                if (conflict.prioritySide != 1 - side)
                    fail("a left turn does not give way to the opposing straight-on car");
            }
        }
    }
    if (!foundLeftAcrossStraight)
        fail("the left turn / opposing straight conflict was not found");
    ++checks;

    // No two conflicting signal movements may run together unless one clearly
    // gives way (or both are left turns, which go first come, first served).
    // Movements on crossing axes must never be allowed at the same time at all.
    TrafficSystem probe;
    for (int phase = 0; phase < 8; ++phase)
    {
        probe.phase_ = static_cast<TrafficPhase>(phase);
        for (const Conflict& conflict : conflicts_)
        {
            const RouteInfo& infoA = routes_[conflict.route[0]];
            const RouteInfo& infoB = routes_[conflict.route[1]];
            if (infoA.roundabout)
                continue;

            const bool goA = probe.movementSignal(infoA.lane, infoA.turn) != SignalState::Red;
            const bool goB = probe.movementSignal(infoB.lane, infoB.turn) != SignalState::Red;
            if (!goA || !goB)
                continue;

            const std::string pair = std::string(laneName(infoA.lane)) + " " + turnName(infoA.turn) +
                                     " / " + laneName(infoB.lane) + " " + turnName(infoB.turn);
            if (laneIsNorthSouth(infoA.lane) != laneIsNorthSouth(infoB.lane))
                fail("crossing axes allowed together in phase " + probe.phaseName() + ": " + pair);
            else if (conflict.prioritySide < 0 && !(infoA.turn == Turn::Left && infoB.turn == Turn::Left))
                fail("equal-priority conflict allowed together in phase " + probe.phaseName() + ": " + pair);
            ++checks;
        }
    }

    // The two signal heads facing each other must never both be green at the
    // same time as the crossing pair.
    for (int phase = 0; phase < 8; ++phase)
    {
        probe.phase_ = static_cast<TrafficPhase>(phase);
        const bool northGreen = probe.signalFor(Lane::Northbound) == SignalState::Green ||
                                probe.leftArrowFor(Lane::Northbound) == SignalState::Green;
        const bool eastGreen = probe.signalFor(Lane::Eastbound) == SignalState::Green ||
                               probe.leftArrowFor(Lane::Eastbound) == SignalState::Green;
        if (northGreen && eastGreen)
            fail("north-south and east-west are green at the same time");
        ++checks;
    }

    // Route helpers: reversing twice gives the route back, and a reversed route
    // starts where the original ends, facing the other way.
    for (std::size_t index = 0; index < routes_.size(); index += 5)
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

    if (passed)
        report = "All " + std::to_string(checks) + " route, conflict and signal checks passed (" +
                 std::to_string(signalConflicts) + " signal and " + std::to_string(roundaboutConflicts) +
                 " roundabout conflict zones).\n";
    return passed;
}
