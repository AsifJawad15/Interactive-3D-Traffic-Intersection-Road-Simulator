// Everything TrafficSystem works out once, before the first vehicle moves:
// the routes through every junction of the city, how they chain together,
// how each size of vehicle swings through them and which it can drive at all,
// where routes share a lane, where they conflict, and who has priority. Also
// the bus line, and the --self-test and --plot checks of all that geometry.

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
#include <execution>
#include <functional>
#include <limits>
#include <numeric>
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

    // A waiting vehicle's front bumper stops this far short of the painted line.
    constexpr float lineClearance = 0.3f;

    // Trucks and buses may swing their overhangs this far over a kerb, as
    // real ones do, as long as their wheels stay on the road. Lamp posts,
    // signal poles and signs stand further back than this.
    constexpr float overhangAllowance = 0.8f;

    // The bus line: a loop of four left turns round the block between X0,
    // G, G2 and X1. Buses turn from the kerb lane into the kerb lane (a
    // bus-only movement), so they can stop at the kerb on every side.
    struct LineLeg
    {
        const char* junction;
        int inArm;
        int outArm;
    };
    constexpr std::array<LineLeg, 4> busLineLegs = {{
        {"G", ArmEast, ArmNorth},
        {"G2", ArmSouth, ArmEast},
        {"X1", ArmWest, ArmSouth},
        {"X0", ArmNorth, ArmWest}
    }};
    // Where on each leg the bus centre stands at its stop: well before the
    // queue at the junction ahead, well after the lane changes behind.
    constexpr float busStopDistance = 15.0f;

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

    // A grid of square cells over a set of points, so the points near a
    // given place are found without trying them all.
    class SampleGrid
    {
    public:
        explicit SampleGrid(const std::vector<glm::vec2>& points, float cellSize = 0.5f) : cellSize_(cellSize)
        {
            for (std::size_t index = 0; index < points.size(); ++index)
                cells_[key(points[index])].push_back(static_cast<int>(index));
        }

        template <typename Visit>
        void near(glm::vec2 point, float radius, Visit&& visit) const
        {
            const int reach = static_cast<int>(std::ceil(radius / cellSize_));
            const int cx = static_cast<int>(std::floor(point.x / cellSize_));
            const int cz = static_cast<int>(std::floor(point.y / cellSize_));
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
        float cellSize_ = 0.5f;
        std::unordered_map<std::int64_t, std::vector<int>> cells_;

        static std::int64_t pack(int x, int z)
        {
            return (static_cast<std::int64_t>(x) << 32) ^ static_cast<std::int64_t>(static_cast<std::uint32_t>(z));
        }
        std::int64_t key(glm::vec2 p) const
        {
            return pack(static_cast<int>(std::floor(p.x / cellSize_)), static_cast<int>(std::floor(p.y / cellSize_)));
        }
    };

    std::vector<glm::vec2> positionsOf(const std::vector<PathSample>& samples)
    {
        std::vector<glm::vec2> points;
        points.reserve(samples.size());
        for (const PathSample& sample : samples)
            points.push_back(sample.position);
        return points;
    }

    float signedAngleDifference(float a, float b)
    {
        float difference = a - b;
        while (difference > 180.0f) difference -= 360.0f;
        while (difference < -180.0f) difference += 360.0f;
        return difference;
    }

    glm::vec2 forwardOf(float headingDegrees)
    {
        const float yaw = glm::radians(headingDegrees);
        return {std::sin(yaw), std::cos(yaw)};
    }

    // The driver's left, for a heading: lanes of right-hand traffic lie at
    // -x when heading +z, so the left is +x there.
    glm::vec2 leftOf(float headingDegrees)
    {
        const float yaw = glm::radians(headingDegrees);
        return {std::cos(yaw), -std::sin(yaw)};
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

    // Distance from a junction centre within which its routes can meet.
    float junctionRadius(const Junction& junction);

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

    // A truck's right turn: it cannot follow the kerb round the corner (its
    // rear wheels would climb it), so it swings wide on a bigger arc from the
    // kerb lane into the far lane of the road ahead, as real trucks do.
    constexpr float wideTurnRadius = 8.0f;

    Route wideRightRoute(float lengthIn, float lengthOut)
    {
        constexpr float laneIn = RoadNetwork::laneOffsets[1];
        constexpr float laneOut = RoadNetwork::laneOffsets[0];
        const float arcStart = -laneOut - wideTurnRadius;
        Route route;
        route.addLine({-laneIn, -lengthIn}, {-laneIn, arcStart});
        route.addArc({-laneIn - wideTurnRadius, arcStart}, wideTurnRadius, 0.0f, 90.0f);
        route.addLine({-laneIn - wideTurnRadius, -laneOut}, {-lengthOut, -laneOut});
        return route;
    }

    // Where a car going straight on moves into the other lane of the road
    // ahead: from just past the far zebra to well before the middle of that
    // road (every road is at least 100 m long, so its middle is 50 m out).
    constexpr float laneChangeStart = 16.0f;
    constexpr float laneChangeEnd = 40.0f;

    // Straight on through an intersection, then across into the other lane.
    // Two opposite arcs of equal radius make the S, so the heading never
    // jumps; each covers half the length and half the sideways shift, which
    // fixes the radius: r = (l^2 + d^2) / 2d.
    Route laneChangeRoute(float laneIn, float laneOut, float lengthIn, float lengthOut)
    {
        const float half = 0.5f * (laneChangeEnd - laneChangeStart);
        const float shift = 0.5f * std::abs(laneOut - laneIn);
        const float radius = (half * half + shift * shift) / (2.0f * shift);
        const float angle = glm::degrees(std::asin(half / radius));

        Route route;
        route.addLine({-laneIn, -lengthIn}, {-laneIn, laneChangeStart});
        if (laneOut > laneIn)
        {
            // Out towards the kerb: right, then left again.
            route.addArc({-laneIn - radius, laneChangeStart}, radius, 0.0f, angle);
            route.addArc({-laneOut + radius, laneChangeEnd}, radius, 180.0f + angle, -angle);
        }
        else
        {
            // In towards the centre line: left, then right again.
            route.addArc({-laneIn + radius, laneChangeStart}, radius, 180.0f, -angle);
            route.addArc({-laneOut - radius, laneChangeEnd}, radius, -angle, angle);
        }
        route.addLine({-laneOut, laneChangeEnd}, {-laneOut, lengthOut});
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
    Route roundaboutRoute(float laneIn, float laneOut, float exitRotation, float lengthIn, float lengthOut,
                          float& lineDistance, float& mergeDistance, float& exitDistance)
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
        exitDistance = route.totalLength() - exitTail.totalLength();
        return route;
    }

    float junctionRadius(const Junction& junction)
    {
        // Intersections reach out past the end of the lane changes.
        return junction.type == JunctionType::Roundabout ? 34.0f
             : junction.isIntersection() ? laneChangeEnd + 4.0f : 28.0f;
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

    // The same crossing test, but only against the edges that span the
    // point's row: each edge is filed under every one-metre row it crosses.
    class PolygonIndex
    {
    public:
        explicit PolygonIndex(std::vector<glm::vec2> polygon) : points_(std::move(polygon))
        {
            low_ = glm::vec2(std::numeric_limits<float>::max());
            high_ = glm::vec2(std::numeric_limits<float>::lowest());
            for (const glm::vec2& point : points_)
            {
                low_ = glm::min(low_, point);
                high_ = glm::max(high_, point);
            }
            rows_.resize(static_cast<std::size_t>(std::floor((high_.y - low_.y) / rowSize)) + 1);
            for (std::size_t i = 0, j = points_.size() - 1; i < points_.size(); j = i++)
            {
                const std::size_t first = row(std::min(points_[i].y, points_[j].y));
                const std::size_t last = row(std::max(points_[i].y, points_[j].y));
                for (std::size_t r = first; r <= last; ++r)
                    rows_[r].push_back({i, j});
            }
        }

        bool contains(glm::vec2 point) const
        {
            if (point.x < low_.x || point.y < low_.y || point.x > high_.x || point.y > high_.y)
                return false;
            bool inside = false;
            for (const auto& [i, j] : rows_[row(point.y)])
            {
                const glm::vec2& a = points_[i];
                const glm::vec2& b = points_[j];
                if ((a.y > point.y) != (b.y > point.y) &&
                    point.x < (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x)
                    inside = !inside;
            }
            return inside;
        }

    private:
        static constexpr float rowSize = 1.0f;
        std::vector<glm::vec2> points_;
        glm::vec2 low_ {0.0f};
        glm::vec2 high_ {0.0f};
        std::vector<std::vector<std::pair<std::size_t, std::size_t>>> rows_;

        std::size_t row(float y) const
        {
            const float clamped = glm::clamp(y, low_.y, high_.y);
            return std::min(static_cast<std::size_t>((clamped - low_.y) / rowSize), rows_.size() - 1);
        }
    };

    // Whether a point is on the carriageway: off every raised block,
    // splitter island and roundabout island, and inside the outer kerb. With
    // `overhang`, a strip `overhangAllowance` wide behind every kerb counts
    // as road too (for the body of a truck or bus, not its wheels).
    class KerbMap
    {
    public:
        explicit KerbMap(const RoadNetwork& network)
            : edge_(network.outsideOutline(0.0f)), overhangEdge_(network.outsideOutline(overhangAllowance))
        {
            for (std::size_t block = 0; block < network.blockCount(); ++block)
            {
                blocks_.emplace_back(network.blockOutline(block, 0.0f));
                overhangBlocks_.emplace_back(network.blockOutline(block, overhangAllowance));
            }
            for (std::size_t junction = 0; junction < network.junctionCount(); ++junction)
            {
                const Junction& j = network.junctions()[junction];
                if (j.type != JunctionType::Roundabout)
                    continue;
                islands_.push_back(j.centre);
                for (int arm = 0; arm < 4; ++arm)
                {
                    if (j.hasArm[static_cast<std::size_t>(arm)])
                        splitters_.emplace_back(network.splitterIsland(junction, arm));
                }
            }
        }

        bool onRoad(glm::vec2 point, bool overhang) const
        {
            for (const PolygonIndex& block : overhang ? overhangBlocks_ : blocks_)
            {
                if (block.contains(point))
                    return false;
            }
            for (const PolygonIndex& splitter : splitters_)
            {
                if (splitter.contains(point))
                    return false;
            }
            if (!(overhang ? overhangEdge_ : edge_).contains(point))
                return false;
            const float island = RoadNetwork::islandRadius + 0.3f - (overhang ? overhangAllowance : 0.0f);
            for (const glm::vec2& centre : islands_)
            {
                if (glm::length(point - centre) < island)
                    return false;
            }
            return true;
        }

    private:
        PolygonIndex edge_;
        PolygonIndex overhangEdge_;
        std::vector<PolygonIndex> blocks_;
        std::vector<PolygonIndex> overhangBlocks_;
        std::vector<PolygonIndex> splitters_;
        std::vector<glm::vec2> islands_;
    };

    // The body of a size class at a pose, seen from above, and whether it
    // stays on the road there. Cars and vans keep their whole body off the
    // kerbs; trucks and buses keep their wheels on the road and may swing
    // their overhangs a little over a kerb.
    bool bodyOnRoad(const KerbMap& kerbs, glm::vec2 centre, float yawDegrees, SizeClass sizeClass)
    {
        const SizeClassSpec& spec = sizeClassSpec(sizeClass);
        const glm::vec2 forward = forwardOf(yawDegrees);
        const glm::vec2 left = leftOf(yawDegrees);
        const bool overhang = widthTier(sizeClass) == 1;

        const auto test = [&](float along, float across, bool wheel)
        {
            return kerbs.onRoad(centre + forward * along + left * across, overhang && !wheel);
        };

        // The outline: every half metre along both sides and across both ends.
        const int alongSteps = static_cast<int>(std::ceil(2.0f * spec.halfLength / 0.5f));
        for (int step = 0; step <= alongSteps; ++step)
        {
            const float along = -spec.halfLength + 2.0f * spec.halfLength * static_cast<float>(step) / static_cast<float>(alongSteps);
            if (!test(along, spec.halfWidth, false) || !test(along, -spec.halfWidth, false))
                return false;
        }
        const int acrossSteps = static_cast<int>(std::ceil(2.0f * spec.halfWidth / 0.5f));
        for (int step = 1; step < acrossSteps; ++step)
        {
            const float across = -spec.halfWidth + 2.0f * spec.halfWidth * static_cast<float>(step) / static_cast<float>(acrossSteps);
            if (!test(spec.halfLength, across, false) || !test(-spec.halfLength, across, false))
                return false;
        }

        if (overhang)
        {
            // The tyres: a patch round each wheel of both axles.
            const float track = spec.halfWidth - 0.12f;
            for (float axle : {spec.frontAxle, spec.frontAxle - spec.wheelBase})
            {
                for (float side : {-1.0f, 1.0f})
                {
                    for (float along : {-0.45f, 0.45f})
                    {
                        for (float across : {-0.15f, 0.15f})
                        {
                            if (!test(axle + along, side * track + across, true))
                                return false;
                        }
                    }
                }
            }
        }
        return true;
    }

    // The part of the line through `origin` along the unit `direction` that
    // lies inside the box: from origin + t0 * direction to origin + t1 * direction.
    bool clipLine(const OrientedBox& box, glm::vec2 origin, glm::vec2 direction, float& t0, float& t1)
    {
        const glm::vec2 across {box.forward.y, -box.forward.x};
        const glm::vec2 relative = origin - box.centre;
        const std::array<float, 2> start = {glm::dot(relative, across), glm::dot(relative, box.forward)};
        const std::array<float, 2> step = {glm::dot(direction, across), glm::dot(direction, box.forward)};
        const std::array<float, 2> half = {box.halfExtents.x, box.halfExtents.y};
        t0 = std::numeric_limits<float>::lowest();
        t1 = std::numeric_limits<float>::max();
        for (std::size_t axis = 0; axis < 2; ++axis)
        {
            if (std::abs(step[axis]) < 1.0e-6f)
            {
                if (std::abs(start[axis]) > half[axis])
                    return false;
                continue;
            }
            float a = (-half[axis] - start[axis]) / step[axis];
            float b = (half[axis] - start[axis]) / step[axis];
            if (a > b)
                std::swap(a, b);
            t0 = std::max(t0, a);
            t1 = std::min(t1, b);
        }
        return t0 <= t1;
    }

    // The strongly connected piece of a route graph with the most routes
    // (Kosaraju: finish order forwards, then components backwards).
    std::vector<bool> largestStrongComponent(const std::vector<std::vector<std::size_t>>& successors,
                                             const std::vector<bool>& present)
    {
        const std::size_t count = successors.size();
        std::vector<std::vector<std::size_t>> predecessors(count);
        for (std::size_t node = 0; node < count; ++node)
        {
            if (!present[node])
                continue;
            for (std::size_t next : successors[node])
            {
                if (present[next])
                    predecessors[next].push_back(node);
            }
        }

        std::vector<bool> seen(count, false);
        std::vector<std::size_t> order;
        for (std::size_t start = 0; start < count; ++start)
        {
            if (!present[start] || seen[start])
                continue;
            // Iterative depth-first search recording finish order.
            std::vector<std::pair<std::size_t, std::size_t>> stack {{start, 0}};
            seen[start] = true;
            while (!stack.empty())
            {
                auto& [node, next] = stack.back();
                if (next < successors[node].size())
                {
                    const std::size_t child = successors[node][next++];
                    if (present[child] && !seen[child])
                    {
                        seen[child] = true;
                        stack.push_back({child, 0});
                    }
                }
                else
                {
                    order.push_back(node);
                    stack.pop_back();
                }
            }
        }

        std::vector<int> component(count, -1);
        std::vector<std::size_t> sizes;
        for (auto node = order.rbegin(); node != order.rend(); ++node)
        {
            if (component[*node] >= 0)
                continue;
            const int id = static_cast<int>(sizes.size());
            std::size_t size = 0;
            std::vector<std::size_t> stack {*node};
            component[*node] = id;
            while (!stack.empty())
            {
                const std::size_t current = stack.back();
                stack.pop_back();
                ++size;
                for (std::size_t previous : predecessors[current])
                {
                    if (component[previous] < 0)
                    {
                        component[previous] = id;
                        stack.push_back(previous);
                    }
                }
            }
            sizes.push_back(size);
        }

        std::vector<bool> result(count, false);
        if (sizes.empty())
            return result;
        const int largest = static_cast<int>(std::max_element(sizes.begin(), sizes.end()) - sizes.begin());
        for (std::size_t node = 0; node < count; ++node)
            result[node] = component[node] == largest;
        return result;
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
                        float exitDistance = 0.0f;
                        const Route route = roundaboutRoute(lanes[static_cast<std::size_t>(lane)], lanes[static_cast<std::size_t>(lane)],
                                                            exitRotation, lengthIn, lengthOut,
                                                            lineDistance, mergeDistance, exitDistance);

                        // Indicate the way out: right for the first exit all
                        // the way round; left for the last until the exit,
                        // and right when leaving by any other.
                        std::vector<Indication> indications;
                        const float approach = std::max(0.0f, lineDistance - 25.0f);
                        if (turn == Turn::Right)
                            indications.push_back({approach, exitDistance + 8.0f, 1});
                        else if (turn == Turn::Left)
                            indications.push_back({approach, exitDistance - 6.0f, -1});
                        if (turn != Turn::Right)
                            indications.push_back({exitDistance - 6.0f, exitDistance + 8.0f, 1});

                        // The waiting car's front sits where the entry arc begins.
                        addRoute(index, inArm, lane, outArm, lane, turn, route, lineDistance + 2.0f,
                                 mergeDistance, indications);
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
                    const float laneOffset = lanes[static_cast<std::size_t>(lane)];
                    const Route route = turnRoute(turn, laneOffset, lengthIn, lengthOut, reach);
                    const float lineFront = bend ? route.totalLength()
                                                 : lengthIn - RoadNetwork::stopLine - lineClearance;

                    std::vector<Indication> indications;
                    if (!bend && turn != Turn::Straight)
                    {
                        // From well before the line until the turn is done.
                        const float radius = turn == Turn::Right ? reach - laneOffset : reach + laneOffset;
                        const float turnEnd = lengthIn - reach + radius * glm::radians(90.0f);
                        indications.push_back({std::max(0.0f, lineFront - 30.0f), turnEnd, turn == Turn::Left ? -1 : 1});
                    }
                    addRoute(index, inArm, lane, outArm, lane, turn, route, lineFront, 0.0f, indications);

                    if (!bend && turn == Turn::Right)
                    {
                        const Route wide = wideRightRoute(lengthIn, lengthOut);
                        const float turnEnd = lengthIn + (-RoadNetwork::laneOffsets[0] - wideTurnRadius) +
                                              wideTurnRadius * glm::radians(90.0f);
                        const std::size_t added = addRoute(index, inArm, lane, outArm, 0, turn, wide, lineFront, 0.0f,
                                                           {{std::max(0.0f, lineFront - 30.0f), turnEnd, 1}});
                        routes_[added].onlyClass = static_cast<int>(SizeClass::Truck);
                    }

                    // Going straight on, a car may also move over into the
                    // other lane once it is through. This is the only place
                    // cars change lane, and it is what lets a car caught in the
                    // "straight on only" lane round the ring road get out again.
                    // The crossing is measured as a conflict zone like any other.
                    if (!bend && turn == Turn::Straight)
                    {
                        const int other = 1 - lane;
                        const Indication change {lengthIn + laneChangeStart - 10.0f, lengthIn + laneChangeEnd,
                                                 other > lane ? 1 : -1};
                        addRoute(index, inArm, lane, outArm, other, turn,
                                 laneChangeRoute(laneOffset, lanes[static_cast<std::size_t>(other)], lengthIn, lengthOut),
                                 lineFront, 0.0f, {change});
                    }
                }
            }
        }
    }

    buildBusRoutes();
}

void TrafficSystem::buildBusRoutes()
{
    // The bus line's turns: left from the kerb lane into the kerb lane. Only
    // the buses may use them; everyone else turns left from the inner lane.
    constexpr float lane = RoadNetwork::laneOffsets[1];
    for (const LineLeg& leg : busLineLegs)
    {
        std::size_t junctionIndex = network_.junctionCount();
        for (std::size_t index = 0; index < network_.junctionCount(); ++index)
        {
            if (network_.junctions()[index].name == leg.junction)
                junctionIndex = index;
        }
        if (junctionIndex == network_.junctionCount())
            continue;   // the self-test reports the missing line
        const Junction& junction = network_.junctions()[junctionIndex];
        const float lengthIn = junction.armLength[static_cast<std::size_t>(leg.inArm)];
        const float lengthOut = junction.armLength[static_cast<std::size_t>(leg.outArm)];
        const Route route = turnRoute(Turn::Left, lane, lengthIn, lengthOut, RoadNetwork::boxHalf);
        const float lineFront = lengthIn - RoadNetwork::stopLine - lineClearance;
        const float turnEnd = lengthIn - RoadNetwork::boxHalf + (RoadNetwork::boxHalf + lane) * glm::radians(90.0f);
        const std::size_t index = addRoute(junctionIndex, leg.inArm, 1, leg.outArm, 1, Turn::Left, route, lineFront, 0.0f,
                                           {{busStopDistance + 3.0f, turnEnd, -1}});
        routes_[index].onlyClass = static_cast<int>(SizeClass::Bus);
    }
}

std::size_t TrafficSystem::addRoute(std::size_t junction, int inArm, int inLane, int outArm, int outLane,
                                    Turn turn, const Route& canonical, float lineFront, float mergeDistance,
                                    const std::vector<Indication>& indications)
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
    info.lineFront = lineFront;
    info.mergeDistance = mergeDistance;
    info.indications = indications;
    info.priorityRank = turnRank(turn) +
        ((j.type == JunctionType::GiveWayT && j.majorArm[static_cast<std::size_t>(inArm)]) ? 10 : 0);
    routes_.push_back(std::move(info));
    return routes_.size() - 1;
}

void TrafficSystem::buildSuccessors()
{
    // A route's successors are the routes of the next junction that start
    // exactly where it ends, in the same lane.
    for (RouteInfo& info : routes_)
    {
        info.successors.clear();
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
// The bus line
// ---------------------------------------------------------------------------

void TrafficSystem::buildBusLine()
{
    busLine_.clear();
    busStopSites_.clear();

    for (const LineLeg& leg : busLineLegs)
    {
        for (std::size_t index = 0; index < routes_.size(); ++index)
        {
            const RouteInfo& info = routes_[index];
            if (info.onlyClass == static_cast<int>(SizeClass::Bus) && network_.junctions()[info.junction].name == leg.junction &&
                info.inArm == leg.inArm && info.outArm == leg.outArm)
                busLine_.push_back(index);
        }
    }

    // Each leg must lead straight into the next, or there is no line.
    bool closed = busLine_.size() == busLineLegs.size();
    for (std::size_t leg = 0; closed && leg < busLine_.size(); ++leg)
    {
        const std::vector<std::size_t>& next = routes_[busLine_[leg]].successors;
        closed = std::find(next.begin(), next.end(), busLine_[(leg + 1) % busLine_.size()]) != next.end();
    }
    if (!closed)
    {
        busLine_.clear();
        return;
    }

    // One stop per leg, on the approach to its junction, with a shelter on
    // the sidewalk beside it.
    for (std::size_t routeIndex : busLine_)
    {
        RouteInfo& info = routes_[routeIndex];
        info.busStop = busStopDistance;
        const RouteSample sample = info.route.sample(busStopDistance);
        const glm::vec2 lane {sample.position.x, sample.position.z};
        const glm::vec2 left = leftOf(sample.headingDegrees);
        BusStopSite site;
        site.busCentre = lane;
        // From the kerb lane's centre: across the rest of the lane and 2.75 m
        // onto the 4.5 m sidewalk, behind the line of the lamp posts.
        site.shelter = lane - left * (RoadNetwork::halfWidth - RoadNetwork::laneOffsets[1] + 2.75f);
        site.facingDegrees = glm::degrees(std::atan2(left.x, left.y));
        site.headingDegrees = sample.headingDegrees;
        busStopSites_.push_back(site);
    }
}

// ---------------------------------------------------------------------------
// How each size of vehicle swings through a route, and where it may drive
// ---------------------------------------------------------------------------

void TrafficSystem::buildBodyPaths()
{
    // The front axle follows the lane; the rear axle is dragged along behind
    // it at a fixed wheelbase (a tractrix), so it cuts inside every turn.
    // Every route starts halfway along a straight road with the vehicle
    // lined up in its lane, which fixes where the rear axle starts.
    constexpr float integrationStep = 0.05f;
    std::for_each(std::execution::par, routes_.begin(), routes_.end(), [](RouteInfo& info)
    {
        const float length = info.route.totalLength();
        const std::size_t samples = static_cast<std::size_t>(std::floor(length / bodyStep)) + 2;
        for (std::size_t sizeClass = 0; sizeClass < sizeClassCount; ++sizeClass)
        {
            const SizeClassSpec& spec = sizeClassSpec(static_cast<SizeClass>(sizeClass));
            std::vector<float>& table = info.bodyYaw[sizeClass];
            table.assign(samples, 0.0f);

            const RouteSample start = info.route.sampleExtended(spec.frontAxle);
            glm::vec2 front {start.position.x, start.position.z};
            glm::vec2 rear = front - forwardOf(start.headingDegrees) * spec.wheelBase;

            float distance = 0.0f;
            for (std::size_t index = 0; index < samples; ++index)
            {
                const float target = static_cast<float>(index) * bodyStep;
                while (distance < target)
                {
                    distance = std::min(distance + integrationStep, target);
                    const glm::vec3 next = info.route.sampleExtended(distance + spec.frontAxle).position;
                    front = {next.x, next.z};
                    rear = front - glm::normalize(front - rear) * spec.wheelBase;
                }
                const glm::vec2 axis = front - rear;
                table[index] = glm::degrees(std::atan2(axis.x, axis.y));
            }
        }
    });
}

TrafficSystem::BodyFrame TrafficSystem::bodyFrame(std::size_t routeIndex, SizeClass sizeClass, float distance) const
{
    const RouteInfo& info = routes_[routeIndex];
    const SizeClassSpec& spec = sizeClassSpec(sizeClass);
    const std::vector<float>& table = info.bodyYaw[static_cast<std::size_t>(sizeClass)];

    const float position = glm::clamp(distance / bodyStep, 0.0f, static_cast<float>(table.size() - 1));
    const std::size_t low = std::min(static_cast<std::size_t>(position), table.size() - 2);
    const float blend = position - static_cast<float>(low);
    const float yaw = table[low] + signedAngleDifference(table[low + 1], table[low]) * blend;

    const RouteSample front = info.route.sampleExtended(distance + spec.frontAxle);
    const glm::vec2 forward = forwardOf(yaw);

    BodyFrame frame;
    frame.centre = front.position - glm::vec3(forward.x, 0.0f, forward.y) * spec.frontAxle;
    frame.yawDegrees = yaw;
    frame.steerDegrees = signedAngleDifference(front.headingDegrees, yaw);
    return frame;
}

void TrafficSystem::findAllowedRoutes()
{
    const KerbMap kerbs(network_);

    std::vector<std::vector<std::size_t>> successors(routes_.size());
    for (std::size_t index = 0; index < routes_.size(); ++index)
        successors[index] = routes_[index].successors;

    for (std::size_t sizeClass = 0; sizeClass < sizeClassCount; ++sizeClass)
    {
        const SizeClass size = static_cast<SizeClass>(sizeClass);
        // Each route is checked on its own, so they are checked in parallel.
        std::vector<char> clearFlags(routes_.size(), 0);
        std::vector<std::size_t> order(routes_.size());
        std::iota(order.begin(), order.end(), std::size_t {0});
        std::for_each(std::execution::par, order.begin(), order.end(), [&](std::size_t index)
        {
            const RouteInfo& info = routes_[index];
            // Buses only drive their line; the bus and truck turns are for
            // their own class alone.
            const bool onLine = std::find(busLine_.begin(), busLine_.end(), index) != busLine_.end();
            if (size == SizeClass::Bus ? !onLine : (info.onlyClass >= 0 && info.onlyClass != static_cast<int>(sizeClass)))
                return;

            bool ok = true;
            const float length = info.route.totalLength();
            for (float distance = 0.0f; distance <= length && ok; distance += 0.5f)
            {
                const BodyFrame frame = bodyFrame(index, size, distance);
                ok = bodyOnRoad(kerbs, {frame.centre.x, frame.centre.z}, frame.yawDegrees, size);
            }
            clearFlags[index] = ok ? 1 : 0;
        });
        const std::vector<bool> clear(clearFlags.begin(), clearFlags.end());

        // A vehicle must always have somewhere to go next and be able to get
        // anywhere it can drive: keep the largest strongly connected piece.
        // The bus line is a single loop and is kept whole, or not at all.
        const std::vector<bool> kept = size == SizeClass::Bus ? clear : largestStrongComponent(successors, clear);
        for (std::size_t index = 0; index < routes_.size(); ++index)
            routes_[index].allowed[sizeClass] = kept[index];
    }
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
        grids.emplace_back(positionsOf(samples.back()));
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
                    matchOffset = sampleB.distance + glm::dot(difference, forwardOf(sampleB.headingDegrees)) - sampleA.distance;
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
    {
        for (auto& list : info.conflicts)
            list.clear();
    }

    // ---- 1. What every width tier's bodies cover along each route ----------
    // Cut across the lane at every eighth of a metre (a "station"). Each body
    // a size class can have on the route, in quarter-metre steps, crosses a
    // run of stations; at each one it covers a stretch of that cross line.
    // Kept per quarter-metre bin: the most either side any body reaches
    // there. Every point of every body then lies inside the thin box of the
    // bin it is nearest, so two bodies can only touch where two such boxes
    // do. How far along the lane each class reaches, behind and ahead of its
    // own route distance, is kept too.
    struct Coverage
    {
        std::vector<float> low;    // per bin, lateral, left positive
        std::vector<float> high;
    };
    constexpr float stationStep = 0.125f;
    constexpr float noCover = std::numeric_limits<float>::max();
    constexpr float stationMargin = 16.0f;   // stations run this far past both ends
    std::vector<std::array<Coverage, widthTierCount>> coverage(routes_.size());
    std::vector<std::array<float, sizeClassCount>> rearReaches(routes_.size());
    std::vector<std::array<float, sizeClassCount>> frontReaches(routes_.size());

    // Routes are independent of each other here, so they are covered in parallel.
    std::vector<std::size_t> routeOrder(routes_.size());
    std::iota(routeOrder.begin(), routeOrder.end(), std::size_t {0});
    std::for_each(std::execution::par, routeOrder.begin(), routeOrder.end(), [&](std::size_t routeIndex)
    {
        const RouteInfo& info = routes_[routeIndex];
        const Junction& junction = network_.junctions()[info.junction];
        const float length = info.route.totalLength();
        const std::size_t bins = static_cast<std::size_t>(std::floor(length / sampleStep)) + 1;
        for (Coverage& tier : coverage[routeIndex])
        {
            tier.low.assign(bins, noCover);
            tier.high.assign(bins, -noCover);
        }
        std::array<float, sizeClassCount>& rearReach = rearReaches[routeIndex];
        std::array<float, sizeClassCount>& frontReach = frontReaches[routeIndex];
        rearReach.fill(0.0f);
        frontReach.fill(0.0f);

        // Every station of the route, sampled once: where the cross line
        // meets the lane and which way it runs.
        const std::size_t stationCount =
            static_cast<std::size_t>(std::ceil((length + 2.0f * stationMargin) / stationStep)) + 1;
        std::vector<glm::vec2> stationPoints(stationCount);
        std::vector<glm::vec2> stationLeft(stationCount);
        for (std::size_t station = 0; station < stationCount; ++station)
        {
            const RouteSample sample = info.route.sampleExtended(static_cast<float>(station) * stationStep - stationMargin);
            stationPoints[station] = {sample.position.x, sample.position.z};
            stationLeft[station] = leftOf(sample.headingDegrees);
        }

        for (std::size_t sizeClass = 0; sizeClass < sizeClassCount; ++sizeClass)
        {
            if (!info.allowed[sizeClass])
                continue;
            const SizeClass size = static_cast<SizeClass>(sizeClass);
            const SizeClassSpec& spec = sizeClassSpec(size);
            Coverage& tier = coverage[routeIndex][static_cast<std::size_t>(widthTier(size))];
            const float reachLimit = junctionRadius(junction) + spec.halfLength + 2.0f;

            for (float distance = 0.0f; distance <= length; distance += sampleStep)
            {
                const glm::vec3 lane = info.route.sample(distance).position;
                if (glm::length(glm::vec2(lane.x, lane.z) - junction.centre) > reachLimit)
                    continue;
                const BodyFrame frame = bodyFrame(routeIndex, size, distance);
                const OrientedBox body = makeOrientedBox({frame.centre.x, frame.centre.z}, frame.yawDegrees,
                                                         {spec.halfWidth, spec.halfLength});

                const auto record = [&](float along, float low, float high)
                {
                    rearReach[sizeClass] = std::max(rearReach[sizeClass], distance - along);
                    frontReach[sizeClass] = std::max(frontReach[sizeClass], along - distance);
                    if (along < -0.5f * sampleStep || along > length + 0.5f * sampleStep)
                        return;
                    // A station between two bins belongs to both.
                    const float bin = along / sampleStep;
                    for (const float index : {std::floor(bin), std::ceil(bin)})
                    {
                        const std::size_t slot = static_cast<std::size_t>(glm::clamp(index, 0.0f, static_cast<float>(bins - 1)));
                        tier.low[slot] = std::min(tier.low[slot], low);
                        tier.high[slot] = std::max(tier.high[slot], high);
                    }
                };

                const float from = distance - spec.halfLength - 2.5f;
                const float to = distance + spec.halfLength + 2.5f;
                const long long first = static_cast<long long>(std::ceil((from + stationMargin) / stationStep));
                const long long last = static_cast<long long>(std::floor((to + stationMargin) / stationStep));
                for (long long station = std::max(0LL, first);
                     station <= last && station < static_cast<long long>(stationCount); ++station)
                {
                    float t0 = 0.0f;
                    float t1 = 0.0f;
                    const std::size_t index = static_cast<std::size_t>(station);
                    if (clipLine(body, stationPoints[index], stationLeft[index], t0, t1))
                        record(static_cast<float>(station) * stationStep - stationMargin, t0, t1);
                }

                // The corners, where the outline turns between two stations.
                const glm::vec2 forward = body.forward;
                const glm::vec2 across {forward.y, -forward.x};
                for (float sideAlong : {-1.0f, 1.0f})
                {
                    for (float sideAcross : {-1.0f, 1.0f})
                    {
                        const glm::vec2 corner = body.centre + forward * (sideAlong * spec.halfLength) +
                                                 across * (sideAcross * spec.halfWidth);
                        float along = 0.0f;
                        float lateral = 0.0f;
                        info.route.project(corner, from - 1.0f, to + 1.0f, along, lateral);
                        record(along, lateral, lateral);
                    }
                }
            }
        }
    });

    rearReach_.fill(0.0f);
    frontReach_.fill(0.0f);
    for (std::size_t routeIndex = 0; routeIndex < routes_.size(); ++routeIndex)
    {
        for (std::size_t sizeClass = 0; sizeClass < sizeClassCount; ++sizeClass)
        {
            rearReach_[sizeClass] = std::max(rearReach_[sizeClass], rearReaches[routeIndex][sizeClass]);
            frontReach_[sizeClass] = std::max(frontReach_[sizeClass], frontReaches[routeIndex][sizeClass]);
        }
    }

    // ---- 2. Those covers as thin boxes near each junction ------------------
    // Margins: the conflict margin all round, the step between two sampled
    // bodies across, and along the lane half a bin plus the fan-out of the
    // cross lines on the tightest arc.
    struct Slice
    {
        OrientedBox box;
        float distance = 0.0f;
    };
    std::vector<std::array<std::vector<Slice>, widthTierCount>> slices(routes_.size());
    std::vector<std::array<std::vector<glm::vec2>, widthTierCount>> centres(routes_.size());
    std::vector<std::array<float, widthTierCount>> sliceReach(routes_.size());
    for (std::size_t routeIndex = 0; routeIndex < routes_.size(); ++routeIndex)
    {
        const RouteInfo& info = routes_[routeIndex];
        const Junction& junction = network_.junctions()[info.junction];
        for (std::size_t tier = 0; tier < widthTierCount; ++tier)
        {
            const Coverage& cover = coverage[routeIndex][tier];
            float reach = 0.0f;
            for (std::size_t bin = 0; bin < cover.low.size(); ++bin)
            {
                if (cover.low[bin] > cover.high[bin])
                    continue;
                const float along = static_cast<float>(bin) * sampleStep;
                const RouteSample sample = info.route.sample(along);
                const glm::vec2 lane {sample.position.x, sample.position.z};
                if (glm::length(lane - junction.centre) > junctionRadius(junction))
                    continue;
                const float middle = 0.5f * (cover.low[bin] + cover.high[bin]);
                const glm::vec2 centre = lane + leftOf(sample.headingDegrees) * middle;
                const glm::vec2 half {0.5f * (cover.high[bin] - cover.low[bin]) + conflictMargin + 0.03f,
                                      0.5f * sampleStep + 0.05f + conflictMargin};
                slices[routeIndex][tier].push_back({makeOrientedBox(centre, sample.headingDegrees, half), along});
                centres[routeIndex][tier].push_back(centre);
                reach = std::max(reach, glm::length(half));
            }
            sliceReach[routeIndex][tier] = reach;
        }
    }
    std::vector<SampleGrid> grids;
    grids.reserve(routes_.size() * widthTierCount);
    for (std::size_t routeIndex = 0; routeIndex < routes_.size(); ++routeIndex)
    {
        for (std::size_t tier = 0; tier < widthTierCount; ++tier)
            grids.emplace_back(centres[routeIndex][tier], 2.0f);
    }

    // ---- 3. Every pair of routes of a junction, for every pair of tiers ----
    // Junctions are independent, so they are searched in parallel; the zones
    // are then numbered junction by junction, so every run gives the same list.
    std::vector<std::vector<Conflict>> found(network_.junctionCount());
    std::vector<std::size_t> junctionOrder(network_.junctionCount());
    std::iota(junctionOrder.begin(), junctionOrder.end(), std::size_t {0});
    std::for_each(std::execution::par, junctionOrder.begin(), junctionOrder.end(), [&](std::size_t junctionIndex)
    {
    std::vector<std::size_t> members;
    for (std::size_t index = 0; index < routes_.size(); ++index)
    {
        if (routes_[index].junction == junctionIndex)
            members.push_back(index);
    }
    for (std::size_t first = 0; first < members.size(); ++first)
    {
        for (std::size_t second = first + 1; second < members.size(); ++second)
        {
            const std::size_t a = members[first];
            const std::size_t b = members[second];
            const RouteInfo& infoA = routes_[a];
            const RouteInfo& infoB = routes_[b];

            // Routes from the same lane queue one behind the other and then
            // split; following keeps them apart, and they never meet again.
            if (sameStartLane(a, b))
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

            for (std::size_t tierA = 0; tierA < widthTierCount; ++tierA)
            {
                for (std::size_t tierB = 0; tierB < widthTierCount; ++tierB)
                {
                    const std::vector<Slice>& pathA = slices[a][tierA];
                    const std::vector<Slice>& pathB = slices[b][tierB];
                    if (pathA.empty() || pathB.empty())
                        continue;
                    const std::size_t countB = pathB.size();
                    const SampleGrid& gridB = grids[b * widthTierCount + tierB];
                    const float searchRadius = sliceReach[a][tierA] + sliceReach[b][tierB];

                    // Mark every pair of slices that touch.
                    std::unordered_set<std::uint64_t> touching;
                    for (std::size_t i = 0; i < pathA.size(); ++i)
                    {
                        gridB.near(pathA[i].box.centre, searchRadius, [&](int index)
                        {
                            const std::size_t j = static_cast<std::size_t>(index);
                            const glm::vec2 between = pathA[i].box.centre - pathB[j].box.centre;
                            if (glm::dot(between, between) > searchRadius * searchRadius)
                                return;
                            if (following(pathA[i].distance, pathB[j].distance))
                                return;
                            if (boxesOverlap(pathA[i].box, pathB[j].box))
                                touching.insert(static_cast<std::uint64_t>(i) * countB + j);
                        });
                    }
                    if (touching.empty())
                        continue;

                    // Each connected patch of touching pairs is one conflict
                    // zone. Its extent on each route is the stretch of lane
                    // involved.
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
                        conflict.tier = {static_cast<int>(tierA), static_cast<int>(tierB)};
                        conflict.in = {pathA[lowA].distance - 0.5f * sampleStep, pathB[lowB].distance - 0.5f * sampleStep};
                        conflict.out = {pathA[highA].distance + 0.5f * sampleStep, pathB[highB].distance + 0.5f * sampleStep};

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

                            // Otherwise equal: a car keeping its lane goes before one
                            // moving across into it, and of two cars swapping lanes the
                            // one moving out towards the kerb goes first.
                            const bool changesA = infoA.inLane != infoA.outLane;
                            const bool changesB = infoB.inLane != infoB.outLane;
                            if (conflict.prioritySide < 0 && changesA != changesB)
                                conflict.prioritySide = changesA ? 1 : 0;
                            else if (conflict.prioritySide < 0 && changesA && changesB)
                                conflict.prioritySide = infoA.outLane > infoB.outLane ? 0 : 1;
                        }

                        found[junctionIndex].push_back(conflict);
                    }
                }
            }
        }
    }
    });

    for (const std::vector<Conflict>& list : found)
    {
        for (const Conflict& conflict : list)
        {
            const std::size_t index = conflicts_.size();
            conflicts_.push_back(conflict);
            routes_[conflict.route[0]].conflicts[static_cast<std::size_t>(conflict.tier[0])].push_back({index, 0});
            routes_[conflict.route[1]].conflicts[static_cast<std::size_t>(conflict.tier[1])].push_back({index, 1});
        }
    }


    claimCounts_.assign(conflicts_.size() * 2, 0);
}

void TrafficSystem::finishRoutes()
{
    for (RouteInfo& info : routes_)
    {
        const Junction& junction = network_.junctions()[info.junction];
        const float length = info.route.totalLength();

        // Nothing to cross and no signal (a bend, or a free-flowing turn):
        // the vehicle simply drives through.
        std::array<float, widthTierCount> firstZone {};
        std::array<float, widthTierCount> lastZone {};
        for (std::size_t tier = 0; tier < widthTierCount; ++tier)
        {
            firstZone[tier] = std::numeric_limits<float>::max();
            lastZone[tier] = 0.0f;
            for (const ConflictRef& ref : info.conflicts[tier])
            {
                firstZone[tier] = std::min(firstZone[tier], conflicts_[ref.conflict].in[static_cast<std::size_t>(ref.side)]);
                lastZone[tier] = std::max(lastZone[tier], conflicts_[ref.conflict].out[static_cast<std::size_t>(ref.side)]);
            }
            // A route over a pedestrian crossing always has a place to wait
            // at the line, even a turn that crosses no other traffic: a
            // vehicle giving way to people must stand on its approach lane,
            // where the traffic behind it sees it, not just past the point
            // where its lane splits from theirs.
            bool overCrossing = false;
            for (std::size_t sizeClass = 0; sizeClass < sizeClassCount; ++sizeClass)
            {
                if (static_cast<std::size_t>(widthTier(static_cast<SizeClass>(sizeClass))) == tier)
                    overCrossing = overCrossing || !info.crossings[sizeClass].empty();
            }
            info.needsCommit[tier] = !info.conflicts[tier].empty() || junction.isSignalised() || overCrossing;
        }

        for (std::size_t sizeClass = 0; sizeClass < sizeClassCount; ++sizeClass)
        {
            const std::size_t tier = static_cast<std::size_t>(widthTier(static_cast<SizeClass>(sizeClass)));
            // Wait before the first conflict zone, whatever the painted line
            // says, so a vehicle standing at the line can never be touched by
            // crossing traffic.
            float stop = length;
            if (info.needsCommit[tier])
                stop = std::min(info.lineFront, firstZone[tier] - 0.4f) - frontReach_[sizeClass];
            info.stopDistance[sizeClass] = stop;
            info.junctionExit[sizeClass] = lastZone[tier] > 0.0f ? lastZone[tier] + rearReach_[sizeClass]
                                                                 : std::min(stop + 12.0f, length);

            // Crossings beyond the waiting place are only reached after
            // being let into the junction.
            for (CrossingRef& ref : info.crossings[sizeClass])
                ref.afterLine = info.needsCommit[tier] && ref.in > stop;
        }

        // Comfortable cornering speed at every metre: v = sqrt(a * r).
        const int metres = static_cast<int>(std::ceil(length)) + 1;
        info.curveSpeed.assign(static_cast<std::size_t>(metres), 99.0f);
        for (int metre = 0; metre < metres; ++metre)
        {
            const float curvature = info.route.sample(static_cast<float>(metre)).curvature;
            if (curvature > 1.0e-4f)
                info.curveSpeed[static_cast<std::size_t>(metre)] = std::sqrt(lateralAcceleration / curvature);
        }
    }
}

void TrafficSystem::buildCrossingRefs()
{
    // Where each size of body passes over each crossing of its junction:
    // the body grown by 0.3 m against the band grown by 0.4 m along the road
    // and 0.2 m past its ends, sampled every quarter metre. The interval is
    // widened by one sample either way, so it is never too short.
    const std::vector<Crossing>& crossings = network_.crossings();
    std::vector<OrientedBox> bands;
    for (const Crossing& crossing : crossings)
    {
        const glm::vec2 centre = crossing.point(0.5f * (crossing.from + crossing.to), 0.0f);
        bands.push_back(makeOrientedBox(centre, glm::degrees(std::atan2(crossing.along.x, crossing.along.y)),
                                        {0.5f * (crossing.to - crossing.from) + 0.2f, crossing.halfWidth + 0.4f}));
    }

    std::vector<std::size_t> routeOrder(routes_.size());
    std::iota(routeOrder.begin(), routeOrder.end(), std::size_t {0});
    std::for_each(std::execution::par, routeOrder.begin(), routeOrder.end(), [&](std::size_t routeIndex)
    {
        RouteInfo& info = routes_[routeIndex];
        const float length = info.route.totalLength();
        for (std::size_t sizeClass = 0; sizeClass < sizeClassCount; ++sizeClass)
        {
            const SizeClassSpec& spec = sizeClassSpec(static_cast<SizeClass>(sizeClass));
            std::vector<CrossingRef>& refs = info.crossings[sizeClass];
            refs.clear();
            for (std::size_t index = 0; index < crossings.size(); ++index)
            {
                if (crossings[index].junction != info.junction)
                    continue;
                float first = -1.0f;
                float last = -1.0f;
                for (float distance = 0.0f; distance <= length; distance += bodyStep)
                {
                    const BodyFrame frame = bodyFrame(routeIndex, static_cast<SizeClass>(sizeClass), distance);
                    const OrientedBox body = makeOrientedBox({frame.centre.x, frame.centre.z}, frame.yawDegrees,
                                                             {spec.halfWidth + 0.3f, spec.halfLength + 0.3f});
                    if (!boxesOverlap(body, bands[index]))
                        continue;
                    if (first < 0.0f)
                        first = distance;
                    last = distance;
                }
                if (first < 0.0f)
                    continue;
                CrossingRef ref;
                ref.crossing = index;
                ref.in = std::max(0.0f, first - bodyStep);
                ref.out = std::min(length, last + bodyStep);
                refs.push_back(ref);
            }
            std::sort(refs.begin(), refs.end(), [](const CrossingRef& a, const CrossingRef& b) { return a.in < b.in; });
        }
    });
    crossingStates_.assign(crossings.size(), CrossingState {});
}

std::vector<StopMarking> TrafficSystem::stopMarkings() const
{
    // One line per approach lane that has to wait: the earliest stop of all
    // the car routes leaving from that lane (they share it up to the line).
    constexpr std::size_t car = static_cast<std::size_t>(SizeClass::Car);
    std::vector<StopMarking> markings;
    std::vector<bool> done(routes_.size(), false);
    for (std::size_t index = 0; index < routes_.size(); ++index)
    {
        const RouteInfo& info = routes_[index];
        if (done[index] || info.onlyClass >= 0 || !info.needsCommit[0])
            continue;

        const Junction& junction = network_.junctions()[info.junction];
        const bool major = junction.type == JunctionType::GiveWayT &&
                           junction.majorArm[static_cast<std::size_t>(info.inArm)];
        float stop = info.stopDistance[car];
        for (std::size_t other = index; other < routes_.size(); ++other)
        {
            if (sameStartLane(index, other) && routes_[other].onlyClass < 0)
            {
                stop = std::min(stop, routes_[other].stopDistance[car]);
                done[other] = true;
            }
        }
        if (major)
            continue;   // the road with priority has no line to wait at

        const RouteSample sample = info.route.sample(stop + frontReach_[car] + lineClearance);
        markings.push_back({{sample.position.x, sample.position.z}, forwardOf(sample.headingDegrees),
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
    char line[240];
    std::snprintf(line, sizeof(line), "%zu junctions, %zu routes, %zu conflict zones, bus line of %zu legs\n",
                  network_.junctionCount(), routes_.size(), conflicts_.size(), busLine_.size());
    output += line;
    for (std::size_t sizeClass = 0; sizeClass < sizeClassCount; ++sizeClass)
    {
        std::size_t allowed = 0;
        for (const RouteInfo& info : routes_)
            allowed += info.allowed[sizeClass] ? 1 : 0;
        std::snprintf(line, sizeof(line), "  %-6s may drive %3zu routes; body reaches %.2f m behind, %.2f m ahead\n",
                      sizeClassSpec(static_cast<SizeClass>(sizeClass)).name, allowed,
                      rearReach_[sizeClass], frontReach_[sizeClass]);
        output += line;
    }

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
            std::string classes;
            for (std::size_t sizeClass = 0; sizeClass < sizeClassCount; ++sizeClass)
                classes += info.allowed[sizeClass] ? sizeClassSpec(static_cast<SizeClass>(sizeClass)).name[0] : '-';
            std::snprintf(line, sizeof(line),
                "  %3zu %s%s -> %s%s %-8s %s length %6.1f  car stop %6.1f  zones %2zu+%-2zu next %zu%s\n",
                index, armName(info.inArm), info.inLane == 0 ? "i" : "o",
                armName(info.outArm), info.outLane == 0 ? "i" : "o", turnName(info.turn), classes.c_str(),
                info.route.totalLength(), info.needsCommit[0] ? info.stopDistance[0] : -1.0f,
                info.conflicts[0].size(), info.conflicts[1].size(), info.successors.size(),
                info.onlyClass == static_cast<int>(SizeClass::Bus) ? "  (bus line)" : info.onlyClass >= 0 ? "  (trucks only)" : "");
            output += line;
        }
    }
    return output;
}

bool TrafficSystem::writeNetworkImage(const std::string& path) const
{
    // Half a metre per pixel over 600 m x 600 m, north up: the whole city
    // (about 430 m across, kerb to kerb) with a margin.
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

    // Kerbs of the city blocks and round the outside, and the islands.
    for (std::size_t block = 0; block < network_.blockCount(); ++block)
        polyline(network_.blockOutline(block, 0.0f), {150, 150, 150}, true);
    polyline(network_.outsideOutline(0.0f), {150, 150, 150}, true);
    for (std::size_t junction = 0; junction < network_.junctionCount(); ++junction)
    {
        const Junction& j = network_.junctions()[junction];
        if (j.type != JunctionType::Roundabout)
            continue;
        for (int arm = 0; arm < 4; ++arm)
        {
            if (j.hasArm[static_cast<std::size_t>(arm)])
                polyline(network_.splitterIsland(junction, arm), {150, 150, 150}, true);
        }
        std::vector<glm::vec2> island;
        for (int step = 0; step < 64; ++step)
        {
            const float angle = static_cast<float>(step) / 64.0f * 6.2831853f;
            island.push_back(j.centre + RoadNetwork::islandRadius * glm::vec2{std::cos(angle), std::sin(angle)});
        }
        polyline(island, {90, 170, 90}, true);
    }

    // Every route: inner lanes cyan, outer lanes orange, the bus line magenta.
    for (const RouteInfo& info : routes_)
    {
        const std::array<unsigned char, 3> color = info.onlyClass >= 0 ? std::array<unsigned char, 3>{230, 70, 220}
            : info.inLane == 0 ? std::array<unsigned char, 3>{70, 200, 230} : std::array<unsigned char, 3>{240, 160, 60};
        for (float distance = 0.0f; distance <= info.route.totalLength(); distance += 0.25f)
        {
            const glm::vec3 p = info.route.sample(distance).position;
            plot({p.x, p.z}, color, 0);
        }
    }

    // Stop lines white, conflict-zone middles red, bus stops yellow.
    for (const StopMarking& marking : stopMarkings())
        plot(marking.position, {255, 255, 255}, 1);
    for (const Conflict& conflict : conflicts_)
    {
        const glm::vec3 p = routes_[conflict.route[0]].route.sample(0.5f * (conflict.in[0] + conflict.out[0])).position;
        plot({p.x, p.z}, {235, 50, 50}, 1);
    }
    for (const BusStopSite& site : busStopSites_)
    {
        plot(site.busCentre, {250, 220, 40}, 2);
        plot(site.shelter, {250, 220, 40}, 2);
    }

    return stbi_write_png(path.c_str(), size, size, 3, pixels.data(), size * 3) != 0;
}

bool TrafficSystem::writeTurnImage(const std::string& path) const
{
    // 64 m x 64 m round the central crossroads at 5 cm a pixel, north up.
    constexpr int size = 1280;
    constexpr float metresPerPixel = 0.05f;
    const glm::vec2 centre = network_.junctions()[network_.centralJunction()].centre;
    std::vector<unsigned char> pixels(static_cast<std::size_t>(size) * size * 3, 0);
    for (std::size_t index = 0; index < pixels.size(); index += 3)
    {
        pixels[index] = 40;
        pixels[index + 1] = 42;
        pixels[index + 2] = 46;
    }

    const auto plot = [&pixels, centre](glm::vec2 world, std::array<unsigned char, 3> color, int radius)
    {
        const glm::vec2 local = world - centre;
        const int cx = static_cast<int>(std::lround(local.x / metresPerPixel)) + size / 2;
        const int cy = size / 2 - static_cast<int>(std::lround(local.y / metresPerPixel));
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
    const auto line = [&plot](glm::vec2 a, glm::vec2 b, std::array<unsigned char, 3> color)
    {
        const int steps = std::max(1, static_cast<int>(glm::length(b - a) / 0.025f));
        for (int step = 0; step <= steps; ++step)
            plot(glm::mix(a, b, static_cast<float>(step) / static_cast<float>(steps)), color, 0);
    };

    // The kerbs (green-grey) and every lane of the junction (dim).
    for (std::size_t block = 0; block < network_.blockCount(); ++block)
    {
        const std::vector<glm::vec2> outline = network_.blockOutline(block, 0.0f);
        for (std::size_t index = 0; index < outline.size(); ++index)
            line(outline[index], outline[(index + 1) % outline.size()], {150, 175, 150});
    }
    for (const RouteInfo& info : routes_)
    {
        if (info.junction != network_.centralJunction())
            continue;
        for (float distance = 0.0f; distance <= info.route.totalLength(); distance += 0.05f)
        {
            const glm::vec3 p = info.route.sample(distance).position;
            plot({p.x, p.z}, {80, 84, 92}, 0);
        }
    }

    // The three turns.
    struct Sweep
    {
        std::size_t route = noRoute;
        SizeClass size = SizeClass::Car;
        std::array<unsigned char, 3> color {};
    };
    std::array<Sweep, 3> sweeps {};
    for (std::size_t index = 0; index < routes_.size(); ++index)
    {
        const RouteInfo& info = routes_[index];
        if (info.junction != network_.centralJunction())
            continue;
        if (info.onlyClass == static_cast<int>(SizeClass::Bus))
            sweeps[0] = {index, SizeClass::Bus, {235, 80, 220}};
        else if (info.onlyClass == static_cast<int>(SizeClass::Truck) && info.inArm == ArmSouth)
            sweeps[1] = {index, SizeClass::Truck, {245, 165, 40}};
        else if (info.onlyClass < 0 && info.turn == Turn::Right && info.inArm == ArmEast)
            sweeps[2] = {index, SizeClass::Car, {80, 210, 240}};
    }
    for (const Sweep& sweep : sweeps)
    {
        if (sweep.route == noRoute)
            continue;
        const RouteInfo& info = routes_[sweep.route];
        const SizeClassSpec& spec = sizeClassSpec(sweep.size);
        const std::size_t sizeClass = static_cast<std::size_t>(sweep.size);
        const float from = std::max(0.0f, info.stopDistance[sizeClass] - 4.0f);
        const float to = std::min(info.route.totalLength(), info.junctionExit[sizeClass] + 4.0f);
        for (float distance = from; distance <= to; distance += 1.0f)
        {
            const BodyFrame frame = bodyFrame(sweep.route, sweep.size, distance);
            const OrientedBox body = makeOrientedBox({frame.centre.x, frame.centre.z}, frame.yawDegrees,
                                                     {spec.halfWidth, spec.halfLength});
            const glm::vec2 across {body.forward.y, -body.forward.x};
            const std::array<glm::vec2, 4> corners = {
                body.centre + body.forward * spec.halfLength + across * spec.halfWidth,
                body.centre + body.forward * spec.halfLength - across * spec.halfWidth,
                body.centre - body.forward * spec.halfLength - across * spec.halfWidth,
                body.centre - body.forward * spec.halfLength + across * spec.halfWidth
            };
            for (std::size_t corner = 0; corner < 4; ++corner)
                line(corners[corner], corners[(corner + 1) % 4], sweep.color);
            // The two axles: front on the lane, rear trailing.
            plot(body.centre + body.forward * spec.frontAxle, {255, 255, 255}, 2);
            plot(body.centre + body.forward * (spec.frontAxle - spec.wheelBase), {255, 255, 255}, 2);
        }
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
    const auto className = [](std::size_t sizeClass)
    {
        return std::string(sizeClassSpec(static_cast<SizeClass>(sizeClass)).name);
    };

    // The closed city: every junction has as many arms as its type says
    // (never fewer than two, so no route needs a U-turn).
    if (routes_.size() < 40)
        fail("unexpected network size: " + std::to_string(routes_.size()) + " routes");
    ++checks;
    for (const Junction& junction : network_.junctions())
    {
        const auto arms = std::count(junction.hasArm.begin(), junction.hasArm.end(), true);
        const bool bend = junction.type == JunctionType::Bend;
        const bool tee = junction.type == JunctionType::SignalT || junction.type == JunctionType::GiveWayT;
        const bool full = junction.type == JunctionType::SignalCross || junction.type == JunctionType::Roundabout;
        if ((bend && arms != 2) || (tee && arms != 3) || (full && arms != 4))
            fail(junction.name + " has " + std::to_string(arms) + " arms, which does not match its type");
        ++checks;
    }

    // Every class outline must hold the real body of every kind in the class.
    for (std::size_t kind = 0; kind < vehicleKindCount; ++kind)
    {
        const VehicleSpec& spec = vehicleSpec(static_cast<VehicleKind>(kind));
        const SizeClassSpec& outline = sizeClassSpec(spec.sizeClass);
        if (0.5f * spec.length > outline.halfLength + 1.0e-4f || 0.5f * spec.width > outline.halfWidth + 1.0e-4f)
            fail(std::string(spec.name) + " does not fit inside the " + outline.name + " outline");
        ++checks;
    }

    std::vector<std::size_t> predecessors(routes_.size(), 0);
    for (const RouteInfo& info : routes_)
    {
        for (std::size_t next : info.successors)
            ++predecessors[next];
    }

    const KerbMap kerbs(network_);
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

        // Every route begins where others end, and ends where others begin:
        // lanes join up, and a car can drive on for ever.
        if (predecessors[index] == 0)
            fail(name + " cannot be reached: no route ends where it starts");
        if (info.successors.empty())
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

        // Ordinary cars and vans may drive every route except the bus and truck turns:
        // their whole body stays clear of every kerb and island all the way.
        for (std::size_t sizeClass : {static_cast<std::size_t>(SizeClass::Car), static_cast<std::size_t>(SizeClass::Van)})
        {
            const bool reserved = info.onlyClass >= 0;
            if (info.allowed[sizeClass] == reserved)
                fail(name + (reserved ? " is open to " : " is closed to ") + className(sizeClass) + "s");
            ++checks;
        }

        for (std::size_t sizeClass = 0; sizeClass < sizeClassCount; ++sizeClass)
        {
            if (!info.allowed[sizeClass])
                continue;
            const SizeClass size = static_cast<SizeClass>(sizeClass);

            // The body swings smoothly: from one quarter metre to the next the
            // heading never jumps, and it lines up with the lane at both ends
            // (where the route joins the next one).
            for (float distance = bodyStep; distance <= length; distance += bodyStep)
            {
                const BodyFrame a = bodyFrame(index, size, distance - bodyStep);
                const BodyFrame b = bodyFrame(index, size, distance);
                if (std::abs(signedAngleDifference(b.yawDegrees, a.yawDegrees)) > 3.0f ||
                    glm::length(b.centre - a.centre) > bodyStep * 1.5f + 0.01f)
                {
                    fail(name + ": the " + className(sizeClass) + " body jumps at s = " + std::to_string(distance));
                    break;
                }
            }
            for (const float end : {0.0f, length})
            {
                const BodyFrame frame = bodyFrame(index, size, end);
                const RouteSample lane = info.route.sample(end);
                if (std::abs(signedAngleDifference(frame.yawDegrees, lane.headingDegrees)) > 0.5f ||
                    glm::length(frame.centre - lane.position) > 0.05f)
                    fail(name + ": the " + className(sizeClass) + " body is not lined up with the lane at s = " +
                         std::to_string(end));
            }
            checks += 3;

            // Its body stays on the road (the wheels, for trucks and buses).
            for (float distance = 0.0f; distance <= length; distance += 0.5f)
            {
                const BodyFrame frame = bodyFrame(index, size, distance);
                if (!bodyOnRoad(kerbs, {frame.centre.x, frame.centre.z}, frame.yawDegrees, size))
                {
                    fail(name + ": a " + className(sizeClass) + " runs over a kerb or island at s = " + std::to_string(distance));
                    break;
                }
            }
            ++checks;

            // A vehicle waiting at its line must be clear of every conflict
            // zone, or crossing traffic could touch it while it waits.
            const std::size_t tier = static_cast<std::size_t>(widthTier(size));
            for (const ConflictRef& ref : info.conflicts[tier])
            {
                const float entersZone = conflicts_[ref.conflict].in[static_cast<std::size_t>(ref.side)] - frontReach_[sizeClass];
                if (info.stopDistance[sizeClass] >= entersZone)
                    fail(name + ": a " + className(sizeClass) + " waits inside conflict zone " + std::to_string(ref.conflict));
                ++checks;
            }
            if (info.needsCommit[tier] && info.stopDistance[sizeClass] < 5.0f)
                fail(name + ": a " + className(sizeClass) + " stops implausibly early (s = " +
                     std::to_string(info.stopDistance[sizeClass]) + ")");
            ++checks;
        }
    }

    // Each size class can get from anywhere it may drive to anywhere else it
    // may drive, and back: nobody is ever stranded. Without the lane changes
    // this would fail: round the ring road one lane only ever goes straight on.
    for (std::size_t sizeClass = 0; sizeClass < sizeClassCount; ++sizeClass)
    {
        std::vector<std::size_t> members;
        for (std::size_t index = 0; index < routes_.size(); ++index)
        {
            if (routes_[index].allowed[sizeClass])
                members.push_back(index);
        }
        if (members.empty())
        {
            fail("no route is open to the " + className(sizeClass) + " class");
            continue;
        }
        const auto reachable = [this, sizeClass, &members](bool forwards)
        {
            std::vector<bool> seen(routes_.size(), false);
            std::deque<std::size_t> queue {members.front()};
            seen[members.front()] = true;
            while (!queue.empty())
            {
                const std::size_t current = queue.front();
                queue.pop_front();
                for (std::size_t other = 0; other < routes_.size(); ++other)
                {
                    if (!routes_[other].allowed[sizeClass] || seen[other])
                        continue;
                    const RouteInfo& from = forwards ? routes_[current] : routes_[other];
                    const std::size_t to = forwards ? other : current;
                    if (std::find(from.successors.begin(), from.successors.end(), to) != from.successors.end())
                    {
                        seen[other] = true;
                        queue.push_back(other);
                    }
                }
            }
            return static_cast<std::size_t>(std::count(seen.begin(), seen.end(), true));
        };
        const std::size_t ahead = reachable(true);
        const std::size_t behind = reachable(false);
        if (ahead != members.size() || behind != members.size())
            fail("the " + className(sizeClass) + " routes are not strongly connected: " + std::to_string(ahead) +
                 " reachable ahead, " + std::to_string(behind) + " behind, of " + std::to_string(members.size()));
        ++checks;
    }

    // Trucks cannot take the tight right turns from the kerb lane (they swing
    // wide instead), and at a roundabout only go straight on from the kerb
    // lane, but must still reach a good part of the city.
    {
        std::size_t open = 0;
        std::size_t ordinary = 0;
        for (const RouteInfo& info : routes_)
        {
            ordinary += info.onlyClass >= 0 ? 0 : 1;
            open += info.allowed[static_cast<std::size_t>(SizeClass::Truck)] ? 1 : 0;
        }
        if (open * 5 < ordinary * 2)
            fail("trucks may drive only " + std::to_string(open) + " of " + std::to_string(ordinary) + " routes");
        ++checks;
    }

    // The bus line: a closed loop of bus turns, every leg open to buses and
    // to nobody else, each with a stop on the straight approach, well before
    // the queue at the line and outside every conflict zone.
    if (busLine_.size() != busLineLegs.size())
        fail("the bus line is not a closed loop (" + std::to_string(busLine_.size()) + " legs found)");
    ++checks;
    for (std::size_t leg = 0; leg < busLine_.size(); ++leg)
    {
        const std::size_t index = busLine_[leg];
        const RouteInfo& info = routes_[index];
        const std::string name = "bus line leg " + std::to_string(leg) + " (" + routeName(index) + ")";
        constexpr std::size_t bus = static_cast<std::size_t>(SizeClass::Bus);
        if (!info.allowed[bus])
            fail(name + " is not drivable by a bus");
        if (info.busStop < 0.0f)
            fail(name + " has no stop");
        else
        {
            if (info.busStop + frontReach_[bus] > info.stopDistance[bus] + frontReach_[bus] - 8.0f)
                fail(name + ": the stop is too close to the line");
            if (info.busStop - rearReach_[bus] < 0.0f)
                fail(name + ": a bus at the stop reaches back into the previous road's junction route");
            if (info.route.sample(info.busStop + frontReach_[bus]).curvature > 0.0f)
                fail(name + ": the stop is not on the straight");
            for (const ConflictRef& ref : info.conflicts[1])
            {
                if (info.busStop + frontReach_[bus] >= conflicts_[ref.conflict].in[static_cast<std::size_t>(ref.side)])
                    fail(name + ": a bus at the stop is inside conflict zone " + std::to_string(ref.conflict));
            }
        }
        checks += 4;
    }

    // Pedestrian crossings: every one is driven over by some route, and a
    // vehicle waiting at its line never stands on a crossing beyond it, or
    // nobody could ever cross in front of it.
    {
        std::vector<std::size_t> drivenOver(network_.crossings().size(), 0);
        for (std::size_t index = 0; index < routes_.size(); ++index)
        {
            const RouteInfo& info = routes_[index];
            for (std::size_t sizeClass = 0; sizeClass < sizeClassCount; ++sizeClass)
            {
                if (!info.allowed[sizeClass])
                    continue;
                for (const CrossingRef& ref : info.crossings[sizeClass])
                {
                    ++drivenOver[ref.crossing];
                    if (!(ref.in < ref.out))
                        fail(routeName(index) + ": an empty crossing interval");
                    if (ref.afterLine && info.stopDistance[sizeClass] >= ref.in)
                        fail(routeName(index) + ": a " + className(sizeClass) + " waiting at its line stands on crossing " +
                             std::to_string(ref.crossing));
                    ++checks;
                }
            }
        }
        for (std::size_t crossing = 0; crossing < drivenOver.size(); ++crossing)
        {
            const Crossing& info = network_.crossings()[crossing];
            if (drivenOver[crossing] == 0)
                fail("no route drives over the crossing at " + network_.junctions()[info.junction].name + " " + armName(info.arm));
            ++checks;
        }
    }

    // Every conflict must be listed by both of its routes, once each, on
    // opposite sides and for the right tier, and only ever pair routes of
    // the same junction.
    for (std::size_t index = 0; index < conflicts_.size(); ++index)
    {
        const Conflict& conflict = conflicts_[index];
        const std::string name = "conflict " + std::to_string(index);
        if (routes_[conflict.route[0]].junction != routes_[conflict.route[1]].junction)
            fail(name + " pairs routes of different junctions");
        for (int side = 0; side < 2; ++side)
        {
            const RouteInfo& info = routes_[conflict.route[static_cast<std::size_t>(side)]];
            const auto& list = info.conflicts[static_cast<std::size_t>(conflict.tier[static_cast<std::size_t>(side)])];
            const auto listed = std::count_if(list.begin(), list.end(),
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

        // project() finds points placed beside the route, including beyond
        // both ends.
        for (const float along : {-3.0f, 0.37f * length, length + 4.0f})
        {
            const RouteSample sample = route.sampleExtended(along);
            const glm::vec2 point = glm::vec2(sample.position.x, sample.position.z) + leftOf(sample.headingDegrees) * 1.3f;
            float found = 0.0f;
            float lateral = 0.0f;
            route.project(point, along - 5.0f, along + 5.0f, found, lateral);
            if (std::abs(found - along) > 0.02f || std::abs(lateral - 1.3f) > 0.02f)
                fail("project() of route " + std::to_string(index) + " misses a point at s = " + std::to_string(along));
        }
        checks += 4;
    }

    if (failures > 40)
        report += "... and " + std::to_string(failures - 40) + " more failures\n";
    if (passed)
    {
        std::size_t truckRoutes = 0;
        for (const RouteInfo& info : routes_)
            truckRoutes += info.allowed[static_cast<std::size_t>(SizeClass::Truck)] ? 1 : 0;
        report = "All " + std::to_string(checks) + " network, route, body, conflict, signal and bus line checks passed (" +
                 std::to_string(network_.junctionCount()) + " junctions, " + std::to_string(routes_.size()) +
                 " routes, " + std::to_string(conflicts_.size()) + " conflict zones, " +
                 std::to_string(network_.crossings().size()) + " crossings, trucks on " +
                 std::to_string(truckRoutes) + " routes, bus line of " + std::to_string(busLine_.size()) + " legs).\n";
    }
    return passed;
}
