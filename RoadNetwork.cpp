#include "RoadNetwork.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

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
    // A closed maze of roads 400 m across: the original 3 x 3 core (X0 ...
    // SE bend) inside a ring road, joined to it by six links, so there are
    // many ways between any two places and no road leads out of town.
    //
    //   z=+200  NWc ----- G4 ------ ST2 ------------------ NEc
    //            |         |         |                      |
    //   z=+100   |        G2 ------- X1 ----- NE bend       |
    //            |         |         |           |          |
    //   z=   0   |         G ------- X0 ------- R1 ------- G5
    //            |         |         |           |          |
    //   z=-100  G7 ------- ST ------ R2 -------- G3 ------ G6
    //            |                   |                      |
    //   z=-200  SWc --------------- ST3 ------------------ SEc
    //
    //         x=-200    -100         0         +100       +200
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

    // The core.
    const std::size_t x0 = add("X0", JunctionType::SignalCross, {0.0f, 0.0f});
    const std::size_t x1 = add("X1", JunctionType::SignalCross, {0.0f, d});
    const std::size_t r1 = add("R1", JunctionType::Roundabout, {d, 0.0f});
    const std::size_t r2 = add("R2", JunctionType::Roundabout, {0.0f, -d});
    const std::size_t g = add("G", JunctionType::GiveWayT, {-d, 0.0f});
    const std::size_t st = add("ST", JunctionType::SignalT, {-d, -d});
    const std::size_t g2 = add("G2", JunctionType::GiveWayT, {-d, d});
    const std::size_t ne = add("NE bend", JunctionType::Bend, {d, d});
    const std::size_t g3 = add("G3", JunctionType::GiveWayT, {d, -d});
    // The ring road.
    const std::size_t g4 = add("G4", JunctionType::GiveWayT, {-d, 2.0f * d});
    const std::size_t st2 = add("ST2", JunctionType::SignalT, {0.0f, 2.0f * d});
    const std::size_t g5 = add("G5", JunctionType::GiveWayT, {2.0f * d, 0.0f});
    const std::size_t g6 = add("G6", JunctionType::GiveWayT, {2.0f * d, -d});
    const std::size_t st3 = add("ST3", JunctionType::SignalT, {0.0f, -2.0f * d});
    const std::size_t g7 = add("G7", JunctionType::GiveWayT, {-2.0f * d, -d});
    const std::size_t nwc = add("NW corner", JunctionType::Bend, {-2.0f * d, 2.0f * d});
    const std::size_t nec = add("NE corner", JunctionType::Bend, {2.0f * d, 2.0f * d});
    const std::size_t sec = add("SE corner", JunctionType::Bend, {2.0f * d, -2.0f * d});
    const std::size_t swc = add("SW corner", JunctionType::Bend, {-2.0f * d, -2.0f * d});
    network.centralJunction_ = x0;
    network.junctions_[r1].fountain = true;

    // Core roads.
    network.connect(x0, ArmNorth, x1, ArmSouth);
    network.connect(x0, ArmEast, r1, ArmWest);
    network.connect(x0, ArmSouth, r2, ArmNorth);
    network.connect(x0, ArmWest, g, ArmEast);
    network.connect(x1, ArmWest, g2, ArmEast);
    network.connect(x1, ArmEast, ne, ArmWest);
    network.connect(r1, ArmNorth, ne, ArmSouth);
    network.connect(r1, ArmSouth, g3, ArmNorth);
    network.connect(r2, ArmWest, st, ArmEast);
    network.connect(r2, ArmEast, g3, ArmWest);
    network.connect(g, ArmNorth, g2, ArmSouth);
    network.connect(g, ArmSouth, st, ArmNorth);

    // Links from the core out to the ring.
    network.connect(x1, ArmNorth, st2, ArmSouth);
    network.connect(r1, ArmEast, g5, ArmWest);
    network.connect(r2, ArmSouth, st3, ArmNorth);
    network.connect(st, ArmWest, g7, ArmEast);
    network.connect(g2, ArmNorth, g4, ArmSouth);
    network.connect(g3, ArmEast, g6, ArmWest);

    // The ring, clockwise from the north-west corner.
    network.connect(nwc, ArmEast, g4, ArmWest);
    network.connect(g4, ArmEast, st2, ArmWest);
    network.connect(st2, ArmEast, nec, ArmWest);
    network.connect(nec, ArmSouth, g5, ArmNorth);
    network.connect(g5, ArmSouth, g6, ArmNorth);
    network.connect(g6, ArmSouth, sec, ArmNorth);
    network.connect(sec, ArmWest, st3, ArmEast);
    network.connect(st3, ArmWest, swc, ArmEast);
    network.connect(swc, ArmNorth, g7, ArmSouth);
    network.connect(g7, ArmNorth, nwc, ArmSouth);

    // At every give-way T-junction the straight road through it has priority
    // over the side road.
    for (Junction& junction : network.junctions_)
    {
        if (junction.type != JunctionType::GiveWayT)
            continue;
        for (int arm = 0; arm < 4; ++arm)
        {
            const auto index = static_cast<std::size_t>(arm);
            junction.majorArm[index] = junction.hasArm[index] && junction.hasArm[static_cast<std::size_t>((arm + 2) % 4)];
        }
    }

    // The blocks, as the grid cells each one covers.
    network.blocks_ = {
        // The four blocks of the core.
        Block{{{-1, 0}}}, Block{{{0, 0}}}, Block{{{-1, -1}}}, Block{{{0, -1}}},
        // Between the core and the ring.
        Block{{{-2, -1}, {-2, 0}, {-2, 1}}},   // west, tall
        Block{{{-1, 1}}},                      // north-west
        Block{{{0, 1}, {1, 1}, {1, 0}}},       // north-east, L-shaped round the NE bend
        Block{{{1, -1}}},                      // east
        Block{{{0, -2}, {1, -2}}},             // south-east, wide
        Block{{{-2, -2}, {-1, -2}}}            // south-west, wide
    };

    network.placeLamps();
    network.placeCrossings();
    return network;
}

float Crossing::endAcross(int end) const
{
    if (end == refugeEnd)
        return 0.0f;   // the middle of the splitter island
    // On the sidewalk, far enough behind the kerb that the overhang of a
    // bus or truck finishing a turn (up to 0.8 m over the kerb) misses.
    constexpr float behindKerb = 1.15f;
    return end == 0 ? from - behindKerb : to + behindKerb;
}

void RoadNetwork::placeCrossings()
{
    // Exactly where the zebras are painted (RoadRenderer.cpp): across every
    // arm of a crossroads or T-junction just beyond the kerb corners, and
    // across every roundabout arm in two halves either side of the island,
    // which is 0.8 m wide there - room for a walker to wait in the middle.
    crossings_.clear();
    for (std::size_t index = 0; index < junctions_.size(); ++index)
    {
        const Junction& junction = junctions_[index];
        const bool roundabout = junction.type == JunctionType::Roundabout;
        if (!junction.isIntersection() && !roundabout)
            continue;
        for (int arm = 0; arm < 4; ++arm)
        {
            if (!junction.hasArm[static_cast<std::size_t>(arm)])
                continue;
            Crossing crossing;
            crossing.junction = index;
            crossing.arm = arm;
            crossing.signalised = junction.isSignalised();
            crossing.walksWithNorthSouth = arm == ArmEast || arm == ArmWest;
            crossing.along = armDirection(arm);
            crossing.across = {crossing.along.y, -crossing.along.x};
            const float near = roundabout ? roundaboutCrossingNear : crossingNear;
            const float far = roundabout ? roundaboutCrossingFar : crossingFar;
            crossing.origin = junction.centre + crossing.along * (0.5f * (near + far));
            crossing.halfWidth = 0.5f * (far - near);
            if (!roundabout)
            {
                crossing.from = -halfWidth;
                crossing.to = halfWidth;
                crossings_.push_back(crossing);
                continue;
            }
            constexpr float islandHalf = 0.4f;
            crossing.from = -halfWidth;
            crossing.to = -islandHalf;
            crossing.refugeEnd = 1;
            crossings_.push_back(crossing);
            crossing.from = islandHalf;
            crossing.to = halfWidth;
            crossing.refugeEnd = 0;
            crossings_.push_back(crossing);
        }
    }
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
    roads_.push_back({a, armA, b, armB, first.centre, second.centre});
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

void RoadNetwork::appendOuterBend(std::vector<glm::vec2>& outline, glm::vec2 corner, glm::vec2 in, glm::vec2 out,
                                  float inset) const
{
    // The kerb here is on the outside of a bend's curve: the bend's two arms
    // point back along `in` and on along `out`, and the kerb is a quarter
    // circle round the same centre as the road, beyond its outer edge.
    const glm::vec2 centre = corner + (out - in) * bendRadius;
    const glm::vec2 leftOfIn {-in.y, in.x};
    const glm::vec2 leftOfOut {-out.y, out.x};
    appendArc(outline, centre, bendRadius + halfWidth + inset,
              std::atan2(leftOfIn.y, leftOfIn.x), std::atan2(leftOfOut.y, leftOfOut.x));
}

std::vector<glm::vec2> RoadNetwork::traceOutline(const std::vector<Cell>& cells, float inset, bool outside) const
{
    const auto contains = [&cells](int x, int z)
    {
        return std::any_of(cells.begin(), cells.end(), [x, z](const Cell& cell) { return cell.x == x && cell.z == z; });
    };

    // Every cell side with no cell of the set beyond it is on the boundary.
    // Each one is directed so the set is on its left (counter-clockwise), or
    // the other way round for the ground outside.
    struct Edge
    {
        glm::ivec2 from;
        glm::ivec2 to;
    };
    std::vector<Edge> edges;
    for (const Cell& cell : cells)
    {
        const glm::ivec2 sw {cell.x, cell.z};
        const glm::ivec2 se {cell.x + 1, cell.z};
        const glm::ivec2 ne {cell.x + 1, cell.z + 1};
        const glm::ivec2 nw {cell.x, cell.z + 1};
        if (!contains(cell.x, cell.z - 1)) edges.push_back({sw, se});
        if (!contains(cell.x + 1, cell.z)) edges.push_back({se, ne});
        if (!contains(cell.x, cell.z + 1)) edges.push_back({ne, nw});
        if (!contains(cell.x - 1, cell.z)) edges.push_back({nw, sw});
    }
    if (outside)
    {
        for (Edge& edge : edges)
            std::swap(edge.from, edge.to);
    }

    // Chain the edges into one loop of grid points. Blocks are simple shapes
    // with no holes and no two corners touching, so every grid point on the
    // boundary has exactly one edge leaving it.
    std::vector<glm::ivec2> loop;
    loop.push_back(edges.front().from);
    glm::ivec2 current = edges.front().to;
    while (current != loop.front() && loop.size() <= edges.size())
    {
        loop.push_back(current);
        const auto next = std::find_if(edges.begin(), edges.end(), [current](const Edge& edge) { return edge.from == current; });
        current = next->to;
    }

    // Each grid point where the boundary turns is a corner of the kerb. Turning
    // left, the area is on the inside of the corner (a kerb fillet, the inside
    // of a bend, or a roundabout); turning right, it wraps round the outside of
    // a bend. Where the boundary runs straight on, the kerb is straight too.
    std::vector<glm::vec2> outline;
    for (std::size_t index = 0; index < loop.size(); ++index)
    {
        const glm::ivec2 previous = loop[(index + loop.size() - 1) % loop.size()];
        const glm::ivec2 point = loop[index];
        const glm::ivec2 next = loop[(index + 1) % loop.size()];
        const glm::vec2 in = glm::vec2(point - previous);
        const glm::vec2 out = glm::vec2(next - point);
        const float turn = in.x * out.y - in.y * out.x;
        const glm::vec2 corner = glm::vec2(point) * spacing;
        if (turn > 0.0f)
            appendCorner(outline, corner, out - in, inset, in.x != 0.0f);
        else if (turn < 0.0f)
            appendOuterBend(outline, corner, in, out, inset);
    }
    return outline;
}

std::vector<glm::vec2> RoadNetwork::blockOutline(std::size_t block, float inset) const
{
    return traceOutline(blocks_[block].cells, inset, false);
}

std::vector<glm::vec2> RoadNetwork::outsideOutline(float inset) const
{
    std::vector<Cell> city;
    for (int x = -gridHalf; x < gridHalf; ++x)
    {
        for (int z = -gridHalf; z < gridHalf; ++z)
            city.push_back({x, z});
    }
    return traceOutline(city, inset, true);
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
        const Junction& to = junctions_[road.to];
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
