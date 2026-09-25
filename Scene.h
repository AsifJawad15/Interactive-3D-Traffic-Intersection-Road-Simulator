#pragma once

#include "DayNight.h"
#include "LightManager.h"
#include "Mesh.h"
#include "PedestrianRenderer.h"
#include "Pedestrians.h"
#include "Player.h"
#include "PropRenderer.h"
#include "RoadRenderer.h"
#include "ShadowMap.h"
#include "Shader.h"
#include "Simulation.h"
#include "Texture.h"
#include "VehicleRenderer.h"
#include "World.h"

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <array>
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

// Everything one frame of the city is drawn from.
struct SceneFrame
{
    glm::mat4 view {1.0f};
    glm::mat4 projection {1.0f};
    glm::vec3 cameraPosition {0.0f};
    const TrafficSystem* traffic = nullptr;
    const std::vector<VehiclePose>* vehicles = nullptr;
    PlayerView player;
    PlayerDrawMode playerDrawMode = PlayerDrawMode::Outside;
    bool cameraOnPlayer = false;
    const std::vector<PedestrianPose>* pedestrians = nullptr;
    const std::vector<WalkerLook>* looks = nullptr;
    const DayNight* dayNight = nullptr;
    int shadingMode = 2;
    bool driverView = false;
    std::size_t selectedVehicleIndex = 0;
    float elapsedSeconds = 0.0f;
    ShadowQuality shadows = ShadowQuality::High;
};

class Scene
{
public:
    Scene(const TrafficSystem& traffic, const World& world);

    // Works out what this frame draws: the lights, and the vehicles and
    // people in view or casting a shadow into it.
    void prepare(const SceneFrame& frame);

    // Draws the shadow maps (after prepare). It leaves its own framebuffer
    // bound, so it comes before the frame's target is bound and cleared.
    void renderShadows(const SceneFrame& frame);

    // Draws the city into the bound target, lit and shadowed.
    void render(const SceneFrame& frame);

    // What the last frame used, for the HUD.
    int shadowMapsDrawn() const { return shadowMapsInUse_; }
    int headlightsLit() const { return lights_.activeSpotCount(); }

private:
    const World& world_;
    Shader sceneShader_;
    // The depth-only shader of the shadow maps, and whichever of the two
    // the drawing code below is feeding (they share the draw functions).
    Shader shadowShader_;
    const Shader* shader_ = &sceneShader_;
    Mesh cube_;
    Mesh beveledCube_;
    Mesh buildingMesh_;
    Mesh cylinder_;
    Mesh faceQuad_;   // a unit square facing +z, texture upright

    // Lab 5 surfaces of revolution. Each is one Bezier profile swept about the
    // Y axis by Mesh::makeBezierRevolution.
    Mesh fountainBasin_;
    Mesh fountainColumn_;
    Mesh lampPost_;

    Texture white_;
    // Specular map for grass and leaves: a sun highlight on a lawn seen from
    // above would otherwise wash the whole city out to white.
    Texture matte_;
    Texture asphalt_;
    Texture grass_;
    Texture sidewalk_;

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
    // view this frame (kept, so a frame never allocates). Each vehicle's
    // parts are a group: drawn on screen, into the shadow maps, or both
    // (a car just out of view can still cast its shadow into it).
    struct PartGroup
    {
        std::size_t begin = 0;
        std::size_t end = 0;
        glm::vec3 centre {0.0f};
        float radius = 0.0f;
        bool seen = true;
        bool casts = true;
    };
    VehicleRenderer vehicleLooks_;
    std::vector<VehiclePart> vehicleParts_;
    std::vector<PartGroup> partGroups_;
    std::vector<PointLight> movingLights_;
    std::vector<SpotLight> headlights_;
    bool vehiclesWarmed_ = false;

    // Shadows: the maps, the box everything that casts stands in, and the
    // state of the pass being drawn.
    ShadowMap shadowMap_;
    ShadowBounds casterBounds_;
    int shadowMapsInUse_ = 0;
    bool shadowPass_ = false;
    bool nearPass_ = false;
    // Whether an object at `centre` belongs in what is being drawn: always
    // on screen, and in the near shadow map only if it reaches into it.
    bool casts(const glm::vec3& centre, float radius) const;
    void drawCasters(const SceneFrame& frame);
    void collectVehicles(const SceneFrame& frame, const glm::mat4& viewProjection, const glm::vec3& shadowStep);
    void collectHeadlights(const SceneFrame& frame);

    // Buildings, shops, trees, paving and street furniture, baked.
    PropRenderer props_;

    // The people, and you on foot: one instanced draw per body shape.
    PedestrianRenderer people_;
    // How many copies of each body shape are seen on screen: all but you,
    // when you look through your own eyes (you still cast a shadow).
    std::array<std::size_t, bodyShapeCount> peopleSeen_ {};
    // The walkers' lights: poles and housings baked, the lamps drawn as
    // instances in this frame's colours.
    Mesh walkSignalPoles_;
    Mesh walkSignalHousings_;
    Mesh walkSignalLens_;
    std::vector<InstanceData> walkLenses_;

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
    void drawVehicleParts(bool shadows);
    void drawRoads();
    void drawStreetLamps(bool illuminated);
    // The city dressing. `darkness` runs from 0 by day to 1 at night and
    // lights the windows.
    void drawCity(bool illuminated, float darkness);
    void drawSignals(const TrafficSystem& traffic);
    void drawGiveWaySigns();
    void drawTrafficSignal(
        const glm::vec3& position, float yawDegrees, SignalState state, bool leftArrow, bool signalsLive);
    void drawIsland(const glm::vec2& centre);
    void drawFountain(const glm::vec2& centre);
    void drawWaterJets(const glm::vec3& origin);
    void drawStreetFurniture();
    void drawBillboards(bool illuminated);
    void drawNeonSigns(bool illuminated);
    void buildWalkSignals();
    void drawWalkSignals(const TrafficSystem& traffic);
    void collectPeople(const SceneFrame& frame, const glm::mat4& viewProjection, const glm::vec3& shadowStep);
    void drawPeople();
    void drawInstances(const Mesh& mesh, const std::vector<InstanceData>& instances, float shininess, bool matte,
                       std::size_t count = static_cast<std::size_t>(-1));
};
