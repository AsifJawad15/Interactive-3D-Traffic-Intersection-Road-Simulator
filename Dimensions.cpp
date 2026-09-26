#include "Dimensions.h"

#include "DayNight.h"
#include "RoadNetwork.h"
#include "Simulation.h"
#include "TreeGenerator.h"
#include "VehicleTypes.h"
#include "World.h"

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>

namespace
{
    struct Range
    {
        float low = std::numeric_limits<float>::max();
        float high = std::numeric_limits<float>::lowest();
        void add(float value)
        {
            low = std::min(low, value);
            high = std::max(high, value);
        }
    };

    const char* styleName(BuildingStyle style)
    {
        switch (style)
        {
        case BuildingStyle::ShopRow: return "shop row";
        case BuildingStyle::Apartment: return "flats";
        case BuildingStyle::Office: return "office tower";
        case BuildingStyle::Hotel: return "hotel";
        case BuildingStyle::House: return "house";
        case BuildingStyle::Warehouse: return "warehouse";
        default: return "skyline";
        }
    }

    const char* speciesName(TreeSpecies species)
    {
        switch (species)
        {
        case TreeSpecies::Broadleaf: return "broadleaf";
        case TreeSpecies::Conifer: return "conifer";
        default: return "palm";
        }
    }

    const char* propName(PropKind kind)
    {
        switch (kind)
        {
        case PropKind::Bench: return "bench";
        case PropKind::Bin: return "bin";
        case PropKind::Planter: return "planter";
        case PropKind::Bollard: return "bollard";
        case PropKind::SpeedSign: return "50 km/h sign";
        default: return "picnic table";
        }
    }

    const char* junctionName(JunctionType type)
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

    void printBuildings(const char* title, const std::vector<Building>& buildings)
    {
        struct Totals
        {
            int count = 0;
            Range width, depth, height;
            int fewestFloors = 1000, mostFloors = 0;
        };
        std::map<int, Totals> byStyle;
        for (const Building& building : buildings)
        {
            Totals& totals = byStyle[static_cast<int>(building.style)];
            ++totals.count;
            totals.width.add(building.frontWidth());
            totals.depth.add(building.depth());
            totals.height.add(building.size.y);
            const int storeys = building.floors + (building.groundFloor > 0.0f ? 1 : 0);
            totals.fewestFloors = std::min(totals.fewestFloors, storeys);
            totals.mostFloors = std::max(totals.mostFloors, storeys);
        }
        std::printf("\n%s (%zu)\n", title, buildings.size());
        std::printf("  %-13s %5s  %-13s %-13s %-13s %-7s %s\n", "style", "count", "front width", "depth", "height",
                    "storeys", "storey height");
        for (const auto& [style, totals] : byStyle)
        {
            std::printf("  %-13s %5d  %5.1f - %5.1f %5.1f - %5.1f %5.1f - %5.1f %2d - %-2d   %.1f m\n",
                        styleName(static_cast<BuildingStyle>(style)), totals.count, totals.width.low,
                        totals.width.high, totals.depth.low, totals.depth.high, totals.height.low, totals.height.high,
                        totals.fewestFloors, totals.mostFloors, World::storeyHeight(static_cast<BuildingStyle>(style)));
        }
    }
}

int printDimensions()
{
    const TrafficSystem traffic;
    const RoadNetwork& network = traffic.network();
    const World world = World::make(network, traffic.busStopSites());

    std::printf("Object dimensions (metres unless stated; x across, y up, z along)\n");

    // ---- Vehicles ----------------------------------------------------------
    // Wheel radii are written in the body builder (VehicleRenderer.cpp).
    const std::array<float, vehicleKindCount> wheelRadius = {0.33f, 0.32f, 0.38f, 0.33f, 0.33f, 0.36f,
                                                             0.38f, 0.37f, 0.48f, 0.50f, 0.31f};
    std::printf("\nVehicles (VehicleTypes.cpp; wheel radius from VehicleRenderer.cpp)\n");
    std::printf("  %-11s %-6s %6s %6s %6s %9s %7s %11s %7s %7s\n", "kind", "class", "length", "width", "height",
                "wheelbase", "wheel r", "cruise km/h", "accel", "brake");
    for (std::size_t index = 0; index < vehicleKindCount; ++index)
    {
        const VehicleSpec& spec = vehicleSpec(static_cast<VehicleKind>(index));
        const SizeClassSpec& size = sizeClassSpec(spec.sizeClass);
        std::printf("  %-11s %-6s %6.2f %6.2f %6.2f %9.2f %7.2f %11.0f %7.2f %7.2f\n", spec.name, size.name,
                    spec.length, spec.width, spec.height, index == static_cast<std::size_t>(VehicleKind::Motorbike) ? 1.40f : size.wheelBase,
                    wheelRadius[index], spec.cruiseSpeed * 3.6f, spec.acceleration, spec.braking);
    }
    std::printf("  size-class planning outlines (half length x half width): ");
    for (std::size_t index = 0; index < sizeClassCount; ++index)
    {
        const SizeClassSpec& size = sizeClassSpec(static_cast<SizeClass>(index));
        std::printf("%s %.2f x %.2f%s", size.name, size.halfLength, size.halfWidth, index + 1 < sizeClassCount ? ", " : "\n");
    }

    // ---- Roads -------------------------------------------------------------
    std::printf("\nRoads and junctions (RoadNetwork.h)\n");
    std::printf("  city grid spacing %.0f, city %.0f x %.0f, walkable square %.0f x %.0f\n", RoadNetwork::spacing,
                2.0f * RoadNetwork::gridHalf * RoadNetwork::spacing, 2.0f * RoadNetwork::gridHalf * RoadNetwork::spacing,
                2.0f * World::boundary, 2.0f * World::boundary);
    std::printf("  lane width %.2f, two lanes each way (centres %.2f and %.2f from the centreline), road %.1f kerb to kerb\n",
                RoadNetwork::laneWidth, RoadNetwork::laneOffsets[0], RoadNetwork::laneOffsets[1], 2.0f * RoadNetwork::halfWidth);
    std::printf("  sidewalk width %.1f, kerb height %.2f (road surface y %.2f, sidewalk y %.2f)\n", RoadNetwork::sidewalkWidth,
                RoadNetwork::kerbTopY - RoadNetwork::roadY, RoadNetwork::roadY, RoadNetwork::kerbTopY);
    std::printf("  kerb corner radius %.1f, zebra band %.1f - %.1f from the junction centre, stop line at %.1f\n",
                RoadNetwork::cornerRadius, RoadNetwork::crossingNear, RoadNetwork::crossingFar, RoadNetwork::stopLine);
    std::printf("  bend radius (centreline) %.1f\n", RoadNetwork::bendRadius);
    std::printf("  roundabout: island radius %.1f, circulating lane radius %.1f, outer kerb radius %.1f, entry/exit arcs %.1f,"
                " zebra %.1f - %.1f\n", RoadNetwork::islandRadius, RoadNetwork::ringRadius, RoadNetwork::roundaboutRadius,
                RoadNetwork::entryRadius, RoadNetwork::roundaboutCrossingNear, RoadNetwork::roundaboutCrossingFar);
    std::map<int, int> junctionCount;
    for (const Junction& junction : network.junctions())
        ++junctionCount[static_cast<int>(junction.type)];
    std::printf("  junctions: %zu (", network.junctions().size());
    for (const auto& [type, count] : junctionCount)
        std::printf("%d %s%s", count, junctionName(static_cast<JunctionType>(type)), type == junctionCount.rbegin()->first ? ")\n" : ", ");
    std::printf("  roads: %zu, zebra crossings: %zu\n", network.roads().size(), network.crossings().size());

    // ---- Buildings ---------------------------------------------------------
    printBuildings("Buildings (WorldCity.cpp)", world.buildings());
    printBuildings("Skyline towers beyond the city", world.skyline());
    std::size_t shops = 0, neonShops = 0;
    Range shopWidth;
    for (const Shop& shop : world.shops())
    {
        ++shops;
        neonShops += shop.neon ? 1 : 0;
        shopWidth.add(shop.width);
    }
    std::printf("  shop fronts: %zu (%zu with neon signs), %.1f - %.1f wide\n", shops, neonShops, shopWidth.low, shopWidth.high);
    Range letters;
    for (const NeonSign& sign : world.neonSigns())
        letters.add(sign.letterHeight);
    std::printf("  neon signs: %zu, letters %.2f - %.2f high\n", world.neonSigns().size(), letters.low, letters.high);

    // ---- Trees -------------------------------------------------------------
    std::printf("\nTrees (TreeGenerator.cpp; natural size x the placed scale)\n");
    std::printf("  %-10s %5s  %-15s %-15s\n", "species", "count", "height", "crown radius");
    for (const TreeSpecies species : {TreeSpecies::Broadleaf, TreeSpecies::Conifer, TreeSpecies::Palm})
    {
        std::array<TreeModel, TreeGenerator::variantsPerSpecies> models;
        for (int variant = 0; variant < TreeGenerator::variantsPerSpecies; ++variant)
            models[static_cast<std::size_t>(variant)] = TreeGenerator::make(species, variant);
        int count = 0;
        Range height, crown;
        for (const Tree& tree : world.trees())
        {
            if (tree.species != species)
                continue;
            ++count;
            const TreeModel& model = models[static_cast<std::size_t>(tree.variant % TreeGenerator::variantsPerSpecies)];
            height.add(model.height * tree.scale);
            crown.add(model.crownRadius * tree.scale);
        }
        std::printf("  %-10s %5d  %5.1f - %5.1f   %5.1f - %5.1f\n", speciesName(species), count, height.low, height.high,
                    crown.low, crown.high);
    }

    // ---- Street furniture --------------------------------------------------
    std::printf("\nStreet furniture and signs\n");
    std::map<int, int> propCount;
    for (const Prop& prop : world.props())
        ++propCount[static_cast<int>(prop.kind)];
    for (const auto& [kind, count] : propCount)
        std::printf("  %-13s %4d\n", propName(static_cast<PropKind>(kind)), count);
    std::printf("  street lamps %zu + park and car-park lamps %zu: Bezier post 5.60 high (radius 0.17 at the foot),"
                " head 0.75 x 0.28 x 0.75 at 5.75, light at 5.58\n", network.streetLamps().size(), world.extraLamps().size());
    std::printf("  signal heads %zu: pole 0.13 x 4.50 x 0.13, housing 0.72 x 1.85 x 0.55 centred at 4.45,"
                " three lenses 0.38 across, arrow box 0.46 x 0.46 x 0.50\n", world.signalHeads().size());
    std::printf("  walk signals %zu, give-way signs %zu, road signs %zu (plate 0.90 x 0.90 at 2.15)\n",
                world.walkSignals().size(), world.giveWaySigns().size(), world.roadSigns().size());
    std::printf("  billboards %zu: panel %.1f x %.1f, underside %.1f above the ground; spot lamp cone %.0f / %.0f degrees\n",
                world.billboards().size(), World::billboardWidth, World::billboardHeight, World::billboardBottom,
                world.spotLamp().innerDegrees, world.spotLamp().outerDegrees);
    std::printf("  bus shelters %zu: %.1f long x %.1f deep x %.1f high\n", world.busShelters().size(), World::shelterLength,
                World::shelterDepth, World::shelterHeight);
    std::printf("  crates %zu: 1.05 cube; parked cars %zu\n", world.crates().size(), world.parkedCars().size());
    for (const Pond& pond : world.ponds())
        std::printf("  pond: %.1f x %.1f (radii %.1f, %.1f)\n", 2.0f * pond.radii.x, 2.0f * pond.radii.y, pond.radii.x, pond.radii.y);
    std::printf("  petrol station canopy: %.1f x %.1f, %.1f high\n", GasStation::canopyWidth, GasStation::canopyDepth,
                GasStation::canopyHeight);

    // ---- People ------------------------------------------------------------
    std::printf("\nPeople (Mannequin.h, Pedestrians.cpp)\n");
    std::printf("  height 1.56 - 1.90 (proportions of a 1.75 figure, scaled); thigh 0.455, shin 0.445, torso 0.44,"
                " upper arm 0.29, forearm 0.26; you on foot 1.78, eyes at 1.65\n");

    // ---- Camera and lights -------------------------------------------------
    std::printf("\nCamera (Camera.cpp)\n");
    std::printf("  field of view 55 deg (free, top, chase), 68 in a car, 62 on foot; near plane 0.1 - 6 (rises with height),"
                " far plane 1200\n");
    std::printf("\nLights\n");
    std::printf("  point lights (lamps): colour (1.65, 0.92, 0.36), attenuation 1 / (1 + 0.09 d + 0.032 d^2), reach 22\n");
    std::printf("  spot lights: billboard lamp %.0f / %.0f deg; headlights 10 / 26 deg, reach 42\n",
                world.spotLamp().innerDegrees, world.spotLamp().outerDegrees);
    std::printf("  emissive strength 3.0; at most 32 point lights and 8 spot lights per frame\n");
    std::printf("  directional light at each preset (clear sky):\n");
    std::printf("  %-10s %6s %-12s %-24s %s\n", "preset", "time", "light", "colour", "elevation");
    const std::array<std::pair<const char*, float>, 5> presets = {
        {{"Morning", 7.0f}, {"Noon", 12.0f}, {"Afternoon", 15.5f}, {"Evening", 18.5f}, {"Night", 22.0f}}};
    for (const auto& [name, hours] : presets)
    {
        DayNight sky;
        sky.setTime(hours);
        const glm::vec3 color = sky.lightColor();
        const glm::vec3 towards = -glm::normalize(sky.lightDirection());
        std::printf("  %-10s %5.1fh %-12s (%.2f, %.2f, %.2f)       %4.0f deg\n", name, hours, sky.moonlit() ? "moon" : "sun",
                    color.r, color.g, color.b, glm::degrees(std::asin(glm::clamp(towards.y, -1.0f, 1.0f))));
    }
    return 0;
}
