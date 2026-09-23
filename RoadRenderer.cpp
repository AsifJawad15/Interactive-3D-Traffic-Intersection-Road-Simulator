#include "RoadRenderer.h"

#include "MeshBuilder.h"
#include "Simulation.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace
{
    constexpr float asphaltTile = 4.0f;
    constexpr float sidewalkTile = 4.0f;
    constexpr float lawnTile = 2.5f;
    constexpr float paintY = RoadNetwork::roadY + 0.012f;
    constexpr float pi = std::numbers::pi_v<float>;

    // The side of a direction of travel that is on the driver's right. The
    // world is right-handed with y up, so right = forward x up: a car heading
    // north (+z) has west (-x) on its right, as the routes' lane offsets say.
    glm::vec2 rightOf(glm::vec2 direction)
    {
        return {-direction.y, direction.x};
    }

    float angleOf(glm::vec2 v)
    {
        return std::atan2(v.y, v.x);
    }

    // Points along a circular arc from angle a to angle b, the shorter way.
    std::vector<glm::vec2> arcPoints(glm::vec2 centre, float radius, float a, float b, int segments)
    {
        float sweep = b - a;
        while (sweep > pi) sweep -= 2.0f * pi;
        while (sweep < -pi) sweep += 2.0f * pi;
        std::vector<glm::vec2> points;
        points.reserve(static_cast<std::size_t>(segments) + 1);
        for (int step = 0; step <= segments; ++step)
        {
            const float angle = a + sweep * static_cast<float>(step) / static_cast<float>(segments);
            points.push_back(centre + radius * glm::vec2{std::cos(angle), std::sin(angle)});
        }
        return points;
    }

    std::vector<glm::vec2> circlePoints(glm::vec2 centre, float radius, int segments)
    {
        std::vector<glm::vec2> points;
        points.reserve(static_cast<std::size_t>(segments));
        for (int step = 0; step < segments; ++step)
        {
            const float angle = 2.0f * pi * static_cast<float>(step) / static_cast<float>(segments);
            points.push_back(centre + radius * glm::vec2{std::cos(angle), std::sin(angle)});
        }
        return points;
    }

    // An open band between two matching polylines (the curve of a bend).
    void addStrip(MeshBuilder& builder, const std::vector<glm::vec2>& outer,
                  const std::vector<glm::vec2>& inner, float y, float tile)
    {
        for (std::size_t index = 0; index + 1 < outer.size() && index + 1 < inner.size(); ++index)
            builder.addFlatQuad(outer[index], outer[index + 1], inner[index + 1], inner[index], y, tile);
    }

    // Dashes along a polyline: `dash` metres painted, `gap` metres left bare.
    void addDashes(MeshBuilder& builder, const std::vector<glm::vec2>& line, float dash, float gap, float width)
    {
        float carried = 0.0f;   // distance into the current dash/gap cycle
        const float cycle = dash + gap;
        for (std::size_t index = 0; index + 1 < line.size(); ++index)
        {
            const glm::vec2 a = line[index];
            const glm::vec2 b = line[index + 1];
            const float length = glm::length(b - a);
            if (length < 1.0e-4f)
                continue;
            const glm::vec2 direction = (b - a) / length;
            float t = 0.0f;
            while (t < length)
            {
                const float inCycle = std::fmod(carried, cycle);
                if (inCycle < dash)
                {
                    const float run = std::min(dash - inCycle, length - t);
                    builder.addRibbon({a + direction * t, a + direction * (t + run)}, width, paintY);
                    t += run;
                    carried += run;
                }
                else
                {
                    const float run = std::min(cycle - inCycle, length - t);
                    t += run;
                    carried += run;
                }
            }
        }
    }

    std::vector<glm::vec2> offsetLine(const std::vector<glm::vec2>& line, float offset)
    {
        // Offsets a straight two-point line sideways (right of its direction).
        const glm::vec2 side = rightOf(glm::normalize(line.back() - line.front())) * offset;
        std::vector<glm::vec2> result;
        for (const glm::vec2& point : line)
            result.push_back(point + side);
        return result;
    }

    // The painted markings every road carries between its junctions: a double
    // yellow centre line, dashed white lines between the two lanes of each
    // direction, and a solid white line near each kerb.
    void addRoadMarkings(MeshBuilder& white, MeshBuilder& yellow, const std::vector<glm::vec2>& centre)
    {
        for (float side : {-1.0f, 1.0f})
        {
            yellow.addRibbon(offsetLine(centre, side * 0.15f), 0.12f, paintY);
            addDashes(white, offsetLine(centre, side * RoadNetwork::laneWidth), 3.0f, 6.0f, 0.12f);
            white.addRibbon(offsetLine(centre, side * 6.7f), 0.15f, paintY);
        }
    }

    // Same markings along a bend: arcs concentric with the curve.
    void addBendMarkings(MeshBuilder& white, MeshBuilder& yellow, glm::vec2 centre, float a, float b)
    {
        constexpr int segments = 36;
        const float r = RoadNetwork::bendRadius;
        for (float side : {-1.0f, 1.0f})
        {
            yellow.addRibbon(arcPoints(centre, r + side * 0.15f, a, b, segments), 0.12f, paintY);
            addDashes(white, arcPoints(centre, r + side * RoadNetwork::laneWidth, a, b, segments), 3.0f, 6.0f, 0.12f);
            white.addRibbon(arcPoints(centre, r + side * 6.7f, a, b, segments), 0.15f, paintY);
        }
    }

    // A zebra crossing across an arm, between `near` and `far` metres out.
    void addZebra(MeshBuilder& white, glm::vec2 junctionCentre, int arm, float near, float far, float skipMiddle)
    {
        const glm::vec2 along = armDirection(arm);
        const glm::vec2 across = rightOf(along);
        const glm::vec2 middle = junctionCentre + along * (0.5f * (near + far));
        for (float offset = -6.3f; offset <= 6.31f; offset += 1.26f)
        {
            if (std::abs(offset) < skipMiddle)
                continue;
            white.addPaintRectangle(middle + across * offset, along, far - near, 0.62f, paintY);
        }
    }

    // A lane arrow: straight on, and/or a turn to one side (+1 right, -1 left).
    void addArrow(MeshBuilder& white, glm::vec2 base, glm::vec2 forward, bool straight, float turnSide)
    {
        const glm::vec2 right = rightOf(forward);
        const float shaftEnd = straight ? 1.0f : 0.4f;
        white.addPaintRectangle(base + forward * (0.5f * (shaftEnd - 2.0f)), forward, shaftEnd + 2.0f, 0.22f, paintY);
        if (straight)
        {
            white.addFlatTriangle(base + forward * 1.0f + right * 0.48f,
                                  base + forward * 1.0f - right * 0.48f,
                                  base + forward * 2.0f, paintY);
        }
        if (turnSide != 0.0f)
        {
            const glm::vec2 elbow = base + forward * 0.2f;
            white.addPaintRectangle(elbow + right * (turnSide * 0.55f), right, 1.1f, 0.22f, paintY);
            const glm::vec2 tip = elbow + right * (turnSide * 1.1f);
            white.addFlatTriangle(tip + forward * 0.45f, tip - forward * 0.45f,
                                  tip + right * (turnSide * 0.8f), paintY);
        }
    }
}

RoadRenderer::RoadRenderer(const TrafficSystem& traffic)
{
    const RoadNetwork& network = traffic.network();
    const std::vector<Junction>& junctions = network.junctions();
    constexpr float y = RoadNetwork::roadY;
    constexpr float top = RoadNetwork::kerbTopY;
    constexpr float half = RoadNetwork::halfWidth;

    MeshBuilder asphaltParts;
    MeshBuilder kerbParts;
    MeshBuilder sidewalkParts;
    MeshBuilder lawnParts;
    MeshBuilder white;
    MeshBuilder yellow;

    // ---- Roads -------------------------------------------------------------
    for (const RoadNetwork::Road& road : network.roads())
    {
        const Junction& from = junctions[road.from];
        const glm::vec2 direction = glm::normalize(road.end - road.start);
        const glm::vec2 side = rightOf(direction);
        const float length = glm::length(road.end - road.start);

        // Stop short of a bend's curve, which is drawn with the bend.
        const bool bendAtStart = from.type == JunctionType::Bend;
        const bool bendAtEnd = road.to >= 0 && junctions[static_cast<std::size_t>(road.to)].type == JunctionType::Bend;
        const glm::vec2 a = road.start + direction * (bendAtStart ? RoadNetwork::bendRadius : 0.0f);
        const glm::vec2 b = road.end - direction * (bendAtEnd ? RoadNetwork::bendRadius : 0.0f);
        asphaltParts.addFlatQuad(a - side * half, b - side * half, b + side * half, a + side * half, y, asphaltTile);

        const float markStart = RoadNetwork::junctionReach(from);
        const float markEnd = road.to >= 0
            ? length - RoadNetwork::junctionReach(junctions[static_cast<std::size_t>(road.to)])
            : 700.0f;
        if (markEnd > markStart)
            addRoadMarkings(white, yellow, {road.start + direction * markStart, road.start + direction * markEnd});
    }

    // ---- Junctions ---------------------------------------------------------
    for (std::size_t index = 0; index < junctions.size(); ++index)
    {
        const Junction& junction = junctions[index];
        const glm::vec2 c = junction.centre;

        if (junction.isIntersection())
        {
            asphaltParts.addFlatQuad(c + glm::vec2{-half, -half}, c + glm::vec2{half, -half},
                                c + glm::vec2{half, half}, c + glm::vec2{-half, half}, y, asphaltTile);

            // Rounded road corners between neighbouring arms: the road side of
            // the kerb fillet (the block's sidewalk covers the other side).
            for (float qx : {-1.0f, 1.0f})
            {
                for (float qz : {-1.0f, 1.0f})
                {
                    const int armX = qx > 0.0f ? ArmEast : ArmWest;
                    const int armZ = qz > 0.0f ? ArmNorth : ArmSouth;
                    if (!junction.hasArm[static_cast<std::size_t>(armX)] || !junction.hasArm[static_cast<std::size_t>(armZ)])
                        continue;
                    const float reach = RoadNetwork::boxHalf;
                    const glm::vec2 filletCentre = c + glm::vec2{qx * reach, qz * reach};
                    const glm::vec2 apex = c + glm::vec2{qx * half, qz * half};
                    std::vector<glm::vec2> outline {apex};
                    const std::vector<glm::vec2> arc = arcPoints(
                        filletCentre, RoadNetwork::cornerRadius,
                        angleOf({0.0f, -qz}), angleOf({-qx, 0.0f}), 10);
                    outline.insert(outline.end(), arc.begin(), arc.end());
                    asphaltParts.addFlatPolygon(outline, y, asphaltTile, &apex);
                }
            }

            for (int arm = 0; arm < 4; ++arm)
            {
                if (!junction.hasArm[static_cast<std::size_t>(arm)])
                    continue;
                addZebra(white, c, arm, RoadNetwork::crossingNear, RoadNetwork::crossingFar, 0.0f);

                // Lane arrows, 30 m out, for the moves each lane may make.
                const glm::vec2 forward = -armDirection(arm);
                const glm::vec2 right = rightOf(forward);
                const bool straight = junction.hasArm[static_cast<std::size_t>((arm + 2) % 4)];
                const bool rightTurn = junction.hasArm[static_cast<std::size_t>((arm + 1) % 4)];
                const bool leftTurn = junction.hasArm[static_cast<std::size_t>((arm + 3) % 4)];
                const glm::vec2 base = c + armDirection(arm) * 30.0f;
                if (straight || leftTurn)
                    addArrow(white, base + right * RoadNetwork::laneOffsets[0], forward, straight, leftTurn ? -1.0f : 0.0f);
                if (straight || rightTurn)
                    addArrow(white, base + right * RoadNetwork::laneOffsets[1], forward, straight, rightTurn ? 1.0f : 0.0f);
            }
        }
        else if (junction.type == JunctionType::Roundabout)
        {
            asphaltParts.addFlatPolygon(circlePoints(c, RoadNetwork::roundaboutRadius, 72), y, asphaltTile);
            white.addRibbon([&]()
            {
                std::vector<glm::vec2> ring = circlePoints(c, RoadNetwork::islandRadius + 0.7f, 72);
                ring.push_back(ring.front());
                return ring;
            }(), 0.15f, paintY);

            for (int arm = 0; arm < 4; ++arm)
            {
                const std::vector<glm::vec2> island = network.splitterIsland(index, arm);
                const glm::vec2 apex = c + armDirection(arm) * 17.0f;
                sidewalkParts.addFlatPolygon(island, top, sidewalkTile, &apex);
                kerbParts.addWall(island, y, top);
                addZebra(white, c, arm, RoadNetwork::roundaboutCrossingNear, RoadNetwork::roundaboutCrossingFar, 0.8f);
            }
        }
        else
        {
            // Bend: the road follows a quarter circle between its two arms.
            glm::vec2 arms[2] {};
            int found = 0;
            for (int arm = 0; arm < 4 && found < 2; ++arm)
            {
                if (junction.hasArm[static_cast<std::size_t>(arm)])
                    arms[found++] = armDirection(arm);
            }
            const float r = RoadNetwork::bendRadius;
            const glm::vec2 centre = c + (arms[0] + arms[1]) * r;
            const float a = angleOf(c + arms[0] * r - centre);
            const float b = angleOf(c + arms[1] * r - centre);
            addStrip(asphaltParts, arcPoints(centre, r + half, a, b, 36), arcPoints(centre, r - half, a, b, 36), y, asphaltTile);
            addBendMarkings(white, yellow, centre, a, b);
        }
    }

    // ---- City blocks: kerb, sidewalk, lawn -----------------------------------
    for (std::size_t block = 0; block < network.blockCount(); ++block)
    {
        const std::vector<glm::vec2> kerb = network.blockOutline(block, 0.0f);
        const std::vector<glm::vec2> inner = network.blockOutline(block, RoadNetwork::sidewalkWidth);
        kerbParts.addWall(kerb, y, top);
        sidewalkParts.addFlatRing(kerb, inner, top, sidewalkTile);
        lawnParts.addFlatPolygon(inner, top + 0.01f, lawnTile);
    }

    // ---- Stop and give-way lines, exactly where the traffic waits --------------
    for (const StopMarking& marking : traffic.stopMarkings())
    {
        const glm::vec2 side = rightOf(marking.direction);
        if (!marking.giveWay)
        {
            white.addPaintRectangle(marking.position, marking.direction, 0.45f, 3.3f, paintY);
        }
        else
        {
            for (float step : {-1.5f, -0.5f, 0.5f, 1.5f})
                white.addPaintRectangle(marking.position + side * (step * 0.85f), marking.direction, 0.35f, 0.55f, paintY);
        }
    }

    asphalt_ = asphaltParts.build();
    kerbs_ = kerbParts.build();
    sidewalks_ = sidewalkParts.build();
    lawns_ = lawnParts.build();
    whitePaint_ = white.build();
    yellowPaint_ = yellow.build();
}
