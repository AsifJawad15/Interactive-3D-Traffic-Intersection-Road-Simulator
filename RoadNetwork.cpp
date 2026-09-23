#include "RoadNetwork.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace
{
    constexpr int arcSegments = 12;

    // Rotating the south arm onto `arm`, in whole quarter turns. The same
    // rotation Route::rotated uses: +90 degrees maps south to west, west to
    // north, north to east and east to south.
    glm::vec2 rotateFromSouth(glm::vec2 point, int arm)
    {
        const int quarterTurns = (arm - ArmSouth + 4) % 4;
        const float phi = static_cast<float>(quarterTurns) * 0.5f * std::numbers::pi_v<float>;
        const float c = std::cos(phi);
        const float s = std::sin(phi);
        return {point.x * c + point.y * s, -point.x * s + point.y * c};
    }

    // Points along a circular arc from angle a to angle b (radians), taking
    // the shorter way round, both ends included.
    void appendArc(std::vector<glm::vec2>& points, glm::vec2 centre, float radius, float a, float b)
    {
        float sweep = b - a;
        const float pi = std::numbers::pi_v<float>;
        while (sweep > pi) sweep -= 2.0f * pi;
        while (sweep < -pi) sweep += 2.0f * pi;
        for (int step = 0; step <= arcSegments; ++step)
        {
            const float angle = a + sweep * static_cast<float>(step) / static_cast<float>(arcSegments);
            points.push_back(centre + radius * glm::vec2{std::cos(angle), std::sin(angle)});
        }
    }
}

glm::vec2 armDirection(int arm)
{
    switch (arm)
    {
    case ArmNorth: return {0.0f, 1.0f};
    case ArmEast: return {1.0f, 0.0f};
    case ArmSouth: return {0.0f, -1.0f};
    default: return {-1.0f, 0.0f};
    }
}

const char* armName(int arm)
{
    switch (arm)
    {
    case ArmNorth: return "N";
    case ArmEast: return "E";
    case ArmSouth: return "S";
    default: return "W";
    }
}

// ---------------------------------------------------------------------------
// The city
// ---------------------------------------------------------------------------

RoadNetwork RoadNetwork::makeCity()
{
    //                 | road out of town (north)
    //      B ---------X1--------- B
    //      |          |          |
    //      G ---------X0-------- R1 ---- road out of town (east)
    //      |          |          |
    //  ---ST ---------R2--------- B
    //  road out (west)|  road out of town (south)
    RoadNetwork network;
    const float d = spacing;

    const auto add = [&network](const char* name, JunctionType type, glm::vec2 centre)
    {
        Junction junction;
        junction.name = name;
        junction.type = type;
        junction.centre = centre;
        network.junctions_.push_back(junction);
        return network.junctions_.size() - 1;
    };

    const std::size_t x0 = add("X0", JunctionType::SignalCross, {0.0f, 0.0f});
    const std::size_t x1 = add("X1", JunctionType::SignalCross, {0.0f, d});
    const std::size_t r1 = add("R1", JunctionType::Roundabout, {d, 0.0f});
    const std::size_t r2 = add("R2", JunctionType::Roundabout, {0.0f, -d});
    const std::size_t g = add("G", JunctionType::GiveWayT, {-d, 0.0f});
    const std::size_t st = add("ST", JunctionType::SignalT, {-d, -d});
    const std::size_t nw = add("NW bend", JunctionType::Bend, {-d, d});
    const std::size_t ne = add("NE bend", JunctionType::Bend, {d, d});
    const std::size_t se = add("SE bend", JunctionType::Bend, {d, -d});
    network.centralJunction_ = x0;
    network.junctions_[r1].fountain = true;

    // Grid roads.
    network.connect(x0, ArmNorth, x1, ArmSouth);
    network.connect(x0, ArmEast, r1, ArmWest);
    network.connect(x0, ArmSouth, r2, ArmNorth);
    network.connect(x0, ArmWest, g, ArmEast);
    network.connect(x1, ArmWest, nw, ArmEast);
    network.connect(x1, ArmEast, ne, ArmWest);
    network.connect(r1, ArmNorth, ne, ArmSouth);
    network.connect(r1, ArmSouth, se, ArmNorth);
    network.connect(r2, ArmWest, st, ArmEast);
    network.connect(r2, ArmEast, se, ArmWest);
    network.connect(g, ArmNorth, nw, ArmSouth);
    network.connect(g, ArmSouth, st, ArmNorth);

    // Roads out of town.
    network.leaveTown(x1, ArmNorth);
    network.leaveTown(r1, ArmEast);
    network.leaveTown(r2, ArmSouth);
    network.leaveTown(st, ArmWest);

    // At the give-way T-junction the loop road has priority over the side road.
    network.junctions_[g].majorArm[ArmNorth] = true;
    network.junctions_[g].majorArm[ArmSouth] = true;

    network.blocks_ = {
        Block{{-d, 0.0f}, {0.0f, d}},
        Block{{0.0f, 0.0f}, {d, d}},
        Block{{-d, -d}, {0.0f, 0.0f}},
        Block{{0.0f, -d}, {d, 0.0f}}
    };

    network.placeLamps();
    return network;
}

void RoadNetwork::connect(std::size_t a, int armA, std::size_t b, int armB)
{
    Junction& first = junctions_[a];
    Junction& second = junctions_[b];
    const float half = glm::length(second.centre - first.centre) * 0.5f;
    first.hasArm[static_cast<std::size_t>(armA)] = true;
    first.armLength[static_cast<std::size_t>(armA)] = half;
    second.hasArm[static_cast<std::size_t>(armB)] = true;
    second.armLength[static_cast<std::size_t>(armB)] = half;
    roads_.push_back({a, armA, static_cast<int>(b), armB, first.centre, second.centre});
}

void RoadNetwork::leaveTown(std::size_t junction, int arm)
{
    Junction& j = junctions_[junction];
    j.hasArm[static_cast<std::size_t>(arm)] = true;
    j.armLength[static_cast<std::size_t>(arm)] = outOfTownLength;
    j.leavesTown[static_cast<std::size_t>(arm)] = true;
    roads_.push_back({junction, arm, -1, 0, j.centre, j.centre + armDirection(arm) * visibleRoadLength});
}

int RoadNetwork::junctionAt(glm::vec2 centre) const
{
    for (std::size_t index = 0; index < junctions_.size(); ++index)
    {
        if (glm::length(junctions_[index].centre - centre) < 0.5f)
            return static_cast<int>(index);
    }
    return -1;
}

float RoadNetwork::junctionReach(const Junction& junction)
{
    switch (junction.type)
    {
    case JunctionType::Roundabout: return 31.5f;
    case JunctionType::Bend: return bendRadius;
    default: return stopLine + 0.5f;
    }
}

// ---------------------------------------------------------------------------
// Blocks and islands
// ---------------------------------------------------------------------------

void RoadNetwork::appendCorner(std::vector<glm::vec2>& outline, glm::vec2 corner, glm::vec2 quadrant,
                               float inset, bool reverse) const
{
    const int index = junctionAt(corner);
    const JunctionType type = index >= 0 ? junctions_[static_cast<std::size_t>(index)].type : JunctionType::Bend;
    std::vector<glm::vec2> points;

    if (type == JunctionType::Roundabout)
    {
        // The block stops at the circle of the roundabout's outer kerb, cut
        // off where it meets the two straight road edges.
        const float radius = roundaboutRadius + inset;
        const float edge = halfWidth + inset;
        const float along = std::sqrt(std::max(radius * radius - edge * edge, 0.0f));
        const glm::vec2 onVertical = corner + glm::vec2{quadrant.x * edge, quadrant.y * along};
        const glm::vec2 onHorizontal = corner + glm::vec2{quadrant.x * along, quadrant.y * edge};
        const glm::vec2 a = onVertical - corner;
        const glm::vec2 b = onHorizontal - corner;
        appendArc(points, corner, radius, std::atan2(a.y, a.x), std::atan2(b.y, b.x));
    }
    else
    {
        // A kerb fillet (intersections) or the inside of a loop bend. Both are
        // a quarter circle tangent to the two road edges, centred C metres in
        // from the junction along both axes.
        const float reach = type == JunctionType::Bend ? bendRadius : boxHalf;
        const glm::vec2 centre = corner + quadrant * reach;
        const float radius = std::max(reach - halfWidth - inset, 0.3f);
        appendArc(points, centre, radius,
                  std::atan2(0.0f, -quadrant.x), std::atan2(-quadrant.y, 0.0f));
    }

    if (reverse)
        std::reverse(points.begin(), points.end());
    outline.insert(outline.end(), points.begin(), points.end());
}

std::vector<glm::vec2> RoadNetwork::blockOutline(std::size_t block, float inset) const
{
    const Block& b = blocks_[block];
    std::vector<glm::vec2> outline;
    outline.reserve(4 * (arcSegments + 1));

    // Counter-clockwise seen from above (x east, z north): south-west,
    // south-east, north-east, north-west. Each corner piece runs from the
    // side it arrives on to the side it leaves by.
    appendCorner(outline, {b.minimum.x, b.minimum.y}, {1.0f, 1.0f}, inset, false);
    appendCorner(outline, {b.maximum.x, b.minimum.y}, {-1.0f, 1.0f}, inset, true);
    appendCorner(outline, {b.maximum.x, b.maximum.y}, {-1.0f, -1.0f}, inset, false);
    appendCorner(outline, {b.minimum.x, b.maximum.y}, {1.0f, -1.0f}, inset, true);
    return outline;
}

std::vector<glm::vec2> RoadNetwork::splitterIsland(std::size_t junction, int arm) const
{
    // Authored for the south arm (road running out along -z), between the
    // entry lanes (x < 0) and the exit lanes (x > 0); rotated onto the arm.
    // It widens where the entry and exit arcs curve apart and stops short of
    // the circulating lane.
    static const std::array<glm::vec2, 11> canonical = {
        glm::vec2{0.0f, -31.4f}, glm::vec2{-0.4f, -31.0f}, glm::vec2{-0.4f, -19.0f},
        glm::vec2{-0.75f, -16.6f}, glm::vec2{-0.45f, -15.4f}, glm::vec2{0.0f, -15.1f},
        glm::vec2{0.45f, -15.4f}, glm::vec2{0.75f, -16.6f}, glm::vec2{0.4f, -19.0f},
        glm::vec2{0.4f, -31.0f}, glm::vec2{0.0f, -31.4f}
    };

    const glm::vec2 centre = junctions_[junction].centre;
    std::vector<glm::vec2> outline;
    outline.reserve(canonical.size() - 1);
    for (std::size_t index = 0; index + 1 < canonical.size(); ++index)
        outline.push_back(centre + rotateFromSouth(canonical[index], arm));
    return outline;
}

// ---------------------------------------------------------------------------
// Street lamps
// ---------------------------------------------------------------------------

void RoadNetwork::placeLamps()
{
    lamps_.clear();
    constexpr float lampOffset = halfWidth + 1.2f;

    for (const Road& road : roads_)
    {
        const Junction& from = junctions_[road.from];
        const glm::vec2 direction = glm::normalize(road.end - road.start);
        const glm::vec2 side {direction.y, -direction.x};

        if (road.to < 0)
        {
            // Out of town: one side only, thinning out into the countryside.
            for (float t = junctionReach(from) + 6.0f; t < 200.0f; t += 36.0f)
            {
                const glm::vec2 p = road.start + direction * t + side * lampOffset;
                lamps_.push_back({{p.x, roadY, p.y}, false});
            }
            continue;
        }

        const Junction& to = junctions_[static_cast<std::size_t>(road.to)];
        const float length = glm::length(road.end - road.start);
        const float first = junctionReach(from) + 4.0f;
        const float last = length - junctionReach(to) - 4.0f;

        // Staggered: every 30 m on each side, the two sides offset by 15 m.
        int index = 0;
        for (float t = first; t <= last; t += 15.0f, ++index)
        {
            const float sideSign = index % 2 == 0 ? 1.0f : -1.0f;
            const glm::vec2 p = road.start + direction * t + side * (lampOffset * sideSign);
            lamps_.push_back({{p.x, kerbTopY, p.y}, false});
        }
    }

    for (std::size_t index = 0; index < junctions_.size(); ++index)
    {
        const Junction& junction = junctions_[index];
        for (float sx : {-1.0f, 1.0f})
        {
            for (float sz : {-1.0f, 1.0f})
            {
                const int armX = sx > 0.0f ? ArmEast : ArmWest;
                const int armZ = sz > 0.0f ? ArmNorth : ArmSouth;
                const bool corner = junction.hasArm[static_cast<std::size_t>(armX)] &&
                                    junction.hasArm[static_cast<std::size_t>(armZ)];
                if (!corner || junction.type == JunctionType::Bend)
                    continue;

                glm::vec2 offset {sx * 13.0f, sz * 13.0f};
                if (junction.type == JunctionType::Roundabout)
                    offset = glm::vec2{sx, sz} * 13.8f;
                // The four lamps of the central crossroads are the Lab 3 lamps.
                const bool lab = index == centralJunction_;
                if (lab)
                    offset = glm::vec2{sx, sz} * 11.5f;
                const glm::vec2 p = junction.centre + offset;
                lamps_.push_back({{p.x, kerbTopY, p.y}, lab});
            }
        }
    }
}
