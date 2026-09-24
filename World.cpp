#include "World.h"

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <cmath>

namespace
{
    glm::vec2 facingVector(float degrees)
    {
        const float a = glm::radians(degrees);
        return {std::sin(a), std::cos(a)};
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

const std::vector<std::string>& World::billboardText()
{
    static const std::vector<std::string> text = {
        "GRAPHICS LAB|CSE 4102",
        "DRIVE SAFE|KEEP YOUR LANE",
        "CITY FM|98.4",
        "VISIT THE|FOUNTAIN",
        "OPEN 24H|CAFE + PIZZA",
        "GIVE WAY|AT ROUNDABOUTS"
    };
    return text;
}

glm::vec2 World::shelterPole(const BusShelter& shelter)
{
    // Beyond the advertising end of the shelter, a metre towards the road.
    const glm::vec2 facing = facingVector(shelter.facingDegrees);
    const glm::vec2 along {facing.y, -facing.x};
    return shelter.centre + along * (0.5f * shelterLength + 1.2f) + facing * 1.0f;
}

World World::make(const RoadNetwork& network, const std::vector<BusStopSite>& busStops)
{
    World world;

    // ---- The central crossroads, X0: the original buildings and props ------
    world.buildings_ = {
        {{-24.0f, 5.7f, -24.0f}, {12.0f, 11.0f, 11.0f}, {0.92f, 0.78f, 0.68f}},
        {{-35.0f, 4.2f, -18.0f}, {7.0f, 8.0f, 9.0f}, {0.76f, 0.82f, 0.88f}},
        {{24.0f, 7.2f, -24.0f}, {12.0f, 14.0f, 11.0f}, {0.78f, 0.86f, 0.92f}},
        {{35.0f, 4.7f, -18.0f}, {7.0f, 9.0f, 9.0f}, {0.92f, 0.76f, 0.64f}},
        {{-24.0f, 6.2f, 24.0f}, {12.0f, 12.0f, 11.0f}, {0.84f, 0.88f, 0.78f}},
        {{-35.0f, 5.2f, 18.0f}, {7.0f, 10.0f, 9.0f}, {0.90f, 0.70f, 0.62f}},
        {{24.0f, 5.2f, 24.0f}, {12.0f, 10.0f, 11.0f}, {0.90f, 0.82f, 0.72f}},
        {{35.0f, 6.7f, 18.0f}, {7.0f, 13.0f, 9.0f}, {0.72f, 0.82f, 0.90f}}
    };

    // Trees line the four approaches, clear of the carriageway, the signal
    // heads, the street lamps and the buildings. The four on the drivers'
    // right, where the signal heads stand, are set back on the lawn.
    const glm::vec2 treePositions[] = {
        {-14.0f, -19.0f}, {9.5f, -16.0f}, {-9.5f, 16.0f}, {14.0f, 19.0f},
        {-16.0f, -9.5f}, {-19.0f, 14.0f}, {19.0f, -14.0f}, {16.0f, 9.5f},
        {-9.5f, -27.0f}, {9.5f, 27.0f}, {-27.0f, 9.5f}, {27.0f, -9.5f}
    };
    int treeIndex = 0;
    for (const glm::vec2& position : treePositions)
    {
        // A small per-tree scale and twist stops identical copies from
        // reading as wallpaper.
        world.trees_.push_back({position, 0.86f + 0.06f * static_cast<float>(treeIndex % 4),
                                static_cast<float>(treeIndex) * 37.0f});
        ++treeIndex;
    }

    // Roadside crates carrying the Lab 4 diffuse + specular pair.
    const glm::vec3 cratePositions[] = {
        {-13.5f, 0.72f, -13.5f}, {-12.0f, 0.72f, -15.6f}, {13.5f, 0.72f, 13.5f},
        {15.6f, 0.72f, 12.0f}, {-14.6f, 0.72f, 14.6f}, {14.6f, 0.72f, -14.6f}
    };
    int crateIndex = 0;
    for (const glm::vec3& position : cratePositions)
        world.crates_.push_back({position, 17.0f * static_cast<float>(crateIndex++)});

    world.roadSigns_ = {
        {{-9.2f, 0.22f, -13.0f}, 0.0f, {0.86f, 0.16f, 0.12f}},
        {{9.2f, 0.22f, 13.0f}, 180.0f, {0.86f, 0.16f, 0.12f}},
        {{-13.0f, 0.22f, 9.2f}, 90.0f, {0.14f, 0.32f, 0.72f}},
        {{13.0f, 0.22f, -9.2f}, -90.0f, {0.14f, 0.32f, 0.72f}}
    };

    // Neon lettering above the doors of the X0 buildings, facing the street.
    world.neonSigns_ = {
        {{-24.0f, 4.4f, -18.38f}, 0.0f, "HOTEL", {1.00f, 0.16f, 0.55f}, 1.0f, false},
        {{24.0f, 4.4f, -18.38f}, 0.0f, "CAFE", {0.10f, 0.85f, 1.00f}, 1.0f, false},
        {{-24.0f, 4.4f, 18.38f}, 180.0f, "PIZZA", {1.00f, 0.45f, 0.08f}, 1.0f, false},
        {{24.0f, 4.4f, 18.38f}, 180.0f, "CINEMA", {0.70f, 0.28f, 1.00f}, 1.0f, false},
        {{-35.0f, 3.6f, -13.38f}, 0.0f, "BAR", {1.00f, 0.12f, 0.10f}, 0.7f, true},
        {{-35.0f, 3.6f, 13.38f}, 180.0f, "24H", {0.25f, 1.00f, 0.35f}, 0.7f, false}
    };

    // ---- Billboards on the lawns, facing the road -------------------------
    // The first stands at the central crossroads and is lit by the spot lamp.
    world.billboards_ = {
        {{-50.0f, -15.0f}, 0.0f, 0, true},
        {{50.0f, 15.0f}, 180.0f, 1, false},
        {{-100.0f, -218.0f}, 0.0f, 2, false},
        {{100.0f, 218.0f}, 180.0f, 3, false},
        {{218.0f, 60.0f}, -90.0f, 4, false},
        {{-218.0f, 50.0f}, 90.0f, 5, false}
    };
    {
        // The lamp sits on an arm 1.9 m out from the foot of the panel and
        // shines up at the middle of the picture, so its cone lands on the
        // picture and not on the grass.
        const Billboard& lit = world.billboards_.front();
        const glm::vec2 facing = facingVector(lit.facingDegrees);
        const glm::vec2 lamp = lit.centre + facing * 1.9f;
        world.spotLamp_.position = {lamp.x, billboardBottom - 0.35f, lamp.y};
        world.spotLamp_.target = {lit.centre.x, billboardBottom + 0.5f * billboardHeight, lit.centre.y};
    }

    // ---- Signal heads and give-way signs, one per approach ---------------
    for (std::size_t index = 0; index < network.junctionCount(); ++index)
    {
        const Junction& junction = network.junctions()[index];
        const bool roundabout = junction.type == JunctionType::Roundabout;
        for (int arm = 0; arm < 4; ++arm)
        {
            if (!junction.hasArm[static_cast<std::size_t>(arm)])
                continue;
            const glm::vec2 along = armDirection(arm);
            // Right of the approaching car (heading -along).
            const glm::vec2 right {along.y, -along.x};
            const float yaw = glm::degrees(std::atan2(-along.x, -along.y));
            if (junction.isSignalised())
            {
                const glm::vec2 foot = junction.centre + along * (RoadNetwork::stopLine + 0.6f) +
                                       right * (RoadNetwork::halfWidth + 1.3f);
                world.signalHeads_.push_back({index, arm, foot, yaw});
            }
            else if (roundabout || (junction.type == JunctionType::GiveWayT &&
                                    !junction.majorArm[static_cast<std::size_t>(arm)]))
            {
                const float out = roundabout ? 22.0f : RoadNetwork::stopLine + 1.5f;
                world.giveWaySigns_.push_back({junction.centre + along * out + right * (RoadNetwork::halfWidth + 1.2f), yaw});
            }
        }
        if (roundabout)
        {
            // The island's kerb ring reaches 0.3 m beyond the grass.
            world.islands_.push_back({junction.centre, RoadNetwork::islandRadius + 0.3f});
            if (junction.fountain)
                world.fountains_.push_back({junction.centre, 2.2f});
        }
    }

    // ---- Bus shelters at the stops of the bus line ----------------------------
    for (std::size_t index = 0; index < busStops.size(); ++index)
        world.busShelters_.push_back({busStops[index].shelter, busStops[index].facingDegrees,
                                      static_cast<int>((index + 1) % billboardText().size())});

    // ---- Collision shapes -----------------------------------------------------
    for (const Building& building : world.buildings_)
    {
        // The dark plinth round the foot is 0.175 m wider on every side.
        world.solidBoxes_.push_back(makeOrientedBox({building.position.x, building.position.z}, 0.0f,
                                                    {0.5f * building.size.x + 0.18f, 0.5f * building.size.z + 0.18f}));
    }
    for (const Crate& crate : world.crates_)
        world.solidBoxes_.push_back(makeOrientedBox({crate.position.x, crate.position.z}, crate.yawDegrees, {0.53f, 0.53f}));
    for (const Tree& tree : world.trees_)
        world.solidPosts_.push_back({tree.position, 0.32f * tree.scale});
    for (const StreetLamp& lamp : network.streetLamps())
        world.solidPosts_.push_back({{lamp.position.x, lamp.position.z}, 0.2f});
    for (const SignalHead& head : world.signalHeads_)
        world.solidPosts_.push_back({head.foot, 0.16f});
    for (const GiveWaySign& sign : world.giveWaySigns_)
        world.solidPosts_.push_back({sign.foot, 0.12f});
    for (const RoadSign& sign : world.roadSigns_)
        world.solidPosts_.push_back({{sign.position.x, sign.position.z}, 0.12f});
    for (const Billboard& billboard : world.billboards_)
    {
        const glm::vec2 facing = facingVector(billboard.facingDegrees);
        const glm::vec2 across {facing.y, -facing.x};
        for (float side : {-1.0f, 1.0f})
            world.solidPosts_.push_back({billboard.centre + across * (side * (0.5f * billboardWidth - 0.4f)), 0.2f});
    }

    // A shelter's back wall and both end panels are solid; its open front is
    // not, so you can walk in and sit on the bench.
    for (const BusShelter& shelter : world.busShelters_)
    {
        const glm::vec2 facing = facingVector(shelter.facingDegrees);
        const glm::vec2 along {facing.y, -facing.x};
        const float half = 0.5f * shelterLength;
        const float depth = 0.5f * shelterDepth;
        world.solidBoxes_.push_back(makeOrientedBox(shelter.centre - facing * (depth - 0.05f), shelter.facingDegrees,
                                                    {half, 0.06f}));
        for (float side : {-1.0f, 1.0f})
            world.solidBoxes_.push_back(makeOrientedBox(shelter.centre + along * (side * (half - 0.05f)) - facing * (0.5f * depth),
                                                        shelter.facingDegrees, {0.06f, 0.5f * depth + 0.05f}));
        world.solidPosts_.push_back({shelterPole(shelter), 0.08f});
    }

    // ---- Ground height ------------------------------------------------------
    for (std::size_t block = 0; block < network.blockCount(); ++block)
        world.raised_.push_back(network.blockOutline(block, 0.0f));
    for (std::size_t index = 0; index < network.junctionCount(); ++index)
    {
        const Junction& junction = network.junctions()[index];
        if (junction.type != JunctionType::Roundabout)
            continue;
        for (int arm = 0; arm < 4; ++arm)
        {
            if (junction.hasArm[static_cast<std::size_t>(arm)])
                world.raised_.push_back(network.splitterIsland(index, arm));
        }
    }
    world.cityEdge_ = network.outsideOutline(0.0f);
    return world;
}

std::vector<PointLight> World::signLights() const
{
    std::vector<PointLight> lights;
    for (const NeonSign& sign : neonSigns_)
    {
        // The tubes' own colour spills onto the pavement below.
        const glm::vec2 facing = facingVector(sign.facingDegrees);
        PointLight light;
        light.position = sign.centre + glm::vec3(facing.x, 0.0f, facing.y) * 1.6f - glm::vec3(0.0f, 1.2f, 0.0f);
        light.color = sign.color * 1.6f;
        light.range = 11.0f;
        lights.push_back(light);
    }
    for (const Billboard& billboard : billboards_)
    {
        if (billboard.spotLit)
            continue;   // lit by the spot lamp instead
        const glm::vec2 facing = facingVector(billboard.facingDegrees);
        PointLight light;
        const glm::vec2 front = billboard.centre + facing * 2.5f;
        light.position = {front.x, billboardBottom + 0.3f, front.y};
        light.color = {1.25f, 1.15f, 0.95f};
        light.range = 13.0f;
        lights.push_back(light);
    }
    for (const BusShelter& shelter : busShelters_)
    {
        // The lamp under the roof, and the lit panel, light the waiting area.
        PointLight light;
        light.position = {shelter.centre.x, shelterHeight - 0.3f, shelter.centre.y};
        light.color = {1.05f, 1.00f, 0.90f};
        light.range = 8.0f;
        lights.push_back(light);
    }
    return lights;
}

float World::surfaceHeight(glm::vec2 point) const
{
    if (!pointInPolygon(point, cityEdge_))
        return RoadNetwork::kerbTopY;
    for (const std::vector<glm::vec2>& outline : raised_)
    {
        if (pointInPolygon(point, outline))
            return RoadNetwork::kerbTopY;
    }
    for (const Circle& island : islands_)
    {
        if (glm::length(point - island.centre) < island.radius)
            return RoadNetwork::roadY + 0.19f;   // the island's grass top
    }
    return RoadNetwork::roadY;
}
