#pragma once

#include "Collision.h"
#include "LightManager.h"
#include "RoadNetwork.h"
#include "Simulation.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstddef>
#include <string>
#include <vector>

// Everything that stands in the city besides the roads: buildings, trees,
// crates, signs, signal heads, billboards and neon signs. One list serves both
// the renderer (what to draw) and the player (what to bump into), so the two
// can never disagree about where something is.
//
// Facing angles use the vehicles' heading convention: a thing with facing
// angle a faces the direction (sin a, cos a) in world (x, z).

// The kinds of building the city is made of. Each has its own storey
// height, window pattern and roof, and the ground floor of a shop row (and
// the lobby of an office or hotel) is glazed.
enum class BuildingStyle
{
    ShopRow,     // 2 to 3 storeys, shops on the ground floor
    Apartment,   // 3 to 6 storeys of flats
    Office,      // a tower with a glass curtain wall
    Hotel,       // a wide slab, lobby on the ground floor
    House,       // 1 to 2 storeys and a pitched roof
    Warehouse,   // one tall storey, a strip of high windows, roller doors
    Skyline      // far away beyond the city, seen through the haze
};
inline constexpr int buildingStyleCount = 7;

// Rooftop clutter. Not every roof has any.
enum RoofItem : unsigned
{
    RoofTank = 1u,        // a water tank on legs
    RoofAirCon = 2u,      // air-conditioning units
    RoofBillboard = 4u    // a billboard facing the street
};

struct Building
{
    glm::vec3 position {0.0f};   // centre of the box
    glm::vec3 size {1.0f};
    glm::vec3 tint {1.0f};
    BuildingStyle style = BuildingStyle::Apartment;
    float facingDegrees = 0.0f;   // the front (door, shops) faces this way; a multiple of 90
    int floors = 3;               // storeys above the ground floor band
    float groundFloor = 0.0f;     // height of the glazed ground floor, 0 for none
    float storey = 3.1f;          // height of each floor above it
    float seed = 0.0f;            // 0..1, picks which windows are lit at night
    unsigned roof = 0u;           // RoofItem flags

    // The front: its outward direction, its width, and the depth behind it.
    glm::vec2 front() const;
    float frontWidth() const;
    float depth() const;
};

// A shop on the ground floor of a building: its glass front, an awning and a
// sign above it, lettered in neon or painted on a lit box.
struct Shop
{
    glm::vec2 centre {0.0f};      // middle of the shop front, on the wall line
    float facingDegrees = 0.0f;
    float width = 6.0f;
    std::string name;
    glm::vec3 color {1.0f};       // awning stripes and sign
    bool neon = false;            // the sign is a neon sign (see World::neonSigns)
    bool awning = true;
};

enum class TreeSpecies
{
    Broadleaf,
    Conifer,
    Palm
};

struct Tree
{
    glm::vec2 position {0.0f};
    float scale = 1.0f;
    float twistDegrees = 0.0f;
    TreeSpecies species = TreeSpecies::Broadleaf;
    int variant = 0;              // which of the species' prebuilt shapes
};

// Street furniture.
enum class PropKind
{
    Bench,
    Bin,
    Planter,      // a concrete trough with a clipped shrub
    Bollard,
    SpeedSign,    // a 50 km/h limit sign
    PicnicTable
};

struct Prop
{
    PropKind kind = PropKind::Bench;
    glm::vec2 position {0.0f};
    float facingDegrees = 0.0f;   // a bench's sitter looks this way
};

// A car left standing: in the car park, at the petrol pumps, in a drive.
struct ParkedCar
{
    VehicleKind kind = VehicleKind::Sedan;
    glm::vec2 position {0.0f};
    float yawDegrees = 0.0f;
    glm::vec3 color {0.8f};
};

// A paved area laid on the lawn: a shop forecourt, the plaza and the park's
// paths (paving), or the car park, drives and the petrol station (asphalt).
struct PavedArea
{
    std::vector<glm::vec2> outline;
    bool asphalt = false;
};

// The park's pond: a stone-edged basin of water.
struct Pond
{
    glm::vec2 centre {0.0f};
    glm::vec2 radii {10.0f, 7.0f};
    std::vector<glm::vec2> outline;   // the water's edge
};

// The petrol station: a canopy over two pump islands, a kiosk, and a price
// sign at the road. Everything is placed relative to `centre`, the middle of
// the canopy, with the road ahead along the facing direction.
struct GasStation
{
    glm::vec2 centre {0.0f};
    float facingDegrees = 0.0f;
    glm::vec2 priceSign {0.0f};   // foot of the price sign by the road
    static constexpr float canopyWidth = 16.0f;    // across the facing direction
    static constexpr float canopyDepth = 10.0f;
    static constexpr float canopyHeight = 5.2f;
};

struct Crate
{
    glm::vec3 position {0.0f};
    float yawDegrees = 0.0f;
};

struct RoadSign
{
    glm::vec3 position {0.0f};
    float yawDegrees = 0.0f;
    glm::vec3 color {1.0f};
};

// A signal head on the driver's right of an approach, facing the cars.
struct SignalHead
{
    std::size_t junction = 0;
    int arm = 0;
    glm::vec2 foot {0.0f};
    float yawDegrees = 0.0f;   // the lenses face local -z; this turns them to the arm
};

struct GiveWaySign
{
    glm::vec2 foot {0.0f};
    float yawDegrees = 0.0f;
};

// The walkers' lights at each end of a signalised crossing: a short pole
// just beyond the band, its lamps facing across the road at the people
// waiting on the other side.
struct WalkSignal
{
    std::size_t crossing = 0;
    glm::vec2 foot {0.0f};
    float facingDegrees = 0.0f;
};

// A roadside billboard on two posts. At night its face is lit from behind,
// except for the one at the central crossroads: that one is lit from the
// front by a small lamp on an arm below it - the Lab 3 spot light.
struct Billboard
{
    glm::vec2 centre {0.0f};     // between the two posts, on the ground
    float facingDegrees = 0.0f;  // the printed face looks this way
    int design = 0;              // which picture (World::billboardText)
    bool spotLit = false;
    float baseY = 0.0f;          // on a roof: the height of the roof, else 0
};

// Neon lettering on a building front.
struct NeonSign
{
    glm::vec3 centre {0.0f};     // middle of the lettering, just off the wall
    float facingDegrees = 0.0f;
    std::string text;
    glm::vec3 color {1.0f};
    float letterHeight = 0.8f;   // metres
    bool flickers = false;       // one tired tube, for character
};

// A shelter at a stop of the bus line, on the sidewalk beside the kerb lane.
// Its open side faces the road; a lit advertising panel closes one end and
// a pole with the bus sign stands at the kerb beside it.
struct BusShelter
{
    glm::vec2 centre {0.0f};
    float facingDegrees = 0.0f;   // the open side looks this way, at the road
    int design = 0;               // the picture on its advertising panel
};

// The spot light on the lamp arm of the spot-lit billboard (Lab 3).
struct SpotLamp
{
    glm::vec3 position {0.0f};
    glm::vec3 target {0.0f};
    float innerDegrees = 30.0f;
    float outerDegrees = 42.0f;
};

class World
{
public:
    // `busStops` are the stops of the traffic's bus line: each gets a shelter.
    static World make(const RoadNetwork& network, const std::vector<BusStopSite>& busStops = {});

    // Size of a bus shelter: length along the kerb, depth, and height.
    static constexpr float shelterLength = 3.6f;
    static constexpr float shelterDepth = 1.5f;
    static constexpr float shelterHeight = 2.5f;

    static constexpr float billboardWidth = 6.0f;
    static constexpr float billboardHeight = 3.0f;
    static constexpr float billboardBottom = 2.6f;   // underside of the panel above the ground
    static const std::vector<std::string>& billboardText();

    const std::vector<Building>& buildings() const { return buildings_; }
    const std::vector<Tree>& trees() const { return trees_; }
    const std::vector<Crate>& crates() const { return crates_; }
    const std::vector<RoadSign>& roadSigns() const { return roadSigns_; }
    const std::vector<SignalHead>& signalHeads() const { return signalHeads_; }
    const std::vector<GiveWaySign>& giveWaySigns() const { return giveWaySigns_; }
    const std::vector<WalkSignal>& walkSignals() const { return walkSignals_; }
    const std::vector<Billboard>& billboards() const { return billboards_; }
    const std::vector<NeonSign>& neonSigns() const { return neonSigns_; }
    const SpotLamp& spotLamp() const { return spotLamp_; }
    const std::vector<BusShelter>& busShelters() const { return busShelters_; }
    const std::vector<Shop>& shops() const { return shops_; }
    const std::vector<Prop>& props() const { return props_; }
    const std::vector<ParkedCar>& parkedCars() const { return parkedCars_; }
    const std::vector<PavedArea>& pavedAreas() const { return pavedAreas_; }
    const std::vector<Pond>& ponds() const { return ponds_; }
    const std::vector<GasStation>& gasStations() const { return gasStations_; }
    // The painted bays of the car park, as short lines (x0, z0, x1, z1).
    const std::vector<glm::vec4>& bayLines() const { return bayLines_; }
    // Lamps of the park paths and the car park, lit like the street lamps.
    const std::vector<StreetLamp>& extraLamps() const { return extraLamps_; }
    // The plaza in the park, round its monument.
    bool hasPlaza() const { return hasPlaza_; }
    glm::vec2 plazaCentre() const { return plazaCentre_; }
    // Buildings far out beyond the city, for the skyline in the haze. They
    // are only scenery: nobody can reach them.
    const std::vector<Building>& skyline() const { return skyline_; }

    // Height of one storey, by style.
    static float storeyHeight(BuildingStyle style);

    // Where a shelter's sign pole stands.
    static glm::vec2 shelterPole(const BusShelter& shelter);

    // Night lights besides the street lamps: the coloured spill of every neon
    // sign and the glow in front of every back-lit billboard. They join the
    // street lamps in the light budget.
    std::vector<PointLight> signLights() const;

    // ---- Collision ----------------------------------------------------------
    // Solid things everyone bumps into: buildings, crates, billboard panels
    // (boxes), and trunks, posts and poles (circles).
    const std::vector<OrientedBox>& solidBoxes() const { return solidBoxes_; }
    const std::vector<Circle>& solidPosts() const { return solidPosts_; }
    // The raised roundabout islands stop a car but not a person on foot; the
    // fountain basin and the pond stop both.
    const std::vector<Circle>& islands() const { return islands_; }
    const std::vector<Circle>& fountains() const { return fountains_; }

    // True on the grass behind the sidewalks, in a block or outside the
    // ring road: where buildings, trees and furniture may stand.
    bool onLawn(glm::vec2 point) const;

    // Height of the ground at a point: the asphalt, or the raised sidewalks,
    // lawns and splitter islands behind the kerbs.
    float surfaceHeight(glm::vec2 point) const;

    // Nobody may leave this square (the lawn runs on towards the fog).
    static constexpr float boundary = 300.0f;

private:
    std::vector<Building> buildings_;
    std::vector<Tree> trees_;
    std::vector<Crate> crates_;
    std::vector<RoadSign> roadSigns_;
    std::vector<SignalHead> signalHeads_;
    std::vector<GiveWaySign> giveWaySigns_;
    std::vector<WalkSignal> walkSignals_;
    std::vector<Billboard> billboards_;
    std::vector<NeonSign> neonSigns_;
    SpotLamp spotLamp_;
    std::vector<BusShelter> busShelters_;
    std::vector<Shop> shops_;
    std::vector<Prop> props_;
    std::vector<ParkedCar> parkedCars_;
    std::vector<PavedArea> pavedAreas_;
    std::vector<Pond> ponds_;
    std::vector<GasStation> gasStations_;
    std::vector<glm::vec4> bayLines_;
    std::vector<StreetLamp> extraLamps_;
    std::vector<Building> skyline_;
    glm::vec2 plazaCentre_ {0.0f};
    bool hasPlaza_ = false;

    // The city generator (WorldCity.cpp): buildings along the streets with
    // their shops, the park, the petrol station, the car park, trees, street
    // furniture, the row outside the ring road and the far skyline.
    void buildCity(const RoadNetwork& network);

    std::vector<OrientedBox> solidBoxes_;
    std::vector<Circle> solidPosts_;
    std::vector<Circle> islands_;
    std::vector<Circle> fountains_;

    // Outlines of everything raised above the road, for surfaceHeight.
    std::vector<std::vector<glm::vec2>> raised_;
    std::vector<glm::vec2> cityEdge_;
    std::vector<std::vector<glm::vec2>> lawns_;   // each block's lawn, inside its sidewalk
    std::vector<glm::vec2> outerLawnEdge_;        // where the lawn outside the ring begins
};
