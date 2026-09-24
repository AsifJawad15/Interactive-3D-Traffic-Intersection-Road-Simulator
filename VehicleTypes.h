#pragma once

#include <glm/vec3.hpp>

#include <array>
#include <cstddef>
#include <vector>

// The kinds of vehicle in the city, what each one measures, and how it
// drives. Pure data with no OpenGL: the traffic simulation, the renderer, the
// cameras and the tests all read the same table.
//
// Local vehicle frame, as everywhere else: +z forward, +y up, and +x to the
// driver's LEFT (the driver sits on the left, for right-hand traffic).

enum class VehicleKind
{
    Sedan,
    Hatchback,
    Suv,
    Taxi,
    Police,
    Van,
    Pickup,
    Ambulance,
    BoxTruck,
    Bus,
    Motorbike
};

inline constexpr std::size_t vehicleKindCount = 11;

// The traffic plans junctions with one body outline per size class, big
// enough for every kind in the class. A class also fixes how the body swings
// in a turn: the front axle follows the lane and the rear axle trails behind
// it, a wheelbase back, so a long vehicle's rear wheels cut inside a corner
// and its nose swings out.
enum class SizeClass
{
    Car,     // sedan, hatchback, SUV, taxi, police, motorbike
    Van,     // van, pickup, ambulance
    Truck,   // box truck
    Bus      // 12 m city bus
};

inline constexpr std::size_t sizeClassCount = 4;

struct SizeClassSpec
{
    const char* name;
    float halfLength;   // of the planning outline, centred on the body centre
    float halfWidth;
    float wheelBase;    // front axle to rear axle
    float frontAxle;    // body centre to front axle, along the body
};

// Junction zones are measured for two widths only: ordinary vehicles and
// wide ones. Length needs no class of its own: a zone is measured along the
// lane, and each vehicle adds its own length to it.
inline constexpr std::size_t widthTierCount = 2;
int widthTier(SizeClass sizeClass);

struct VehicleSpec
{
    const char* name;
    SizeClass sizeClass;

    // Real body size, never larger than the class outline.
    float length;
    float width;
    float height;

    // Driving (the Intelligent Driver Model): cruising speed in town, the
    // largest acceleration, the comfortable braking and the time gap kept to
    // the vehicle in front.
    float cruiseSpeed;
    float acceleration;
    float braking;
    float timeHeadway;

    // Flashing light bar (police car, ambulance).
    bool emergency;

    // Where the driver's eyes are, in the local frame of the body centre.
    glm::vec3 eye;

    std::vector<glm::vec3> palette;
};

const VehicleSpec& vehicleSpec(VehicleKind kind);
const SizeClassSpec& sizeClassSpec(SizeClass sizeClass);
const char* vehicleKindName(VehicleKind kind);

// The traffic mix for a city of `count` vehicles: the two line buses first,
// then a repeating pattern that is mostly cars with a few vans, trucks,
// motorbikes and emergency vehicles.
std::vector<VehicleKind> trafficMix(std::size_t count);

// Buses on the city line when there are at least this many vehicles.
inline constexpr std::size_t lineBusCount = 2;
