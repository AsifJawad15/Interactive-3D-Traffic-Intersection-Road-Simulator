#pragma once

#include "LightManager.h"
#include "Mesh.h"
#include "MeshBuilder.h"
#include "Simulation.h"
#include "VehicleTypes.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <vector>

// The vehicles' look. Every body is built once at start-up by the lofted body
// generator: a side profile made of straight runs joined by Bezier corners
// (the Lab 5 curve) is swept across the width, with the sides of the glass
// house leaning inward, the corners rounded seen from above and arches cut
// round the wheels. Glass, lamps, bumpers, cargo boxes, light bars and signs
// are baked into one mesh per material, so a vehicle costs a handful of draw
// calls whatever its kind.

// One piece of a vehicle to draw: a mesh, where it goes, and a flat material.
struct VehiclePart
{
    const Mesh* mesh = nullptr;
    glm::mat4 model {1.0f};
    glm::vec3 color {1.0f};
    float shininess = 32.0f;
    glm::vec3 emissive {0.0f};
    bool matte = false;   // a weak highlight: lamp lenses, tyres
};

// What the lamps show this frame.
struct VehicleLamps
{
    bool headlights = false;   // after dusk
    float seconds = 0.0f;      // clock for the indicators and light bars
};

class VehicleRenderer
{
public:
    VehicleRenderer();

    // Every part of one vehicle, appended to `parts`.
    void collect(const VehiclePose& pose, const VehicleLamps& lamps, std::vector<VehiclePart>& parts) const;

    // What is seen of a vehicle from its own driver's seat: the bonnet, and
    // with `dashboard` the dashboard and the steering wheel.
    void collectDriverView(VehicleKind kind, const glm::vec3& position, float yawDegrees, const glm::vec3& color,
                           bool dashboard, std::vector<VehiclePart>& parts) const;

    // A vehicle standing with its lamps off, baked into static meshes by
    // material, its colours in the vertex colour: body paint and hubs,
    // glass, dark trim and tyres, and the lamp lenses. The parked cars of
    // the city cost four draw calls between them this way.
    void bakeParked(const VehiclePose& pose, MeshBuilder& body, MeshBuilder& glass, MeshBuilder& dark,
                    MeshBuilder& lenses) const;

    // The light a police car's or an ambulance's light bar throws on its
    // surroundings at night, red and blue in turn. False for other kinds.
    bool lightBarLight(const VehiclePose& pose, float seconds, PointLight& light) const;

private:
    struct Wheel
    {
        glm::vec3 centre {0.0f};   // local frame of the body centre
        float radius = 0.33f;
        float width = 0.22f;
        bool steers = false;
    };

    struct KindMeshes
    {
        Mesh paint;          // the body colour
        Mesh second;         // a second colour: cargo box, livery, roof band
        Mesh glass;
        Mesh trim;           // black: bumpers, grille, mirrors, underside
        Mesh headLamps;
        Mesh tailLamps;
        Mesh leftIndicators;
        Mesh rightIndicators;
        Mesh redFlashers;
        Mesh blueFlashers;
        Mesh sign;           // lit sign: taxi roof sign, bus destination
        Mesh doors;          // bus doors, drawn moved out and back when open
        Mesh bonnet;         // for the driver-seat view
        Mesh dashboard;
        glm::vec3 secondColor {0.9f};
        glm::vec3 signColor {1.0f, 0.6f, 0.1f};
        bool signAlwaysLit = false;
        glm::vec3 lightBar {0.0f};   // where the light bar's light shines from
        std::vector<Wheel> wheels;
        // Copies of the geometry, for baking parked vehicles.
        MeshData paintData, secondData, glassData, trimData, headData, tailData, indicatorData, signData;
    };

    std::array<KindMeshes, vehicleKindCount> kinds_;
    Mesh tyre_;
    Mesh hub_;
    MeshData tyreData_;
    MeshData hubData_;
};
