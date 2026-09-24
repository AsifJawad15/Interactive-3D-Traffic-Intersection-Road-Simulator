#pragma once

#include "DayNight.h"
#include "LightManager.h"
#include "Mesh.h"
#include "Player.h"
#include "RoadRenderer.h"
#include "Shader.h"
#include "Simulation.h"
#include "Texture.h"
#include "VehicleRenderer.h"
#include "World.h"

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstddef>
#include <vector>

// How the player is being watched this frame, which decides what is drawn:
// the outside of the car, or only its bonnet, or nothing at all of you on foot.
enum class PlayerDrawMode
{
    Outside,      // any view that is not yours
    DriverSeat,   // the driver view: only the bonnet shows
    OwnEyes       // you are on foot and look through your own eyes
};

class Scene
{
public:
    Scene(const TrafficSystem& traffic, const World& world);

    void render(
        const glm::mat4& view,
        const glm::mat4& projection,
        const glm::vec3& cameraPosition,
        const TrafficSystem& traffic,
        const std::vector<VehiclePose>& vehicles,
        const PlayerView& player,
        PlayerDrawMode playerDrawMode,
        const DayNight& dayNight,
        int shadingMode,
        bool driverView,
        std::size_t selectedVehicleIndex,
        float elapsedSeconds);

private:
    const World& world_;
    Shader shader_;
    Mesh cube_;
    Mesh beveledCube_;
    Mesh buildingMesh_;
    Mesh cylinder_;
    Mesh faceQuad_;   // a unit square facing +z, texture upright

    // Lab 5 surfaces of revolution. Each is one Bezier profile swept about the
    // Y axis by Mesh::makeBezierRevolution.
    Mesh fountainBasin_;
    Mesh fountainColumn_;
    Mesh treeTrunk_;
    Mesh treeCanopy_;
    Mesh lampPost_;

    Texture white_;
    // Specular map for grass and leaves: a sun highlight on a lawn seen from
    // above would otherwise wash the whole city out to white.
    Texture matte_;
    Texture asphalt_;
    Texture grass_;
    Texture sidewalk_;
    Texture facade_;

    // Lab 4 diffuse + specular pair, applied to the roadside crates so the two
    // maps can be pointed at side by side during the demonstration.
    Texture crateDiffuse_;
    Texture crateSpecular_;
    Texture signFace_;

    // The printed faces of the billboards, one per design, and the neon
    // lettering, one mesh per sign.
    std::vector<Texture> billboardFaces_;
    std::vector<Mesh> neonLetters_;

    void drawMesh(
        const Mesh& mesh,
        const glm::mat4& model,
        const glm::vec3& color,
        const Texture& texture,
        const glm::vec2& uvScale,
        float shininess,
        const glm::vec3& emissive,
        const Texture* specularMap = nullptr);

    void drawCube(
        const glm::mat4& model,
        const glm::vec3& color,
        const Texture& texture,
        const glm::vec2& uvScale = {1.0f, 1.0f},
        float shininess = 24.0f,
        const glm::vec3& emissive = {0.0f, 0.0f, 0.0f});

    void drawBeveledCube(
        const glm::mat4& model,
        const glm::vec3& color,
        const Texture& texture,
        const glm::vec2& uvScale = {1.0f, 1.0f},
        float shininess = 24.0f,
        const glm::vec3& emissive = {0.0f, 0.0f, 0.0f});

    void drawCylinder(
        const glm::mat4& model,
        const glm::vec3& color,
        float shininess = 24.0f,
        const glm::vec3& emissive = {0.0f, 0.0f, 0.0f});

    float waveAmplitude_ = 0.0f;
    float emissiveTextured_ = 0.0f;
    float elapsedSeconds_ = 0.0f;

    // The road network, and every street lamp baked into three meshes.
    RoadRenderer roads_;
    LightManager lights_;
    Mesh lampPosts_;
    Mesh lampHeads_;
    Mesh lampBulbs_;

    // Every vehicle's body, lamps and wheels, and the parts of the ones in
    // view this frame (kept, so a frame never allocates).
    VehicleRenderer vehicleLooks_;
    std::vector<VehiclePart> vehicleParts_;
    std::vector<PointLight> movingLights_;
    bool vehiclesWarmed_ = false;

    // The bus shelters, baked into one mesh per material.
    Mesh shelterFrames_;
    Mesh shelterGlass_;
    Mesh shelterRoofs_;
    Mesh shelterBenches_;
    Mesh shelterSigns_;
    Mesh shelterLetters_;

    void buildStreetLamps(const RoadNetwork& network);
    void buildSigns();
    void buildBusShelters();
    void drawBusShelters(bool illuminated);
    void drawVehicleParts();
    void drawRoads();
    void drawStreetLamps(bool illuminated);
    void drawBuildings();
    void drawSignals(const TrafficSystem& traffic);
    void drawGiveWaySigns();
    void drawTrafficSignal(
        const glm::vec3& position, float yawDegrees, SignalState state, bool leftArrow, bool signalsLive);
    void drawIsland(const glm::vec2& centre);
    void drawFountain(const glm::vec2& centre);
    void drawWaterJets(const glm::vec3& origin);
    void drawTrees();
    void drawStreetFurniture();
    void drawBillboards(bool illuminated);
    void drawNeonSigns(bool illuminated);
    void drawWalker(const PlayerView& player);
};
