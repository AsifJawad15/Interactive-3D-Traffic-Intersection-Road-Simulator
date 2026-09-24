#include "World.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <random>
#include <string>
#include <vector>

// The city generator: everything that makes the blocks look lived in. It is
// seeded, so the same city comes out every run, and every piece it places
// is checked against the lawn it must stand on and against everything placed
// before it, so nothing ends up on a sidewalk or inside something else.

namespace
{
    constexpr float pi = 3.14159265358979f;

    glm::vec2 facingVector(float degrees)
    {
        const float a = glm::radians(degrees);
        return {std::sin(a), std::cos(a)};
    }

    float facingOf(glm::vec2 direction)
    {
        return glm::degrees(std::atan2(direction.x, direction.y));
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

    // An axis-aligned rectangle on the ground (x, z).
    struct Rect
    {
        glm::vec2 min {0.0f};
        glm::vec2 max {0.0f};

        bool overlaps(const Rect& other, float margin = 0.0f) const
        {
            return min.x < other.max.x + margin && other.min.x < max.x + margin &&
                   min.y < other.max.y + margin && other.min.y < max.y + margin;
        }
        bool contains(glm::vec2 point) const
        {
            return point.x > min.x && point.x < max.x && point.y > min.y && point.y < max.y;
        }
        Rect grown(float distance) const { return {min - distance, max + distance}; }
        glm::vec2 centre() const { return 0.5f * (min + max); }
        glm::vec2 size() const { return max - min; }
        std::array<glm::vec2, 4> corners() const
        {
            return {min, glm::vec2{max.x, min.y}, max, glm::vec2{min.x, max.y}};
        }
    };

    Rect rectBetween(glm::vec2 a, glm::vec2 b)
    {
        return {glm::min(a, b), glm::max(a, b)};
    }

    Rect rectAround(glm::vec2 centre, float radius)
    {
        return {centre - radius, centre + radius};
    }

    // A rectangle `halfAcross` wide either side of `centre` and `halfAlong`
    // deep along `facing` (a multiple of 90 degrees, or near enough).
    Rect rectFacing(glm::vec2 centre, float facingDegrees, float halfAcross, float halfAlong)
    {
        const glm::vec2 forward = facingVector(facingDegrees);
        const glm::vec2 across {forward.y, -forward.x};
        const glm::vec2 extent = glm::abs(forward) * halfAlong + glm::abs(across) * halfAcross;
        return {centre - extent, centre + extent};
    }

    std::vector<glm::vec2> rectOutline(const Rect& rect)
    {
        const auto corners = rect.corners();
        return {corners.begin(), corners.end()};
    }

    class Random
    {
    public:
        explicit Random(unsigned seed) : engine_(seed) {}
        float uniform(float low, float high) { return std::uniform_real_distribution<float>(low, high)(engine_); }
        int range(int low, int high) { return std::uniform_int_distribution<int>(low, high)(engine_); }
        bool chance(float probability) { return uniform(0.0f, 1.0f) < probability; }
        template <typename T>
        const T& pick(const std::vector<T>& items) { return items[static_cast<std::size_t>(range(0, static_cast<int>(items.size()) - 1))]; }

    private:
        std::mt19937 engine_;
    };

    // What a stretch of street is lined with.
    enum class Zone
    {
        Downtown,      // shops with flats above, the odd hotel
        Offices,       // towers, hotels, shops
        Residential,   // blocks of flats and a few shops
        Houses,        // houses in gardens
        Mixed,         // shops, flats and a warehouse
        Park,
        Industrial     // warehouses and offices
    };

    // Plot sizes by style: frontage width, depth, how far the front stands
    // back from the road's centre line, the gap to the next plot, storeys.
    struct StyleSpec
    {
        float minWidth, maxWidth;
        float minDepth, maxDepth;
        float setback;
        float minGap, maxGap;
        int minFloors, maxFloors;
        float groundFloor;
    };

    const StyleSpec& styleSpec(BuildingStyle style)
    {
        static const std::array<StyleSpec, buildingStyleCount> specs = {{
            {14.0f, 22.0f, 10.0f, 12.0f, 14.0f, 3.0f, 8.0f, 1, 2, 4.2f},    // shop row: 4.2 m shop floor
            {14.0f, 20.0f, 11.0f, 13.0f, 14.5f, 4.0f, 9.0f, 3, 6, 0.0f},    // apartment
            {16.0f, 22.0f, 15.0f, 19.0f, 15.0f, 5.0f, 10.0f, 6, 12, 4.8f},  // office: tall lobby
            {22.0f, 28.0f, 12.0f, 14.0f, 15.0f, 5.0f, 9.0f, 5, 8, 4.5f},    // hotel
            {13.0f, 14.5f, 8.0f, 9.5f, 18.0f, 4.0f, 8.0f, 1, 2, 0.0f},      // house (with its drive)
            {24.0f, 34.0f, 16.0f, 22.0f, 16.5f, 6.0f, 10.0f, 1, 1, 0.0f},   // warehouse
            {18.0f, 40.0f, 18.0f, 40.0f, 0.0f, 0.0f, 0.0f, 5, 22, 0.0f}     // skyline
        }};
        return specs[static_cast<std::size_t>(style)];
    }

    BuildingStyle pickStyle(Zone zone, Random& random)
    {
        const float roll = random.uniform(0.0f, 1.0f);
        switch (zone)
        {
        case Zone::Downtown:
            return roll < 0.58f ? BuildingStyle::ShopRow : roll < 0.9f ? BuildingStyle::Apartment : BuildingStyle::Hotel;
        case Zone::Offices:
            return roll < 0.5f ? BuildingStyle::Office : roll < 0.68f ? BuildingStyle::Hotel : BuildingStyle::ShopRow;
        case Zone::Residential:
            return roll < 0.7f ? BuildingStyle::Apartment : BuildingStyle::ShopRow;
        case Zone::Houses:
            return roll < 0.86f ? BuildingStyle::House : BuildingStyle::Apartment;
        case Zone::Mixed:
            return roll < 0.4f ? BuildingStyle::ShopRow : roll < 0.8f ? BuildingStyle::Apartment : BuildingStyle::Warehouse;
        case Zone::Industrial:
            return roll < 0.7f ? BuildingStyle::Warehouse : BuildingStyle::Office;
        default:
            return BuildingStyle::Apartment;
        }
    }

    const std::vector<glm::vec3>& stylePalette(BuildingStyle style)
    {
        static const std::array<std::vector<glm::vec3>, buildingStyleCount> palettes = {{
            {{0.90f, 0.80f, 0.66f}, {0.86f, 0.70f, 0.58f}, {0.80f, 0.78f, 0.72f}, {0.72f, 0.52f, 0.44f}, {0.92f, 0.88f, 0.80f}, {0.78f, 0.84f, 0.80f}},
            {{0.88f, 0.84f, 0.76f}, {0.78f, 0.74f, 0.70f}, {0.74f, 0.62f, 0.52f}, {0.84f, 0.80f, 0.86f}, {0.76f, 0.82f, 0.80f}, {0.92f, 0.86f, 0.70f}},
            {{0.70f, 0.74f, 0.80f}, {0.60f, 0.64f, 0.70f}, {0.82f, 0.84f, 0.86f}, {0.52f, 0.56f, 0.62f}},
            {{0.92f, 0.90f, 0.84f}, {0.80f, 0.72f, 0.62f}, {0.86f, 0.82f, 0.90f}},
            {{0.95f, 0.92f, 0.84f}, {0.90f, 0.80f, 0.66f}, {0.82f, 0.86f, 0.90f}, {0.88f, 0.76f, 0.74f}, {0.80f, 0.84f, 0.72f}},
            {{0.66f, 0.68f, 0.70f}, {0.72f, 0.66f, 0.56f}, {0.56f, 0.62f, 0.66f}, {0.70f, 0.60f, 0.52f}},
            {{0.62f, 0.66f, 0.72f}, {0.56f, 0.58f, 0.64f}, {0.70f, 0.70f, 0.72f}, {0.66f, 0.62f, 0.60f}}
        }};
        return palettes[static_cast<std::size_t>(style)];
    }

    // A stretch of kerb along one side of a road: the lawn behind it is
    // where that side's buildings go.
    struct Frontage
    {
        glm::vec2 start {0.0f};       // on the road's centre line
        glm::vec2 direction {1.0f, 0.0f};
        glm::vec2 normal {0.0f, 1.0f};   // from the road into the land
        float firstFree = 0.0f;       // plots may use this range along the road
        float lastFree = 0.0f;
        int block = -1;               // -1: outside the ring road
        Zone zone = Zone::Downtown;

        glm::vec2 at(float along, float out) const { return start + direction * along + normal * out; }
    };

    int armTowards(glm::vec2 direction)
    {
        if (direction.y > 0.5f) return ArmNorth;
        if (direction.x > 0.5f) return ArmEast;
        if (direction.y < -0.5f) return ArmSouth;
        return ArmWest;
    }

    // How far along the road from junction `junction` the first plot may
    // start on the side facing `normal`.
    float cornerReach(const Junction& junction, glm::vec2 normal)
    {
        const bool corner = junction.hasArm[static_cast<std::size_t>(armTowards(normal))];
        if (junction.type == JunctionType::Roundabout)
            return 28.0f;
        if (junction.type == JunctionType::Bend)
            return corner ? 24.0f : 36.0f;   // inside or outside of the bend
        return corner ? 18.0f : 4.0f;        // a corner, or the straight side of a T
    }

    // Land use of the ten blocks, in the order RoadNetwork::makeCity lists
    // them, and of the row outside the ring road by side.
    Zone blockZone(int block, glm::vec2 normal)
    {
        static const std::array<Zone, 10> zones = {
            Zone::Downtown,      // core north-west, the bus loop
            Zone::Offices,       // core north-east
            Zone::Downtown,      // core south-west
            Zone::Residential,   // core south-east (and the car park)
            Zone::Houses,        // west, tall
            Zone::Park,          // north-west: the park
            Zone::Offices,       // north-east, L-shaped
            Zone::Mixed,         // east (and the petrol station)
            Zone::Residential,   // south-east, wide
            Zone::Houses         // south-west, wide
        };
        if (block >= 0 && block < static_cast<int>(zones.size()))
            return zones[static_cast<std::size_t>(block)];
        // Outside the ring: flats to the north, towers to the east,
        // warehouses to the south and houses to the west.
        if (normal.y > 0.5f) return Zone::Residential;
        if (normal.x > 0.5f) return Zone::Offices;
        if (normal.y < -0.5f) return Zone::Industrial;
        return Zone::Houses;
    }

    struct ShopName
    {
        const char* name;
        glm::vec3 color;
    };

    const std::vector<ShopName>& neonShops()
    {
        static const std::vector<ShopName> names = {
            {"SUSHI", {1.00f, 0.20f, 0.30f}}, {"BURGER", {1.00f, 0.62f, 0.10f}}, {"NOODLES", {0.20f, 1.00f, 0.55f}},
            {"24/7", {0.30f, 0.80f, 1.00f}}, {"DINER", {1.00f, 0.25f, 0.70f}}, {"JAZZ", {0.65f, 0.35f, 1.00f}},
            {"TACOS", {1.00f, 0.85f, 0.15f}}, {"CLUB", {0.25f, 0.45f, 1.00f}}, {"KEBAB", {1.00f, 0.35f, 0.12f}},
            {"ARCADE", {0.20f, 1.00f, 0.95f}}, {"MOTEL", {1.00f, 0.18f, 0.22f}}, {"KARAOKE", {1.00f, 0.30f, 0.85f}}
        };
        return names;
    }

    const std::vector<ShopName>& paintedShops()
    {
        static const std::vector<ShopName> names = {
            {"PHARMACY", {0.10f, 0.62f, 0.30f}}, {"BANK", {0.10f, 0.26f, 0.62f}}, {"BAKERY", {0.78f, 0.42f, 0.16f}},
            {"BOOKS", {0.46f, 0.16f, 0.20f}}, {"FLOWERS", {0.86f, 0.36f, 0.56f}}, {"GROCERY", {0.26f, 0.56f, 0.16f}},
            {"BARBER", {0.80f, 0.12f, 0.12f}}, {"OPTICS", {0.16f, 0.46f, 0.66f}}, {"TOYS", {0.92f, 0.62f, 0.08f}},
            {"MUSIC", {0.36f, 0.20f, 0.56f}}, {"SHOES", {0.60f, 0.36f, 0.22f}}, {"HARDWARE", {0.86f, 0.46f, 0.08f}},
            {"CAFE", {0.40f, 0.26f, 0.16f}}, {"POST", {0.90f, 0.72f, 0.08f}}
        };
        return names;
    }

    const std::vector<const char*>& companyNames()
    {
        static const std::vector<const char*> names = {"CITY BANK", "TECH HUB", "INSURANCE", "LAW OFFICE", "MEDIA", "CLINIC"};
        return names;
    }
}

// ---------------------------------------------------------------------------

glm::vec2 Building::front() const
{
    return facingVector(facingDegrees);
}

float Building::frontWidth() const
{
    return std::abs(front().x) > 0.5f ? size.z : size.x;
}

float Building::depth() const
{
    return std::abs(front().x) > 0.5f ? size.x : size.z;
}

float World::storeyHeight(BuildingStyle style)
{
    switch (style)
    {
    case BuildingStyle::ShopRow: return 3.3f;
    case BuildingStyle::Apartment: return 3.1f;
    case BuildingStyle::Office: return 3.6f;
    case BuildingStyle::Hotel: return 3.2f;
    case BuildingStyle::House: return 2.9f;
    case BuildingStyle::Warehouse: return 7.0f;
    default: return 3.4f;
    }
}

bool World::onLawn(glm::vec2 point) const
{
    for (const std::vector<glm::vec2>& lawn : lawns_)
    {
        if (pointInPolygon(point, lawn))
            return true;
    }
    return !outerLawnEdge_.empty() && !pointInPolygon(point, outerLawnEdge_) &&
           std::abs(point.x) < boundary - 8.0f && std::abs(point.y) < boundary - 8.0f;
}

void World::buildCity(const RoadNetwork& network)
{
    Random random(20260924u);
    const std::size_t existingShops = shops_.size();   // the X0 shops already have their neon

    for (std::size_t block = 0; block < network.blockCount(); ++block)
        lawns_.push_back(network.blockOutline(block, RoadNetwork::sidewalkWidth));
    outerLawnEdge_ = network.outsideOutline(RoadNetwork::sidewalkWidth);

    // ---- What is already standing, and must be kept clear -----------------
    std::vector<Rect> keepOut;
    for (const Billboard& billboard : billboards_)
        keepOut.push_back(rectFacing(billboard.centre, billboard.facingDegrees, 0.5f * billboardWidth + 1.5f, 2.5f));
    for (const BusShelter& shelter : busShelters_)
    {
        keepOut.push_back(rectFacing(shelter.centre, shelter.facingDegrees, 0.5f * shelterLength + 1.0f, 0.5f * shelterDepth + 1.0f));
        keepOut.push_back(rectAround(shelterPole(shelter), 0.8f));
    }
    for (const SignalHead& head : signalHeads_)
        keepOut.push_back(rectAround(head.foot, 1.0f));
    for (const GiveWaySign& sign : giveWaySigns_)
        keepOut.push_back(rectAround(sign.foot, 0.8f));
    for (const RoadSign& sign : roadSigns_)
        keepOut.push_back(rectAround({sign.position.x, sign.position.z}, 0.8f));
    for (const Crate& crate : crates_)
        keepOut.push_back(rectAround({crate.position.x, crate.position.z}, 1.4f));
    for (const Tree& tree : trees_)
        keepOut.push_back(rectAround(tree.position, 3.0f));
    for (const StreetLamp& lamp : network.streetLamps())
        keepOut.push_back(rectAround({lamp.position.x, lamp.position.z}, 0.6f));
    // The corner of the lawn the player's crash test drives and walks into
    // the hotel across (main.cpp, --player-test).
    keepOut.push_back({{-31.0f, -19.0f}, {-5.0f, 6.0f}});

    std::vector<Rect> buildingRects;   // footprints of every building, for spacing
    for (const Building& building : buildings_)
    {
        buildingRects.push_back(rectBetween({building.position.x - 0.5f * building.size.x, building.position.z - 0.5f * building.size.z},
                                            {building.position.x + 0.5f * building.size.x, building.position.z + 0.5f * building.size.z}));
    }
    std::vector<Rect> reserved;        // the park, the petrol station, the car park, drives, aprons
    std::vector<Rect> awnings;         // the strip in front of every shop row
    std::vector<Rect> propRects;       // benches, bins and the like placed so far

    const auto onLand = [this](const Rect& rect)
    {
        for (const glm::vec2& corner : rect.corners())
        {
            if (!onLawn(corner))
                return false;
        }
        // No bend of a lawn's edge may poke into the rectangle either.
        for (const std::vector<glm::vec2>& lawn : lawns_)
        {
            for (const glm::vec2& point : lawn)
            {
                if (rect.contains(point))
                    return false;
            }
        }
        for (const glm::vec2& point : outerLawnEdge_)
        {
            if (rect.contains(point))
                return false;
        }
        return true;
    };
    const auto clearOf = [](const Rect& rect, const std::vector<Rect>& others, float margin)
    {
        for (const Rect& other : others)
        {
            if (rect.overlaps(other, margin))
                return false;
        }
        return true;
    };
    const auto insidePaved = [this](glm::vec2 point)
    {
        for (const PavedArea& area : pavedAreas_)
        {
            if (pointInPolygon(point, area.outline))
                return true;
        }
        return false;
    };

    // ---- Frontages: both sides of every road ------------------------------
    std::vector<Frontage> frontages;
    for (const RoadNetwork::Road& road : network.roads())
    {
        const glm::vec2 direction = glm::normalize(road.end - road.start);
        const float length = glm::length(road.end - road.start);
        for (float side : {1.0f, -1.0f})
        {
            Frontage frontage;
            frontage.start = road.start;
            frontage.direction = direction;
            frontage.normal = side * glm::vec2{direction.y, -direction.x};
            const glm::vec2 probe = frontage.at(0.5f * length, 30.0f);
            for (std::size_t block = 0; block < network.blockCount(); ++block)
            {
                if (pointInPolygon(probe, network.blockOutline(block, 0.0f)))
                    frontage.block = static_cast<int>(block);
            }
            if (frontage.block < 0 && pointInPolygon(probe, cityEdge_))
                continue;   // not land (cannot happen with this layout)
            frontage.zone = blockZone(frontage.block, frontage.normal);
            frontage.firstFree = cornerReach(network.junctions()[road.from], frontage.normal);
            frontage.lastFree = length - cornerReach(network.junctions()[road.to], frontage.normal);
            frontages.push_back(frontage);
        }
    }
    const auto frontageFacing = [&frontages](int block, glm::vec2 normal) -> const Frontage*
    {
        for (const Frontage& frontage : frontages)
        {
            if (frontage.block == block && glm::dot(frontage.normal, normal) > 0.9f)
                return &frontage;
        }
        return nullptr;
    };

    // ---- Adding things --------------------------------------------------------
    const auto addPaved = [this](const Rect& rect, bool asphalt)
    {
        pavedAreas_.push_back({rectOutline(rect), asphalt});
    };
    const auto addStrip = [this](glm::vec2 a, glm::vec2 b, float width)
    {
        // A straight path from a to b, as a paved quad.
        const glm::vec2 along = glm::normalize(b - a);
        const glm::vec2 side = glm::vec2{-along.y, along.x} * (0.5f * width);
        pavedAreas_.push_back({{a - side, b - side, b + side, a + side}, false});
    };
    const auto addProp = [this, &propRects](PropKind kind, glm::vec2 position, float facing)
    {
        props_.push_back({kind, position, facing});
        propRects.push_back(rectAround(position, 1.1f));
    };
    const auto addTree = [this](glm::vec2 position, TreeSpecies species, Random& rng)
    {
        Tree tree;
        tree.position = position;
        tree.species = species;
        tree.scale = species == TreeSpecies::Palm ? rng.uniform(0.9f, 1.1f) : rng.uniform(0.82f, 1.12f);
        tree.twistDegrees = rng.uniform(0.0f, 360.0f);
        tree.variant = rng.range(0, 2);
        trees_.push_back(tree);
    };
    const auto parkCar = [this](glm::vec2 position, float yaw, Random& rng)
    {
        static const std::vector<VehicleKind> kinds = {VehicleKind::Sedan, VehicleKind::Hatchback, VehicleKind::Suv,
                                                       VehicleKind::Sedan, VehicleKind::Pickup, VehicleKind::Hatchback};
        ParkedCar car;
        car.kind = rng.pick(kinds);
        car.position = position;
        car.yawDegrees = yaw;
        const std::vector<glm::vec3>& palette = vehicleSpec(car.kind).palette;
        car.color = rng.pick(palette);
        parkedCars_.push_back(car);
    };
    const auto lampAt = [this](glm::vec2 position)
    {
        extraLamps_.push_back({{position.x, RoadNetwork::kerbTopY, position.y}, false});
    };

    // ---- The park, with its pond and plaza --------------------------------
    // The whole north-west block. Its layout is set out as fractions of the
    // lawn so it follows the block if the road layout changes.
    {
        const int parkBlock = 5;
        Rect lawn {glm::vec2{1.0e9f}, glm::vec2{-1.0e9f}};
        for (const glm::vec2& point : lawns_[static_cast<std::size_t>(parkBlock)])
        {
            lawn.min = glm::min(lawn.min, point);
            lawn.max = glm::max(lawn.max, point);
        }
        reserved.push_back(lawn);
        const glm::vec2 size = lawn.size();
        const auto at = [&lawn, &size](float u, float v) { return lawn.min + glm::vec2{u, v} * size; };

        Pond pond;
        pond.centre = at(0.36f, 0.64f);
        pond.radii = {12.5f, 8.0f};
        constexpr int pondPoints = 40;
        for (int index = 0; index < pondPoints; ++index)
        {
            // An ellipse with a gentle wobble, so it reads as dug, not drawn.
            const float angle = 2.0f * pi * static_cast<float>(index) / pondPoints;
            const float wobble = 1.0f + 0.07f * std::sin(3.0f * angle + 0.6f) + 0.04f * std::sin(5.0f * angle);
            pond.outline.push_back(pond.centre + glm::vec2{std::cos(angle) * pond.radii.x, std::sin(angle) * pond.radii.y} * wobble);
        }
        ponds_.push_back(pond);

        plazaCentre_ = at(0.72f, 0.30f);
        hasPlaza_ = true;
        constexpr float plazaHalf = 11.0f;
        addPaved(rectAround(plazaCentre_, plazaHalf), false);

        // Paths: from the plaza out to two sidewalks and over to the pond, a
        // loop round the pond, and from there out to the other two sides.
        const glm::vec2 pondEast = pond.centre + glm::vec2{pond.radii.x + 4.0f, 0.0f};
        addStrip({plazaCentre_.x, plazaCentre_.y - plazaHalf}, {plazaCentre_.x, lawn.min.y - 0.4f}, 3.0f);
        addStrip({plazaCentre_.x + plazaHalf, plazaCentre_.y}, {lawn.max.x + 0.4f, plazaCentre_.y}, 3.0f);
        addStrip({plazaCentre_.x - plazaHalf, plazaCentre_.y + plazaHalf}, pondEast + glm::vec2{-1.0f, -1.0f}, 3.0f);
        constexpr int loopPieces = 28;
        for (int index = 0; index < loopPieces; ++index)
        {
            const float a0 = 2.0f * pi * static_cast<float>(index) / loopPieces;
            const float a1 = 2.0f * pi * static_cast<float>(index + 1) / loopPieces;
            const glm::vec2 inner = pond.radii + 3.0f;
            const glm::vec2 outer = pond.radii + 5.8f;
            const auto on = [&pond](glm::vec2 radii, float a) { return pond.centre + glm::vec2{std::cos(a) * radii.x, std::sin(a) * radii.y}; };
            pavedAreas_.push_back({{on(inner, a0), on(outer, a0), on(outer, a1), on(inner, a1)}, false});
        }
        addStrip(pond.centre + glm::vec2{0.0f, pond.radii.y + 5.0f}, {pond.centre.x, lawn.max.y + 0.4f}, 3.0f);
        addStrip(pond.centre - glm::vec2{pond.radii.x + 5.0f, 0.0f}, {lawn.min.x - 0.4f, pond.centre.y}, 3.0f);

        // The pond and the monument are solid.
        for (float offset : {-0.55f, 0.0f, 0.55f})
        {
            const float half = std::sqrt(1.0f - offset * offset) * pond.radii.y * 1.08f + 0.4f;
            fountains_.push_back({pond.centre + glm::vec2{offset * pond.radii.x, 0.0f}, half});
        }
        fountains_.push_back({plazaCentre_, 1.7f});

        // Palms at the plaza's corners, benches round it facing the monument,
        // and bollards where the paths come in.
        for (float sx : {-1.0f, 1.0f})
        {
            for (float sz : {-1.0f, 1.0f})
                addTree(plazaCentre_ + glm::vec2{sx, sz} * 8.3f, TreeSpecies::Palm, random);
        }
        for (int side = 0; side < 4; ++side)
        {
            const glm::vec2 out = facingVector(90.0f * static_cast<float>(side));
            const glm::vec2 across {out.y, -out.x};
            for (float offset : {-4.6f, 4.6f})
                addProp(PropKind::Bench, plazaCentre_ + out * 9.4f + across * offset, facingOf(-out));
            addProp(PropKind::Bin, plazaCentre_ + out * 9.6f + across * 7.0f, 0.0f);
        }
        for (float offset : {-1.1f, 1.1f})
        {
            addProp(PropKind::Bollard, {plazaCentre_.x + offset, plazaCentre_.y - plazaHalf - 0.5f}, 0.0f);
            addProp(PropKind::Bollard, {plazaCentre_.x + plazaHalf + 0.5f, plazaCentre_.y + offset}, 0.0f);
        }

        // Benches on the pond loop looking at the water, picnic tables on
        // the grass north of it.
        for (float angle : {0.35f * pi, 0.8f * pi, 1.3f * pi, 1.75f * pi})
        {
            const glm::vec2 radial {std::cos(angle), std::sin(angle)};
            const glm::vec2 spot = pond.centre + radial * (pond.radii + 7.0f);
            addProp(PropKind::Bench, spot, facingOf(-radial));
        }
        for (float u : {0.14f, 0.26f, 0.5f})
            addProp(PropKind::PicnicTable, at(u, 0.91f), 90.0f);

        // Lamps along the paths and at the plaza.
        for (float sx : {-1.0f, 1.0f})
        {
            for (float sz : {-1.0f, 1.0f})
                lampAt(plazaCentre_ + glm::vec2{sx, sz} * (plazaHalf - 0.6f));
        }
        for (float angle : {0.1f * pi, 0.6f * pi, 1.1f * pi, 1.6f * pi})
            lampAt(pond.centre + glm::vec2{std::cos(angle), std::sin(angle)} * (pond.radii + 6.7f));
        lampAt({plazaCentre_.x + 2.2f, lawn.min.y + 5.0f});
        lampAt({lawn.max.x - 5.0f, plazaCentre_.y + 2.2f});

        // Trees scattered over the grass, clear of the paths, the water and
        // everything placed above. Broadleaf and conifer.
        int planted = 0;
        for (int attempt = 0; attempt < 600 && planted < 34; ++attempt)
        {
            const glm::vec2 spot = lawn.min + glm::vec2{random.uniform(3.0f, size.x - 3.0f), random.uniform(3.0f, size.y - 3.0f)};
            if (!onLawn(spot) || insidePaved(spot))
                continue;
            bool clear = true;
            for (const PavedArea& area : pavedAreas_)
            {
                for (const glm::vec2& point : area.outline)
                    clear = clear && glm::length(point - spot) > 2.6f;
            }
            const glm::vec2 fromPond = (spot - pond.centre) / (pond.radii + 6.5f);
            clear = clear && glm::length(fromPond) > 1.0f && glm::length(spot - plazaCentre_) > plazaHalf + 3.0f;
            for (const Tree& tree : trees_)
                clear = clear && glm::length(tree.position - spot) > 6.5f;
            for (const StreetLamp& lamp : extraLamps_)
                clear = clear && glm::length(glm::vec2{lamp.position.x, lamp.position.z} - spot) > 3.0f;
            clear = clear && clearOf(rectAround(spot, 0.5f), propRects, 1.0f);
            if (!clear)
                continue;
            addTree(spot, random.chance(0.4f) ? TreeSpecies::Conifer : TreeSpecies::Broadleaf, random);
            ++planted;
        }
    }

    // ---- The petrol station, on the road from R1 to G5 ----------------------
    if (const Frontage* road = frontageFacing(7, {0.0f, -1.0f}))
    {
        // Canopy centred 50 m along from R1, 26 m back from the road.
        GasStation station;
        station.centre = road->at(50.0f, 26.0f);
        station.facingDegrees = facingOf(-road->normal);
        station.priceSign = road->at(24.0f, 14.2f);
        gasStations_.push_back(station);

        const Rect forecourt = rectBetween(road->at(34.0f, RoadNetwork::halfWidth + RoadNetwork::sidewalkWidth + 0.05f),
                                           road->at(66.0f, 40.9f));
        addPaved(forecourt, true);
        reserved.push_back(forecourt.grown(1.0f));

        // The kiosk behind the forecourt, a small shop with a neon sign.
        Building kiosk;
        kiosk.style = BuildingStyle::ShopRow;
        kiosk.facingDegrees = station.facingDegrees;
        kiosk.floors = 0;
        kiosk.groundFloor = 4.2f;
        kiosk.storey = storeyHeight(kiosk.style);
        const glm::vec2 kioskCentre = road->at(50.0f, 45.5f);
        kiosk.size = {14.0f, kiosk.groundFloor + 0.6f, 9.0f};
        kiosk.position = {kioskCentre.x, 0.5f * kiosk.size.y, kioskCentre.y};
        kiosk.tint = {0.92f, 0.92f, 0.90f};
        kiosk.seed = 0.37f;
        kiosk.roof = RoofAirCon;
        buildings_.push_back(kiosk);
        buildingRects.push_back(rectFacing(kioskCentre, kiosk.facingDegrees, 7.0f, 4.5f));
        const glm::vec2 kioskFront = kioskCentre - road->normal * 4.5f;
        shops_.push_back({kioskFront, kiosk.facingDegrees, 9.0f, "MART", {0.20f, 0.95f, 0.40f}, true, false});

        // Two cars filling up, one each side of the first pump island.
        const glm::vec2 across = road->direction;
        parkCar(station.centre - across * 6.4f, facingOf(-road->normal), random);
        parkCar(station.centre + across * 1.6f, facingOf(road->normal), random);

        // Solid: the pump islands (with the canopy's columns on them), the
        // price sign by the road.
        for (float offset : {-4.0f, 4.0f})
            solidBoxes_.push_back(makeOrientedBox(station.centre + across * offset, station.facingDegrees, {0.6f, 2.3f}));
        solidPosts_.push_back({station.priceSign, 0.9f});
    }

    // ---- The car park, beside the road from R2 to G3 ------------------------
    if (const Frontage* road = frontageFacing(3, {0.0f, 1.0f}))
    {
        const float near = RoadNetwork::halfWidth + RoadNetwork::sidewalkWidth + 0.05f;
        const Rect lot = rectBetween(road->at(40.0f, near), road->at(78.0f, 35.0f));
        addPaved(lot, true);
        reserved.push_back(lot.grown(1.5f));

        // Two rows of bays facing each other across the aisle.
        constexpr float bayWidth = 2.6f;
        constexpr float bayDepth = 5.0f;
        const float rows[2] = {near + 2.0f, 35.0f - bayDepth - 0.5f};
        for (int row = 0; row < 2; ++row)
        {
            for (int bay = 0; bay <= 13; ++bay)
            {
                const float along = 41.5f + bayWidth * static_cast<float>(bay);
                const glm::vec2 a = road->at(along, rows[row]);
                const glm::vec2 b = road->at(along, rows[row] + bayDepth);
                bayLines_.push_back({a.x, a.y, b.x, b.y});
                if (bay < 13 && random.chance(0.42f))
                {
                    // Nose in, towards the lawn or towards the road.
                    const glm::vec2 centre = road->at(along + 0.5f * bayWidth, rows[row] + 0.5f * bayDepth);
                    parkCar(centre, facingOf(row == 0 ? -road->normal : road->normal) + random.uniform(-2.0f, 2.0f), random);
                }
            }
        }
        lampAt(road->at(39.0f, 22.0f));
        lampAt(road->at(79.0f, 22.0f));
    }

    // ---- Buildings along every street ---------------------------------------
    int shopCount = 0;
    int neonCount = 0;
    int paintedCount = 0;
    int companyCount = 0;
    std::vector<ShopName> neonLeft = neonShops();
    std::vector<ShopName> paintedLeft = paintedShops();
    std::shuffle(neonLeft.begin(), neonLeft.end(), std::mt19937(7u));
    std::shuffle(paintedLeft.begin(), paintedLeft.end(), std::mt19937(11u));
    int hotelCount = 0;

    for (const Frontage& frontage : frontages)
    {
        if (frontage.zone == Zone::Park)
            continue;
        float along = frontage.firstFree + random.uniform(0.0f, 5.0f);
        for (int guard = 0; guard < 60 && along < frontage.lastFree - 8.0f; ++guard)
        {
            const BuildingStyle style = pickStyle(frontage.zone, random);
            const StyleSpec& spec = styleSpec(style);
            float width = random.uniform(spec.minWidth, spec.maxWidth);
            if (along + width > frontage.lastFree)
                width = frontage.lastFree - along;
            if (width < spec.minWidth)
            {
                along += 3.0f;
                continue;
            }
            const float depth = random.uniform(spec.minDepth, spec.maxDepth);
            const float setback = spec.setback + (style == BuildingStyle::ShopRow ? 0.0f : random.uniform(0.0f, 1.5f));

            // A house keeps a 4 m drive on its right-hand side (seen from
            // the road); the body of the house is the rest of the plot.
            const float bodyWidth = style == BuildingStyle::House ? width - 4.0f : width;
            const Rect plot = rectBetween(frontage.at(along, setback), frontage.at(along + width, setback + depth));
            const Rect body = rectBetween(frontage.at(along, setback), frontage.at(along + bodyWidth, setback + depth));
            const bool fits = onLand(plot.grown(0.3f)) && clearOf(plot, buildingRects, 3.0f) &&
                              clearOf(plot, keepOut, 0.0f) && clearOf(plot, reserved, 0.5f);
            if (!fits)
            {
                along += 4.0f;
                continue;
            }

            Building building;
            building.style = style;
            building.facingDegrees = facingOf(-frontage.normal);
            building.floors = random.range(spec.minFloors, spec.maxFloors);
            building.groundFloor = spec.groundFloor;
            building.storey = storeyHeight(style);
            const float parapet = style == BuildingStyle::House ? 0.0f : 0.6f;
            const float height = building.groundFloor + static_cast<float>(building.floors) * building.storey + parapet;
            const glm::vec2 centre = body.centre();
            building.size = {body.size().x, height, body.size().y};
            building.position = {centre.x, 0.5f * height, centre.y};
            building.tint = random.pick(stylePalette(style));
            building.seed = random.uniform(0.0f, 1.0f);
            if (style == BuildingStyle::Office || style == BuildingStyle::Hotel)
                building.roof = RoofAirCon | (random.chance(0.4f) ? RoofTank : 0u);
            else if (style == BuildingStyle::Apartment)
                building.roof = (random.chance(0.55f) ? RoofTank : 0u) | (random.chance(0.35f) ? RoofAirCon : 0u);
            else if (style == BuildingStyle::ShopRow)
                building.roof = random.chance(0.5f) ? RoofAirCon : 0u;
            buildings_.push_back(building);
            buildingRects.push_back(body);

            const glm::vec2 outward = -frontage.normal;
            const float frontCentre = along + 0.5f * bodyWidth;
            const glm::vec2 wallMiddle = frontage.at(frontCentre, setback);

            if (style == BuildingStyle::ShopRow)
            {
                // Shops side by side along the front, a paved forecourt
                // from the sidewalk to the glass, and the awnings' strip.
                const int count = std::max(1, static_cast<int>(bodyWidth / 6.5f));
                const float shopWidth = bodyWidth / static_cast<float>(count);
                for (int index = 0; index < count; ++index)
                {
                    Shop shop;
                    shop.centre = frontage.at(along + (static_cast<float>(index) + 0.5f) * shopWidth, setback);
                    shop.facingDegrees = building.facingDegrees;
                    shop.width = shopWidth;
                    shop.neon = (shopCount++ % 2) == 0;
                    const std::vector<ShopName>& pool = shop.neon ? neonLeft : paintedLeft;
                    const int pick = shop.neon ? neonCount++ : paintedCount++;
                    const ShopName& name = pool[static_cast<std::size_t>(pick) % pool.size()];
                    shop.name = name.name;
                    shop.color = name.color;
                    shops_.push_back(shop);
                }
                addPaved(rectBetween(frontage.at(along, RoadNetwork::halfWidth + RoadNetwork::sidewalkWidth + 0.05f),
                                     frontage.at(along + bodyWidth, setback)), false);
                awnings.push_back(rectBetween(frontage.at(along - 0.5f, setback - 3.2f), frontage.at(along + bodyWidth + 0.5f, setback)));
                // A bench looking out at the road, and a bin, at one end of
                // the forecourt.
                if (random.chance(0.6f))
                {
                    const float end = random.chance(0.5f) ? along + 1.4f : along + bodyWidth - 1.4f;
                    const glm::vec2 bench = frontage.at(end, setback - 1.1f);
                    const glm::vec2 bin = frontage.at(end + (end < frontCentre ? 1.6f : -1.6f), setback - 0.9f);
                    if (clearOf(rectAround(bench, 1.0f), keepOut, 0.0f) && clearOf(rectAround(bin, 0.4f), keepOut, 0.0f))
                    {
                        addProp(PropKind::Bench, bench, facingOf(outward));
                        addProp(PropKind::Bin, bin, 0.0f);
                    }
                }
            }
            else if (style == BuildingStyle::Office)
            {
                // A glazed lobby with the company's name above the door, and
                // planters either side of it.
                Shop lobby;
                lobby.centre = wallMiddle;
                lobby.facingDegrees = building.facingDegrees;
                lobby.width = std::min(10.0f, bodyWidth - 4.0f);
                lobby.name = companyNames()[static_cast<std::size_t>(companyCount++) % companyNames().size()];
                lobby.color = {0.14f, 0.16f, 0.20f};
                lobby.awning = false;
                shops_.push_back(lobby);
                for (float side : {-1.0f, 1.0f})
                    addProp(PropKind::Planter, frontage.at(frontCentre + side * (0.5f * lobby.width + 1.4f), setback - 1.2f),
                            building.facingDegrees + 90.0f);
            }
            else if (style == BuildingStyle::Hotel)
            {
                // A lobby, palms at the door, and the hotel's name in neon
                // along the top of the front.
                Shop lobby;
                lobby.centre = wallMiddle;
                lobby.facingDegrees = building.facingDegrees;
                lobby.width = 9.0f;
                lobby.name = "";
                lobby.color = {0.46f, 0.12f, 0.16f};
                shops_.push_back(lobby);
                static const std::array<const char*, 3> hotelNames = {"GRAND HOTEL", "HOTEL CITY", "PARK INN"};
                NeonSign sign;
                sign.text = hotelNames[static_cast<std::size_t>(hotelCount++) % hotelNames.size()];
                const glm::vec2 signAt = wallMiddle + outward * 0.18f;
                sign.centre = {signAt.x, height - 1.3f, signAt.y};
                sign.facingDegrees = building.facingDegrees;
                sign.color = hotelCount % 2 == 0 ? glm::vec3{1.0f, 0.78f, 0.25f} : glm::vec3{0.35f, 0.75f, 1.0f};
                sign.letterHeight = 1.4f;
                neonSigns_.push_back(sign);
                for (float side : {-1.0f, 1.0f})
                {
                    const glm::vec2 spot = frontage.at(frontCentre + side * 6.5f, setback - 1.6f);
                    if (onLawn(spot) && clearOf(rectAround(spot, 1.5f), keepOut, 0.0f))
                        addTree(spot, TreeSpecies::Palm, random);
                }
            }
            else if (style == BuildingStyle::House)
            {
                // The drive, and often a car on it.
                const Rect drive = rectBetween(frontage.at(along + bodyWidth + 0.6f, RoadNetwork::halfWidth + RoadNetwork::sidewalkWidth + 0.05f),
                                               frontage.at(along + width - 0.4f, setback + 7.0f));
                addPaved(drive, true);
                reserved.push_back(drive);
                if (random.chance(0.6f))
                    parkCar(frontage.at(along + bodyWidth + 2.0f, setback + 2.2f), facingOf(frontage.normal), random);
            }
            else if (style == BuildingStyle::Warehouse)
            {
                // A concrete apron from the sidewalk to the roller doors, and
                // now and then a van or a truck standing on it.
                const Rect apron = rectBetween(frontage.at(along, RoadNetwork::halfWidth + RoadNetwork::sidewalkWidth + 0.05f),
                                               frontage.at(along + bodyWidth, setback));
                addPaved(apron, true);
                if (random.chance(0.5f))
                {
                    ParkedCar truck;
                    truck.kind = random.chance(0.5f) ? VehicleKind::Van : VehicleKind::BoxTruck;
                    const float half = 0.5f * vehicleSpec(truck.kind).length;
                    truck.position = frontage.at(along + 2.0f + half, setback - 2.4f);
                    truck.yawDegrees = facingOf(frontage.direction);
                    truck.color = random.pick(vehicleSpec(truck.kind).palette);
                    parkedCars_.push_back(truck);
                }
            }

            along += width + random.uniform(spec.minGap, spec.maxGap);
        }
    }

    // ---- Neon for the shops that have it ------------------------------------
    for (std::size_t index = existingShops; index < shops_.size(); ++index)
    {
        const Shop& shop = shops_[index];
        if (!shop.neon || shop.name.empty())
            continue;
        const glm::vec2 outward = facingVector(shop.facingDegrees);
        const glm::vec2 at = shop.centre + outward * 0.14f;
        NeonSign sign;
        sign.centre = {at.x, 3.72f, at.y};
        sign.facingDegrees = shop.facingDegrees;
        sign.text = shop.name;
        sign.color = shop.color;
        sign.letterHeight = 0.62f;
        sign.flickers = shop.name == "MOTEL" || shop.name == "JAZZ";
        neonSigns_.push_back(sign);
    }

    // ---- Billboards on two roofs, facing the street -------------------------
    int roofBoards = 0;
    for (Building& building : buildings_)
    {
        if (roofBoards >= 2 || building.style != BuildingStyle::ShopRow || building.frontWidth() < 15.0f ||
            building.floors < 2 || (building.roof & RoofAirCon) != 0u)
            continue;
        building.roof |= RoofBillboard;
        Billboard board;
        const glm::vec2 centre {building.position.x, building.position.z};
        board.centre = centre + building.front() * (0.5f * building.depth() - 2.0f);
        board.facingDegrees = building.facingDegrees;
        board.design = 2 + 3 * roofBoards;
        board.baseY = building.size.y;
        billboards_.push_back(board);
        ++roofBoards;
    }

    // ---- Street trees ---------------------------------------------------------
    // In a line 1.7 m in from the sidewalk wherever there is room: never in
    // front of a shop's awning, over a building, on paving or crowding a
    // post, a sign or another tree.
    const auto treeFits = [&](glm::vec2 spot, float canopy)
    {
        if (!onLawn(spot) || insidePaved(spot))
            return false;
        for (const glm::vec2& offset : {glm::vec2{1.0f, 0.0f}, glm::vec2{-1.0f, 0.0f}, glm::vec2{0.0f, 1.0f}, glm::vec2{0.0f, -1.0f}})
        {
            if (!onLawn(spot + offset))
                return false;
        }
        const Rect crown = rectAround(spot, canopy);
        if (!clearOf(crown, buildingRects, 0.4f) || !clearOf(crown, awnings, 0.3f) || !clearOf(rectAround(spot, 0.8f), keepOut, 0.0f) ||
            !clearOf(rectAround(spot, 0.6f), propRects, 0.5f))
            return false;
        for (const Tree& tree : trees_)
        {
            if (glm::length(tree.position - spot) < 6.5f)
                return false;
        }
        for (const ParkedCar& car : parkedCars_)
        {
            if (glm::length(car.position - spot) < 4.0f)
                return false;
        }
        for (const StreetLamp& lamp : extraLamps_)
        {
            if (glm::length(glm::vec2{lamp.position.x, lamp.position.z} - spot) < 3.0f)
                return false;
        }
        for (const GasStation& station : gasStations_)
        {
            if (glm::length(station.centre - spot) < 16.0f)
                return false;
        }
        for (const Billboard& billboard : billboards_)
        {
            if (billboard.baseY == 0.0f && glm::length(billboard.centre - spot) < 6.0f)
                return false;
        }
        return true;
    };
    for (const Frontage& frontage : frontages)
    {
        if (frontage.zone == Zone::Park)
            continue;
        const float spacing = frontage.zone == Zone::Industrial ? 26.0f : 13.0f;
        for (float along = frontage.firstFree + 2.0f; along < frontage.lastFree; along += spacing + random.uniform(-1.5f, 2.5f))
        {
            const glm::vec2 spot = frontage.at(along, RoadNetwork::halfWidth + RoadNetwork::sidewalkWidth + 1.7f);
            if (treeFits(spot, 2.8f))
                addTree(spot, frontage.zone == Zone::Houses && random.chance(0.35f) ? TreeSpecies::Conifer : TreeSpecies::Broadleaf, random);
        }
    }

    // A few more in the gardens and courtyards behind the buildings.
    for (std::size_t block = 0; block < lawns_.size(); ++block)
    {
        if (static_cast<int>(block) == 5)
            continue;
        Rect bounds {glm::vec2{1.0e9f}, glm::vec2{-1.0e9f}};
        for (const glm::vec2& point : lawns_[block])
        {
            bounds.min = glm::min(bounds.min, point);
            bounds.max = glm::max(bounds.max, point);
        }
        const bool houses = blockZone(static_cast<int>(block), {0.0f, 0.0f}) == Zone::Houses;
        const int wanted = houses ? 14 : static_cast<int>(glm::clamp(bounds.size().x * bounds.size().y / 900.0f, 4.0f, 12.0f));
        int planted = 0;
        for (int attempt = 0; attempt < 300 && planted < wanted; ++attempt)
        {
            const glm::vec2 spot = bounds.min + glm::vec2{random.uniform(0.0f, bounds.size().x), random.uniform(0.0f, bounds.size().y)};
            if (!clearOf(rectAround(spot, 3.0f), reserved, 0.0f) || !treeFits(spot, 3.0f))
                continue;
            addTree(spot, random.chance(houses ? 0.5f : 0.25f) ? TreeSpecies::Conifer : TreeSpecies::Broadleaf, random);
            ++planted;
        }
    }

    // ---- Speed-limit signs on the links out to the ring ---------------------
    for (const RoadNetwork::Road& road : network.roads())
    {
        const Junction& from = network.junctions()[road.from];
        const Junction& to = network.junctions()[road.to];
        const bool link = (std::abs(from.centre.x) < 150.0f && std::abs(from.centre.y) < 150.0f) !=
                          (std::abs(to.centre.x) < 150.0f && std::abs(to.centre.y) < 150.0f);
        if (!link)
            continue;
        const glm::vec2 direction = glm::normalize(road.end - road.start);
        // One for each direction, 12 m past the junction the drivers leave.
        for (int way = 0; way < 2; ++way)
        {
            const glm::vec2 heading = way == 0 ? direction : -direction;
            const glm::vec2 origin = way == 0 ? road.start : road.end;
            const Junction& left = way == 0 ? from : to;
            const glm::vec2 right {heading.y, -heading.x};
            const glm::vec2 spot = origin + heading * (RoadNetwork::junctionReach(left) + 12.0f) +
                                   right * (RoadNetwork::halfWidth + 1.2f);
            if (clearOf(rectAround(spot, 0.3f), keepOut, 0.4f))
            {
                props_.push_back({PropKind::SpeedSign, spot, facingOf(-heading)});
                keepOut.push_back(rectAround(spot, 0.8f));
            }
        }
    }

    // ---- The far skyline --------------------------------------------------------
    // A ring of plain towers 400 to 700 m out, taller towards the east,
    // that the haze turns into a silhouette.
    {
        std::vector<Rect> placed;
        Random sky(99u);
        for (int attempt = 0; attempt < 900 && skyline_.size() < 90; ++attempt)
        {
            const float angle = sky.uniform(0.0f, 2.0f * pi);
            const float radius = sky.uniform(420.0f, 700.0f);
            const glm::vec2 centre {std::cos(angle) * radius, std::sin(angle) * radius};
            const StyleSpec& spec = styleSpec(BuildingStyle::Skyline);
            const glm::vec2 half = 0.5f * glm::vec2{sky.uniform(spec.minWidth, spec.maxWidth), sky.uniform(spec.minDepth, spec.maxDepth)};
            const Rect rect {centre - half, centre + half};
            if (!clearOf(rect, placed, 6.0f))
                continue;
            placed.push_back(rect);
            Building tower;
            tower.style = BuildingStyle::Skyline;
            tower.storey = storeyHeight(tower.style);
            const float eastward = 0.5f + 0.5f * std::cos(angle - 0.25f * pi);
            tower.floors = static_cast<int>(sky.uniform(5.0f, 10.0f + 14.0f * eastward));
            const float height = static_cast<float>(tower.floors) * tower.storey + 0.6f;
            tower.size = {rect.size().x, height, rect.size().y};
            tower.position = {centre.x, 0.5f * height, centre.y};
            tower.tint = sky.pick(stylePalette(BuildingStyle::Skyline));
            tower.seed = sky.uniform(0.0f, 1.0f);
            tower.facingDegrees = 0.0f;
            skyline_.push_back(tower);
        }
    }
}
