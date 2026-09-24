#pragma once

#include "DayNight.h"
#include "LightManager.h"
#include "Mesh.h"
#include "RoadRenderer.h"
#include "Shader.h"
#include "Simulation.h"
#include "Texture.h"

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstddef>

class Scene
{
public:
    explicit Scene(const TrafficSystem& traffic);

    void render(
        const glm::mat4& view,
        const glm::mat4& projection,
        const glm::vec3& cameraPosition,
        const TrafficSystem& traffic,
        const std::vector<VehiclePose>& vehicles,
        const DayNight& dayNight,
        int shadingMode,
        bool driverView,
        std::size_t selectedVehicleIndex,
        float elapsedSeconds);

private:
    Shader shader_;
    Mesh cube_;
    Mesh beveledCube_;
    Mesh buildingMesh_;
    Mesh carCabin_;
    Mesh cylinder_;

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
    float elapsedSeconds_ = 0.0f;

    // The road network, and every street lamp baked into three meshes.
    RoadRenderer roads_;
    LightManager lights_;
    Mesh lampPosts_;
    Mesh lampHeads_;
    Mesh lampBulbs_;

    void buildStreetLamps(const RoadNetwork& network);
    void drawRoads();
    void drawStreetLamps(bool illuminated);
    void drawBuildings();
    void drawSignals(const TrafficSystem& traffic);
    void drawGiveWaySigns(const RoadNetwork& network);
    void drawTrafficSignal(
        const glm::vec3& position, float yawDegrees, SignalState state, bool leftArrow, bool signalsLive);
    void drawIsland(const glm::vec2& centre);
    void drawFountain(const glm::vec2& centre);
    void drawWaterJets(const glm::vec3& origin);
    void drawTrees();
    void drawStreetFurniture();
    void drawFloodlightMast(bool illuminated);
    void drawVehicle(const VehiclePose& vehicle);
    void drawDriverCockpit(const VehiclePose& vehicle);
};
