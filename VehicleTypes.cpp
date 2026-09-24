#include "VehicleTypes.h"

namespace
{
    // Paint that suits ordinary private cars.
    const std::vector<glm::vec3> carPaint = {
        {0.82f, 0.06f, 0.035f}, {0.07f, 0.30f, 0.88f}, {0.95f, 0.58f, 0.04f}, {0.13f, 0.62f, 0.34f},
        {0.52f, 0.10f, 0.74f}, {0.86f, 0.86f, 0.89f}, {0.05f, 0.62f, 0.70f}, {0.16f, 0.17f, 0.19f},
        {0.90f, 0.78f, 0.22f}, {0.55f, 0.08f, 0.14f}, {0.40f, 0.45f, 0.50f}, {0.95f, 0.40f, 0.55f}
    };
    const std::vector<glm::vec3> workPaint = {
        {0.90f, 0.91f, 0.92f}, {0.62f, 0.65f, 0.68f}, {0.10f, 0.20f, 0.45f}, {0.72f, 0.12f, 0.08f}
    };

    // Class outlines. Every kind's real body fits inside its class outline,
    // centred on the same point. The wheelbases are chosen so that even the
    // tightest turn a class is allowed (the 6.75 m right turn from the kerb
    // lane) keeps its rear wheels clear of the kerb fillet.
    const std::array<SizeClassSpec, sizeClassCount> classes = {{
        {"car",   2.45f, 0.94f, 2.75f, 1.55f},
        {"van",   2.75f, 0.94f, 2.90f, 1.85f},
        {"truck", 3.95f, 1.15f, 3.40f, 2.60f},
        {"bus",   6.00f, 1.27f, 5.90f, 3.40f}
    }};

    std::array<VehicleSpec, vehicleKindCount> makeSpecs()
    {
        // name, class, length, width, height, cruise, a, b, T, emergency, eye, paint
        return {{
            {"sedan", SizeClass::Car, 4.70f, 1.84f, 1.45f, 8.6f, 1.6f, 2.2f, 1.1f, false,
             {0.37f, 1.24f, -0.10f}, carPaint},
            {"hatchback", SizeClass::Car, 4.06f, 1.80f, 1.48f, 8.4f, 1.6f, 2.2f, 1.1f, false,
             {0.37f, 1.26f, -0.15f}, carPaint},
            {"SUV", SizeClass::Car, 4.80f, 1.88f, 1.74f, 8.4f, 1.5f, 2.2f, 1.2f, false,
             {0.38f, 1.50f, -0.05f}, carPaint},
            {"taxi", SizeClass::Car, 4.70f, 1.84f, 1.45f, 9.0f, 1.7f, 2.3f, 1.0f, false,
             {0.37f, 1.24f, -0.10f}, {{0.97f, 0.74f, 0.05f}}},
            {"police car", SizeClass::Car, 4.84f, 1.88f, 1.48f, 9.0f, 1.8f, 2.4f, 1.0f, true,
             {0.37f, 1.25f, -0.10f}, {{0.93f, 0.94f, 0.96f}}},
            {"van", SizeClass::Van, 5.20f, 1.88f, 2.25f, 8.0f, 1.3f, 2.0f, 1.3f, false,
             {0.42f, 1.78f, 1.05f}, workPaint},
            {"pickup", SizeClass::Van, 5.40f, 1.88f, 1.82f, 8.4f, 1.4f, 2.1f, 1.2f, false,
             {0.40f, 1.50f, 0.55f}, carPaint},
            {"ambulance", SizeClass::Van, 5.50f, 1.88f, 2.60f, 8.8f, 1.4f, 2.1f, 1.2f, true,
             {0.42f, 1.80f, 1.40f}, {{0.95f, 0.95f, 0.92f}}},
            {"box truck", SizeClass::Truck, 7.90f, 2.30f, 3.30f, 7.6f, 1.0f, 1.8f, 1.5f, false,
             {0.50f, 2.25f, 2.90f}, {{0.80f, 0.14f, 0.10f}, {0.12f, 0.30f, 0.62f}, {0.93f, 0.93f, 0.90f}, {0.16f, 0.46f, 0.26f}}},
            {"bus", SizeClass::Bus, 12.00f, 2.54f, 3.15f, 7.6f, 0.95f, 1.7f, 1.5f, false,
             {0.60f, 2.30f, 5.10f}, {{0.80f, 0.10f, 0.08f}, {0.10f, 0.38f, 0.72f}}},
            {"motorbike", SizeClass::Car, 2.10f, 0.80f, 1.55f, 9.4f, 2.2f, 2.6f, 1.0f, false,
             {0.0f, 1.55f, -0.25f}, {{0.10f, 0.10f, 0.11f}, {0.78f, 0.08f, 0.06f}, {0.12f, 0.32f, 0.80f}}}
        }};
    }

    // Seventeen vehicles, repeated: 4 sedans' worth of cars for every van,
    // one truck, one police car and one ambulance per pattern.
    constexpr std::array<VehicleKind, 17> pattern = {
        VehicleKind::Sedan, VehicleKind::Hatchback, VehicleKind::Suv, VehicleKind::Taxi,
        VehicleKind::Van, VehicleKind::Sedan, VehicleKind::Motorbike, VehicleKind::Hatchback,
        VehicleKind::Pickup, VehicleKind::Suv, VehicleKind::Police, VehicleKind::Sedan,
        VehicleKind::BoxTruck, VehicleKind::Taxi, VehicleKind::Hatchback, VehicleKind::Ambulance,
        VehicleKind::Motorbike
    };
}

int widthTier(SizeClass sizeClass)
{
    return sizeClass == SizeClass::Truck || sizeClass == SizeClass::Bus ? 1 : 0;
}

const VehicleSpec& vehicleSpec(VehicleKind kind)
{
    static const std::array<VehicleSpec, vehicleKindCount> specs = makeSpecs();
    return specs[static_cast<std::size_t>(kind)];
}

const SizeClassSpec& sizeClassSpec(SizeClass sizeClass)
{
    return classes[static_cast<std::size_t>(sizeClass)];
}

const char* vehicleKindName(VehicleKind kind)
{
    return vehicleSpec(kind).name;
}

std::vector<VehicleKind> trafficMix(std::size_t count)
{
    std::vector<VehicleKind> mix;
    mix.reserve(count);
    // A city with very few vehicles gets no buses: the line would be most
    // of the traffic.
    const std::size_t buses = count >= 10 ? lineBusCount : 0;
    for (std::size_t index = 0; index < buses; ++index)
        mix.push_back(VehicleKind::Bus);
    for (std::size_t index = 0; mix.size() < count; ++index)
        mix.push_back(pattern[index % pattern.size()]);
    return mix;
}
