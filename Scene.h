#pragma once

#include "DayNight.h"
#include "Mesh.h"
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
    Scene();

    void render(
        const glm::mat4& view,
        const glm::mat4& projection,
        const glm::vec3& cameraPosition,
        const TrafficSystem& traffic,
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
    Texture asphalt_;
    Texture grass_;
    Texture sidewalk_;
    Texture facade_;

    void drawMesh(
        const Mesh& mesh,
        const glm::mat4& model,
        const glm::vec3& color,
        const Texture& texture,
        const glm::vec2& uvScale,
        float shininess,
        const glm::vec3& emissive);

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

    void drawRoads();
    void drawRoadMarkings(float islandHeight);
    void drawBuildings();
    void drawStreetLamp(const glm::vec3& position, bool illuminated);
    void drawTrafficSignal(
        const glm::vec3& position, float yawDegrees, SignalState state, bool signalsLive);
    void drawIsland(float islandHeight);
    void drawFountain(float islandHeight);
    void drawWaterJets(const glm::vec3& origin, float islandHeight);
    void drawTrees();
    void drawVehicle(const Vehicle& vehicle);
    void drawDriverCockpit(const Vehicle& vehicle);
};
