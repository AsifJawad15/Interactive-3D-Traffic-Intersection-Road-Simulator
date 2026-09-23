#include "Scene.h"

#include "Sky.h"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cmath>
#include <numbers>
#include <tuple>
#include <vector>

namespace
{
    glm::mat4 transformed(const glm::vec3& position, const glm::vec3& scale)
    {
        glm::mat4 model(1.0f);
        model = glm::translate(model, position);
        return glm::scale(model, scale);
    }

    // Bezier control points for the surfaces of revolution, written as
    // (radius, height) pairs in metres. In Lab 5 these were picked with the
    // mouse and saved to a file; here they are simply written down, and the
    // same Bernstein polynomial turns each list into a curved solid.
    const std::vector<glm::vec2>& fountainBasinProfile()
    {
        static const std::vector<glm::vec2> profile = {
            {0.00f, 0.00f}, {1.90f, 0.06f}, {1.45f, 0.32f},
            {1.90f, 0.78f}, {2.10f, 1.06f}
        };
        return profile;
    }

    const std::vector<glm::vec2>& fountainColumnProfile()
    {
        static const std::vector<glm::vec2> profile = {
            {0.46f, 0.00f}, {0.20f, 0.72f}, {0.22f, 1.32f},
            {0.78f, 1.70f}, {0.86f, 1.96f}, {0.12f, 2.06f}
        };
        return profile;
    }

    const std::vector<glm::vec2>& treeTrunkProfile()
    {
        static const std::vector<glm::vec2> profile = {
            {0.30f, 0.00f}, {0.15f, 0.90f}, {0.13f, 1.90f}, {0.20f, 2.60f}
        };
        return profile;
    }

    const std::vector<glm::vec2>& treeCanopyProfile()
    {
        static const std::vector<glm::vec2> profile = {
            {0.00f, 0.00f}, {1.95f, 0.35f}, {2.10f, 1.60f},
            {1.20f, 2.60f}, {0.00f, 2.95f}
        };
        return profile;
    }

    const std::vector<glm::vec2>& lampPostProfile()
    {
        static const std::vector<glm::vec2> profile = {
            {0.17f, 0.00f}, {0.10f, 1.60f}, {0.085f, 4.00f}, {0.075f, 5.60f}
        };
        return profile;
    }
}

Scene::Scene()
    : shader_("shaders/scene.vert", "shaders/scene.frag"),
      cube_(Mesh::makeCube()),
      beveledCube_(Mesh::makeBeveledCube(0.09f)),
      buildingMesh_(Mesh::makeBeveledCube(0.025f)),
      carCabin_(Mesh::makeCarCabin()),
      cylinder_(Mesh::makeCylinder(32)),
      fountainBasin_(Mesh::makeBezierRevolution(fountainBasinProfile(), 22, 28)),
      fountainColumn_(Mesh::makeBezierRevolution(fountainColumnProfile(), 22, 24)),
      treeTrunk_(Mesh::makeBezierRevolution(treeTrunkProfile(), 12, 12)),
      treeCanopy_(Mesh::makeBezierRevolution(treeCanopyProfile(), 16, 16)),
      lampPost_(Mesh::makeBezierRevolution(lampPostProfile(), 12, 12)),
      white_(Texture::makeWhite()),
      // Road surface: GL_REPEAT so one tile covers eighty metres of carriageway,
      // and a mipmapped minification filter so the distant road does not shimmer.
      asphalt_(Texture::fromFileOr(
          "assets/asphalt-photoreal.png", &Texture::makeAsphalt,
          GL_REPEAT, GL_REPEAT, GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, true)),
      // These three use an image file when one is present in assets/ and fall
      // back to the generated pattern otherwise, so the project runs with no
      // assets at all but can be re-skinned by dropping in a photograph.
      grass_(Texture::fromFileOr(
          "assets/grass.png", &Texture::makeGrass,
          GL_REPEAT, GL_REPEAT, GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, true)),
      sidewalk_(Texture::fromFileOr(
          "assets/sidewalk.png", &Texture::makeSidewalk,
          GL_REPEAT, GL_REPEAT, GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, true)),
      facade_(Texture::fromFileOr(
          "assets/facade.png", &Texture::makeFacade,
          GL_REPEAT, GL_REPEAT, GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, true)),
      // The Lab 4 container pair. The specular map is the metal banding only,
      // so the crate's painted panels stay matte while its edges catch a
      // highlight - the whole point of a separate specular map.
      crateDiffuse_(Texture::fromFileOr(
          "assets/container2.png", &Texture::makeFacade,
          GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, true)),
      crateSpecular_(Texture::fromFileOr(
          "assets/container2_specular.png", &Texture::makeFacade,
          GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR)),
      signFace_(Texture::makeSidewalk())
{
    shader_.use();
    shader_.setInt("uDiffuseTexture", 0);
    shader_.setInt("uSpecularTexture", 1);
}

void Scene::render(
    const glm::mat4& view,
    const glm::mat4& projection,
    const glm::vec3& cameraPosition,
    const TrafficSystem& traffic,
    const DayNight& dayNight,
    int shadingMode,
    bool driverView,
    std::size_t selectedVehicleIndex,
    float elapsedSeconds)
{
    elapsedSeconds_ = elapsedSeconds;

    shader_.use();
    shader_.setFloat("uTime", elapsedSeconds);
    shader_.setMat4("uView", view);
    shader_.setMat4("uProjection", projection);
    shader_.setVec3("uViewPosition", cameraPosition);
    shader_.setVec3("uAmbientSky", dayNight.skyAmbient());
    shader_.setVec3("uAmbientGround", dayNight.groundAmbient());
    shader_.setFloat("uEmissiveStrength", 3.0f);
    applyAtmosphereUniforms(shader_, dayNight);
    shader_.setVec3("uLightDirection", dayNight.sunDirection());
    shader_.setVec3("uLightColor", dayNight.sunColor());
    shader_.setInt("uShadingMode", shadingMode);

    const std::array<glm::vec3, 4> lampPositions = {
        glm::vec3{-11.0f, 5.58f, -11.0f}, glm::vec3{11.0f, 5.58f, -11.0f},
        glm::vec3{-11.0f, 5.58f, 11.0f}, glm::vec3{11.0f, 5.58f, 11.0f}
    };
    shader_.setInt("uPointLightCount", 4);
    const glm::vec3 lampColor = dayNight.streetLampsOn()
        ? glm::vec3{1.65f, 0.92f, 0.36f}
        : glm::vec3{0.0f};
    for (std::size_t index = 0; index < lampPositions.size(); ++index)
    {
        shader_.setVec3("uPointLightPositions[" + std::to_string(index) + "]", lampPositions[index]);
        shader_.setVec3("uPointLightColors[" + std::to_string(index) + "]", lampColor);
    }

    // A single spot light on a floodlight mast, aimed at the middle of the
    // intersection. Its cut-off angles are uploaded as cosines so the shader
    // can test the cone with a dot product.
    const glm::vec3 spotPosition {14.0f, 12.0f, 14.0f};
    const glm::vec3 spotTarget {0.0f, 0.0f, 0.0f};
    shader_.setVec3("uSpotPosition", spotPosition);
    shader_.setVec3("uSpotDirection", glm::normalize(spotTarget - spotPosition));
    shader_.setVec3("uSpotColor",
        dayNight.streetLampsOn() ? glm::vec3{2.60f, 2.42f, 1.95f} : glm::vec3{0.0f});
    shader_.setFloat("uSpotCutOff", std::cos(glm::radians(16.0f)));
    shader_.setFloat("uSpotOuterCutOff", std::cos(glm::radians(24.0f)));

    // The ground runs out to two kilometres so it reaches the fog and the
    // horizon; the old 82 m square ended in mid-air at the edge of the view.
    drawCube(transformed({0.0f, -0.30f, 0.0f}, {2000.0f, 0.5f, 2000.0f}), {0.72f, 0.86f, 0.72f}, grass_, {780.0f, 780.0f}, 6.0f);
    drawRoads();
    const float islandHeight = traffic.islandHeight();
    drawRoadMarkings(islandHeight);
    drawIsland(islandHeight);
    drawFountain(islandHeight);

    drawBuildings();
    drawTrees();
    drawStreetFurniture();
    drawFloodlightMast(dayNight.streetLampsOn());

    for (const glm::vec3 position : std::array<glm::vec3, 4>{
             glm::vec3{-11.0f, 0.0f, -11.0f}, glm::vec3{11.0f, 0.0f, -11.0f},
             glm::vec3{-11.0f, 0.0f, 11.0f}, glm::vec3{11.0f, 0.0f, 11.0f}})
    {
        drawStreetLamp(position, dayNight.streetLampsOn());
    }

    // The signal heads go dark when the roundabout takes over: give-way rules
    // replace them, so leaving the lenses lit would be misleading.
    const bool signalsActive = islandHeight < 0.5f;
    for (const auto& [lane, position, yaw] : std::array<std::tuple<Lane, glm::vec3, float>, 4>{{
             {Lane::Northbound, {-7.0f, 0.0f, -8.2f}, 0.0f},
             {Lane::Southbound, {7.0f, 0.0f, 8.2f}, 180.0f},
             {Lane::Eastbound, {-8.2f, 0.0f, 7.0f}, 90.0f},
             {Lane::Westbound, {8.2f, 0.0f, -7.0f}, -90.0f}}})
    {
        drawTrafficSignal(position, yaw, traffic.signalFor(lane),
                          traffic.leftArrowFor(lane) == SignalState::Green, signalsActive);
    }

    // Vehicles that have left the scene and wait to re-enter are not drawn.
    const auto& vehicles = traffic.vehicles();
    for (std::size_t index = 0; index < vehicles.size(); ++index)
    {
        if (vehicles[index].active && (!driverView || index != selectedVehicleIndex))
            drawVehicle(vehicles[index]);
    }

    if (driverView && !vehicles.empty())
        drawDriverCockpit(vehicles[selectedVehicleIndex % vehicles.size()]);
}

void Scene::drawMesh(
    const Mesh& mesh,
    const glm::mat4& model,
    const glm::vec3& color,
    const Texture& texture,
    const glm::vec2& uvScale,
    float shininess,
    const glm::vec3& emissive,
    const Texture* specularMap)
{
    shader_.setMat4("uModel", model);
    shader_.setMat3("uNormalMatrix", glm::inverseTranspose(glm::mat3(model)));
    shader_.setVec3("uBaseColor", color);
    shader_.setVec3("uEmissiveColor", emissive);
    shader_.setVec2("uUvScale", uvScale);
    shader_.setFloat("uShininess", shininess);
    shader_.setFloat("uWaveAmplitude", waveAmplitude_);

    // Texture unit 0 is the diffuse map and unit 1 the specular map, matching
    // the Lab 4 material. Objects with no specular map bind a white texture,
    // which makes the specular term uniform across the surface.
    texture.bind(0);
    (specularMap != nullptr ? *specularMap : white_).bind(1);

    mesh.draw();
}

void Scene::drawCube(
    const glm::mat4& model,
    const glm::vec3& color,
    const Texture& texture,
    const glm::vec2& uvScale,
    float shininess,
    const glm::vec3& emissive)
{
    drawMesh(cube_, model, color, texture, uvScale, shininess, emissive);
}

void Scene::drawBeveledCube(
    const glm::mat4& model,
    const glm::vec3& color,
    const Texture& texture,
    const glm::vec2& uvScale,
    float shininess,
    const glm::vec3& emissive)
{
    drawMesh(beveledCube_, model, color, texture, uvScale, shininess, emissive);
}

void Scene::drawCylinder(
    const glm::mat4& model,
    const glm::vec3& color,
    float shininess,
    const glm::vec3& emissive)
{
    drawMesh(cylinder_, model, color, white_, {1.0f, 1.0f}, shininess, emissive);
}

void Scene::drawRoads()
{
    // The asphalt photograph is very dark, so it is tinted brighter: real worn
    // asphalt reflects roughly 7-10 % of the light, not the 4 % of the image.
    const glm::vec3 asphaltTint {1.28f, 1.28f, 1.30f};
    drawCube(transformed({0.0f, 0.0f, 0.0f}, {12.0f, 0.12f, 80.0f}), asphaltTint, asphalt_, {4.0f, 28.0f}, 8.0f);
    drawCube(transformed({0.0f, 0.01f, 0.0f}, {80.0f, 0.12f, 12.0f}), asphaltTint, asphalt_, {28.0f, 4.0f}, 8.0f);

    const std::array<glm::vec3, 4> pavementCenters = {
        glm::vec3{-23.0f, 0.12f, -23.0f}, glm::vec3{23.0f, 0.12f, -23.0f},
        glm::vec3{-23.0f, 0.12f, 23.0f}, glm::vec3{23.0f, 0.12f, 23.0f}
    };
    for (const glm::vec3 center : pavementCenters)
        drawCube(transformed(center, {32.0f, 0.20f, 32.0f}), {0.92f, 0.92f, 0.92f}, sidewalk_, {8.0f, 8.0f}, 12.0f);
}

void Scene::drawRoadMarkings(float islandHeight)
{
    const glm::vec3 white {0.96f, 0.96f, 0.90f};
    const glm::vec3 yellow {0.95f, 0.72f, 0.08f};

    for (int value = -36; value <= 36; value += 6)
    {
        if (value > -10 && value < 10)
            continue;
        drawCube(transformed({0.0f, 0.09f, static_cast<float>(value)}, {0.14f, 0.035f, 3.0f}), yellow, white_, {1, 1}, 4.0f);
        drawCube(transformed({static_cast<float>(value), 0.10f, 0.0f}, {3.0f, 0.035f, 0.14f}), yellow, white_, {1, 1}, 4.0f);
    }

    // Lane edge lines run the full length of each arm while the signals are in
    // charge, and stop short of the circulating area in roundabout mode. Ending
    // the strips is what removes the markings from the middle; nothing is ever
    // laid over the carriageway, so the road surface stays perfectly flat.
    const float clearRadius = glm::mix(0.0f, 11.0f, islandHeight);
    const float armLength = 40.0f - clearRadius;
    const float armCentre = (40.0f + clearRadius) * 0.5f;

    for (float side : {-1.0f, 1.0f})
    {
        for (float edge : {-5.7f, 5.7f})
        {
            drawCube(
                transformed({edge, 0.09f, side * armCentre}, {0.12f, 0.035f, armLength}),
                white, white_, {1, 1}, 4.0f);
            drawCube(
                transformed({side * armCentre, 0.10f, edge}, {armLength, 0.035f, 0.12f}),
                white, white_, {1, 1}, 4.0f);
        }
    }

    // Pedestrian crossings sit just behind the stop line while the signals are
    // running, and slide outboard of the circulating ring in roundabout mode so
    // that traffic never drives across them.
    const float crossing = glm::mix(7.2f, 13.6f, islandHeight);
    for (int stripe = -5; stripe <= 5; stripe += 2)
    {
        const float offset = static_cast<float>(stripe);
        drawCube(transformed({offset, 0.115f, -crossing}, {0.75f, 0.035f, 2.2f}), white, white_, {1, 1}, 4.0f);
        drawCube(transformed({offset, 0.115f, crossing}, {0.75f, 0.035f, 2.2f}), white, white_, {1, 1}, 4.0f);
        drawCube(transformed({-crossing, 0.12f, offset}, {2.2f, 0.035f, 0.75f}), white, white_, {1, 1}, 4.0f);
        drawCube(transformed({crossing, 0.12f, offset}, {2.2f, 0.035f, 0.75f}), white, white_, {1, 1}, 4.0f);
    }
}

void Scene::drawBuildings()
{
    struct Building
    {
        glm::vec3 position;
        glm::vec3 size;
        glm::vec3 tint;
    };

    const std::array<Building, 8> buildings = {{
        {{-24.0f, 5.7f, -24.0f}, {12.0f, 11.0f, 11.0f}, {0.92f, 0.78f, 0.68f}},
        {{-35.0f, 4.2f, -18.0f}, {7.0f, 8.0f, 9.0f}, {0.76f, 0.82f, 0.88f}},
        {{24.0f, 7.2f, -24.0f}, {12.0f, 14.0f, 11.0f}, {0.78f, 0.86f, 0.92f}},
        {{35.0f, 4.7f, -18.0f}, {7.0f, 9.0f, 9.0f}, {0.92f, 0.76f, 0.64f}},
        {{-24.0f, 6.2f, 24.0f}, {12.0f, 12.0f, 11.0f}, {0.84f, 0.88f, 0.78f}},
        {{-35.0f, 5.2f, 18.0f}, {7.0f, 10.0f, 9.0f}, {0.90f, 0.70f, 0.62f}},
        {{24.0f, 5.2f, 24.0f}, {12.0f, 10.0f, 11.0f}, {0.90f, 0.82f, 0.72f}},
        {{35.0f, 6.7f, 18.0f}, {7.0f, 13.0f, 9.0f}, {0.72f, 0.82f, 0.90f}}
    }};

    for (const Building& building : buildings)
    {
        drawMesh(buildingMesh_, transformed(building.position, building.size), building.tint, facade_, {3.0f, 4.0f}, 28.0f, glm::vec3{0.0f});

        drawMesh(
            buildingMesh_,
            transformed({building.position.x, 0.42f, building.position.z},
                        {building.size.x + 0.35f, 0.48f, building.size.z + 0.35f}),
            {0.20f, 0.22f, 0.24f}, white_, {1, 1}, 24.0f, glm::vec3{0.0f});

        const glm::vec3 roofPosition = building.position + glm::vec3(0.0f, building.size.y * 0.5f + 0.18f, 0.0f);
        drawBeveledCube(transformed(roofPosition, {building.size.x + 0.5f, 0.35f, building.size.z + 0.5f}), {0.22f, 0.24f, 0.27f}, white_, {1, 1}, 16.0f);

        const float entranceZ = building.position.z -
            std::copysign(building.size.z * 0.5f + 0.035f, building.position.z);
        drawBeveledCube(
            transformed({building.position.x, 1.55f, entranceZ}, {2.25f, 2.55f, 0.10f}),
            {0.035f, 0.12f, 0.18f}, white_, {1, 1}, 72.0f);
        drawBeveledCube(
            transformed({building.position.x, 2.95f, entranceZ}, {2.75f, 0.16f, 1.05f}),
            {0.15f, 0.17f, 0.20f}, white_, {1, 1}, 42.0f);

        glm::mat4 roofUnit = glm::translate(glm::mat4(1.0f), roofPosition + glm::vec3{1.6f, 0.55f, -1.4f});
        roofUnit = glm::scale(roofUnit, {1.35f, 0.75f, 1.1f});
        drawBeveledCube(roofUnit, {0.42f, 0.45f, 0.47f}, white_, {1, 1}, 18.0f);
    }
}

void Scene::drawIsland(float islandHeight)
{
    if (islandHeight <= 0.001f)
        return;

    const float radius = TrafficSystem::islandRadius;

    // Nothing is laid on top of the carriageway here. A roundabout has no lane
    // markings through the middle, and drawRoadMarkings simply stops drawing
    // them inside the circulating area, so the road surface itself stays flat.

    // The island rises out of the road rather than popping into place. It
    // stands only as proud of the carriageway as a real kerb does - about
    // nineteen centimetres - so it reads as an island and not as raised road.
    const float lift = glm::mix(-0.60f, 0.10f, islandHeight);
    constexpr float kerbThickness = 0.30f;

    // Kerb first: wider than the island but with its top face LOWER, so the
    // grass disc that follows stands proud of it and the kerb is left showing
    // as a rim rather than as a lid over the grass.
    drawMesh(
        cylinder_,
        transformed({0.0f, lift, 0.0f},
                    {radius * 2.0f + 0.6f, kerbThickness, radius * 2.0f + 0.6f}),
        {0.80f, 0.80f, 0.82f}, sidewalk_, {5.0f, 5.0f}, 16.0f, glm::vec3{0.0f});

    drawMesh(
        cylinder_,
        transformed({0.0f, lift + 0.04f, 0.0f},
                    {radius * 2.0f, kerbThickness, radius * 2.0f}),
        {0.60f, 0.80f, 0.55f}, grass_, {4.0f, 4.0f}, 6.0f, glm::vec3{0.0f});
}

void Scene::drawFountain(float islandHeight)
{
    if (islandHeight <= 0.001f)
        return;

    // Sits on the top face of the island's grass disc: lift + 0.04 + half the
    // kerb thickness, evaluated at a fully raised island.
    const float base = glm::mix(-1.4f, 0.29f, islandHeight);
    const glm::vec3 origin {0.0f, base, 0.0f};

    // Basin and column are Bezier surfaces of revolution, drawn at their
    // authored size because the control points are already in metres.
    drawMesh(fountainBasin_, glm::translate(glm::mat4(1.0f), origin),
             {0.86f, 0.84f, 0.78f}, sidewalk_, {3.0f, 2.0f}, 46.0f, glm::vec3{0.0f});

    drawMesh(fountainColumn_, glm::translate(glm::mat4(1.0f), origin + glm::vec3(0.0f, 0.55f, 0.0f)),
             {0.90f, 0.88f, 0.82f}, sidewalk_, {2.0f, 2.0f}, 58.0f, glm::vec3{0.0f});

    // The water surface in the basin. Its vertices ripple in the vertex shader,
    // so the animation costs one uniform rather than a mesh rebuild.
    waveAmplitude_ = 0.035f;
    drawMesh(
        cylinder_,
        transformed(origin + glm::vec3(0.0f, 0.80f, 0.0f), {3.55f, 0.10f, 3.55f}),
        {0.35f, 0.62f, 0.78f}, white_, {1.0f, 1.0f}, 110.0f, glm::vec3{0.03f, 0.07f, 0.10f});
    waveAmplitude_ = 0.0f;

    drawWaterJets(origin + glm::vec3(0.0f, 2.45f, 0.0f), islandHeight);
}

void Scene::drawWaterJets(const glm::vec3& origin, float islandHeight)
{
    // Each droplet follows the projectile equation p = p0 + v0 t + 0.5 g t^2.
    // Giving the droplets of one jet evenly spaced ages turns a handful of
    // cubes into a continuous stream.
    constexpr int jets = 5;
    constexpr int dropletsPerJet = 10;
    constexpr float gravity = -9.81f;

    for (int jet = 0; jet < jets; ++jet)
    {
        glm::vec3 velocity {0.0f, 4.30f, 0.0f};
        if (jet > 0)
        {
            const float angle = static_cast<float>(jet - 1) * 0.5f * std::numbers::pi_v<float>;
            velocity = glm::vec3{1.85f * std::cos(angle), 3.55f, 1.85f * std::sin(angle)};
        }

        // Time for the droplet to fall back to the height it started from.
        const float flightTime = -2.0f * velocity.y / gravity;

        for (int droplet = 0; droplet < dropletsPerJet; ++droplet)
        {
            const float phase = static_cast<float>(droplet) / static_cast<float>(dropletsPerJet);
            float age = std::fmod(elapsedSeconds_ + phase * flightTime, flightTime);
            if (age < 0.0f)
                age += flightTime;

            const glm::vec3 position = origin + velocity * age +
                glm::vec3{0.0f, 0.5f * gravity * age * age, 0.0f};

            const float size = 0.10f - 0.03f * (age / flightTime);
            drawCube(
                transformed(position, glm::vec3(size)),
                {0.72f, 0.88f, 0.98f}, white_, {1.0f, 1.0f}, 120.0f,
                glm::vec3{0.22f, 0.34f, 0.42f} * islandHeight);
        }
    }
}

void Scene::drawTrees()
{
    // Trees line the four approaches, clear of the carriageway and of the
    // pavements the buildings sit on.
    static const std::array<glm::vec2, 12> positions = {
        glm::vec2{-9.5f, -16.0f}, glm::vec2{9.5f, -16.0f},
        glm::vec2{-9.5f, 16.0f},  glm::vec2{9.5f, 16.0f},
        glm::vec2{-16.0f, -9.5f}, glm::vec2{-16.0f, 9.5f},
        glm::vec2{16.0f, -9.5f},  glm::vec2{16.0f, 9.5f},
        glm::vec2{-9.5f, -27.0f}, glm::vec2{9.5f, 27.0f},
        glm::vec2{-27.0f, 9.5f},  glm::vec2{27.0f, -9.5f}
    };

    for (std::size_t index = 0; index < positions.size(); ++index)
    {
        const glm::vec3 root {positions[index].x, 0.22f, positions[index].y};

        // A small per-tree scale and twist stops twelve identical copies from
        // reading as wallpaper.
        const float variation = 0.86f + 0.06f * static_cast<float>(index % 4);
        const float twist = static_cast<float>(index) * 37.0f;

        glm::mat4 trunk = glm::translate(glm::mat4(1.0f), root);
        trunk = glm::rotate(trunk, glm::radians(twist), {0.0f, 1.0f, 0.0f});
        trunk = glm::scale(trunk, {variation, variation, variation});
        drawMesh(treeTrunk_, trunk, {0.34f, 0.24f, 0.16f}, white_, {1.0f, 1.0f}, 12.0f, glm::vec3{0.0f});

        glm::mat4 canopy = glm::translate(glm::mat4(1.0f), root + glm::vec3(0.0f, 2.15f * variation, 0.0f));
        canopy = glm::rotate(canopy, glm::radians(twist * 1.7f), {0.0f, 1.0f, 0.0f});
        canopy = glm::scale(canopy, {variation, variation, variation});
        drawMesh(treeCanopy_, canopy, {0.20f, 0.44f, 0.20f}, grass_, {2.0f, 2.0f}, 8.0f, glm::vec3{0.0f});
    }
}

void Scene::drawStreetFurniture()
{
    // Roadside crates carrying the Lab 4 diffuse + specular pair. The specular
    // map is the metal frame of the container, so the painted panels stay flat
    // while the banding picks up the sun and the street lamps.
    static const std::array<glm::vec3, 6> cratePositions = {
        glm::vec3{-13.5f, 0.72f, -13.5f}, glm::vec3{-12.0f, 0.72f, -15.6f},
        glm::vec3{13.5f, 0.72f, 13.5f},   glm::vec3{15.6f, 0.72f, 12.0f},
        glm::vec3{-14.6f, 0.72f, 14.6f},  glm::vec3{14.6f, 0.72f, -14.6f}
    };

    for (std::size_t index = 0; index < cratePositions.size(); ++index)
    {
        glm::mat4 crate = glm::translate(glm::mat4(1.0f), cratePositions[index]);
        crate = glm::rotate(crate, glm::radians(17.0f * static_cast<float>(index)), {0.0f, 1.0f, 0.0f});
        crate = glm::scale(crate, {1.05f, 1.05f, 1.05f});
        drawMesh(cube_, crate, {1.0f, 1.0f, 1.0f}, crateDiffuse_, {1.0f, 1.0f}, 64.0f,
                 glm::vec3{0.0f}, &crateSpecular_);
    }

    // Road signs: a Bezier post carrying a plate that faces oncoming traffic.
    struct Sign
    {
        glm::vec3 position;
        float yawDegrees;
        glm::vec3 color;
    };

    static const std::array<Sign, 4> signs = {
        Sign{{-9.2f, 0.22f, -13.0f}, 0.0f, {0.86f, 0.16f, 0.12f}},
        Sign{{9.2f, 0.22f, 13.0f}, 180.0f, {0.86f, 0.16f, 0.12f}},
        Sign{{-13.0f, 0.22f, 9.2f}, 90.0f, {0.14f, 0.32f, 0.72f}},
        Sign{{13.0f, 0.22f, -9.2f}, -90.0f, {0.14f, 0.32f, 0.72f}}
    };

    for (const Sign& sign : signs)
    {
        glm::mat4 parent = glm::translate(glm::mat4(1.0f), sign.position);
        parent = glm::rotate(parent, glm::radians(sign.yawDegrees), {0.0f, 1.0f, 0.0f});

        glm::mat4 post = glm::scale(parent, {0.42f, 0.42f, 0.42f});
        drawMesh(lampPost_, post, {0.62f, 0.63f, 0.66f}, white_, {1.0f, 1.0f}, 48.0f, glm::vec3{0.0f});

        glm::mat4 plate = glm::translate(parent, {0.0f, 2.15f, 0.0f});
        plate = glm::scale(plate, {0.90f, 0.90f, 0.09f});
        drawBeveledCube(plate, sign.color, signFace_, {1.0f, 1.0f}, 52.0f);

        glm::mat4 band = glm::translate(parent, {0.0f, 2.15f, -0.06f});
        band = glm::scale(band, {0.62f, 0.16f, 0.05f});
        drawBeveledCube(band, {0.96f, 0.96f, 0.94f}, white_, {1.0f, 1.0f}, 60.0f);
    }
}

void Scene::drawFloodlightMast(bool illuminated)
{
    // The mast that carries the spot light. Its head is tilted towards the
    // middle of the intersection so the cone in the shader and the geometry
    // the viewer sees agree with each other.
    const glm::vec3 base {14.0f, 0.22f, 14.0f};

    glm::mat4 mast = glm::translate(glm::mat4(1.0f), base);
    mast = glm::scale(mast, {1.0f, 2.15f, 1.0f});
    drawMesh(lampPost_, mast, {0.10f, 0.11f, 0.13f}, white_, {1.0f, 1.0f}, 34.0f, glm::vec3{0.0f});

    glm::mat4 head = glm::translate(glm::mat4(1.0f), {14.0f, 12.0f, 14.0f});
    head = glm::rotate(head, glm::radians(-135.0f), {0.0f, 1.0f, 0.0f});
    head = glm::rotate(head, glm::radians(-38.0f), {1.0f, 0.0f, 0.0f});

    drawBeveledCube(glm::scale(head, {1.10f, 0.55f, 0.40f}),
                    {0.13f, 0.14f, 0.16f}, white_, {1.0f, 1.0f}, 40.0f);

    drawBeveledCube(
        glm::scale(glm::translate(head, {0.0f, 0.0f, 0.24f}), {0.94f, 0.42f, 0.08f}),
        illuminated ? glm::vec3{1.0f, 0.96f, 0.82f} : glm::vec3{0.26f, 0.25f, 0.22f},
        white_, {1.0f, 1.0f}, 96.0f,
        illuminated ? glm::vec3{0.85f, 0.78f, 0.58f} : glm::vec3{0.0f});
}

void Scene::drawStreetLamp(const glm::vec3& position, bool illuminated)
{
    // The tapered post is another Bezier surface of revolution, authored in
    // metres so it needs a translation and nothing else.
    drawMesh(
        lampPost_, glm::translate(glm::mat4(1.0f), position),
        {0.08f, 0.09f, 0.11f}, white_, {1.0f, 1.0f}, 34.0f, glm::vec3{0.0f});

    drawBeveledCube(transformed(position + glm::vec3(0.0f, 5.75f, 0.0f), {0.75f, 0.28f, 0.75f}), {0.12f, 0.13f, 0.16f}, white_, {1, 1}, 40.0f);
    drawBeveledCube(
        transformed(position + glm::vec3(0.0f, 5.58f, 0.0f), {0.46f, 0.16f, 0.46f}),
        illuminated ? glm::vec3{1.0f, 0.72f, 0.30f} : glm::vec3{0.24f, 0.21f, 0.16f},
        white_, {1, 1}, 64.0f,
        illuminated ? glm::vec3{0.72f, 0.38f, 0.08f} : glm::vec3{0.0f});
}

void Scene::drawTrafficSignal(
    const glm::vec3& position, float yawDegrees, SignalState state, bool leftArrow, bool signalsLive)
{
    glm::mat4 parent(1.0f);
    parent = glm::translate(parent, position);
    parent = glm::rotate(parent, glm::radians(yawDegrees), {0.0f, 1.0f, 0.0f});

    glm::mat4 pole = glm::translate(parent, {0.0f, 2.25f, 0.0f});
    pole = glm::scale(pole, {0.13f, 4.5f, 0.13f});
    drawCylinder(pole, {0.07f, 0.08f, 0.09f}, 30.0f);

    glm::mat4 housing = glm::translate(parent, {0.0f, 4.45f, 0.0f});
    housing = glm::scale(housing, {0.72f, 1.85f, 0.55f});
    drawBeveledCube(housing, {0.025f, 0.03f, 0.035f}, white_, {1, 1}, 24.0f);

    const std::array<glm::vec3, 3> activeColors = {
        glm::vec3{1.00f, 0.025f, 0.01f}, glm::vec3{1.00f, 0.58f, 0.015f}, glm::vec3{0.02f, 0.86f, 0.10f}
    };
    const std::array<glm::vec3, 3> inactiveColors = {
        glm::vec3{0.20f, 0.012f, 0.008f}, glm::vec3{0.20f, 0.10f, 0.008f}, glm::vec3{0.008f, 0.16f, 0.02f}
    };
    for (int index = 0; index < 3; ++index)
    {
        // In roundabout mode every lens is dark, whatever the phase timer says.
        const bool lit = signalsLive &&
                         ((index == 0 && state == SignalState::Red) ||
                          (index == 1 && state == SignalState::Yellow) ||
                          (index == 2 && state == SignalState::Green));
        const glm::vec3 color = lit ? activeColors[static_cast<size_t>(index)]
                                    : inactiveColors[static_cast<size_t>(index)];
        const glm::vec3 emissive = lit ? color * 0.62f : glm::vec3{0.0f};

        glm::mat4 lens = glm::translate(parent, {0.0f, 5.02f - index * 0.58f, -0.33f});
        lens = glm::rotate(lens, glm::radians(90.0f), {1.0f, 0.0f, 0.0f});
        lens = glm::scale(lens, {0.38f, 0.15f, 0.38f});
        drawCylinder(lens, color, 54.0f, emissive);
    }

    // A small box on the kerb side carries the left-turn arrow: a green bar
    // and head, lit only during the protected left-turn phase.
    glm::mat4 arrowHousing = glm::translate(parent, {0.56f, 3.84f, 0.0f});
    arrowHousing = glm::scale(arrowHousing, {0.46f, 0.46f, 0.50f});
    drawBeveledCube(arrowHousing, {0.025f, 0.03f, 0.035f}, white_, {1, 1}, 24.0f);

    const bool arrowLit = signalsLive && leftArrow;
    const glm::vec3 arrowColor = arrowLit ? glm::vec3{0.02f, 0.86f, 0.10f} : glm::vec3{0.008f, 0.12f, 0.02f};
    const glm::vec3 arrowGlow = arrowLit ? arrowColor * 0.62f : glm::vec3{0.0f};

    // The arrow points to the driver's left, which is +x in the head's frame.
    glm::mat4 shaft = glm::translate(parent, {0.54f, 3.84f, -0.26f});
    shaft = glm::scale(shaft, {0.24f, 0.06f, 0.03f});
    drawBeveledCube(shaft, arrowColor, white_, {1, 1}, 54.0f, arrowGlow);
    for (float slope : {1.0f, -1.0f})
    {
        glm::mat4 head = glm::translate(parent, {0.63f, 3.84f + slope * 0.045f, -0.26f});
        head = glm::rotate(head, glm::radians(-slope * 40.0f), {0.0f, 0.0f, 1.0f});
        head = glm::scale(head, {0.13f, 0.05f, 0.03f});
        drawBeveledCube(head, arrowColor, white_, {1, 1}, 54.0f, arrowGlow);
    }
}

void Scene::drawVehicle(const Vehicle& vehicle)
{
    glm::mat4 parent(1.0f);
    parent = glm::translate(parent, vehicle.position);
    parent = glm::rotate(parent, glm::radians(vehicle.yawDegrees), {0.0f, 1.0f, 0.0f});

    glm::mat4 shadow = glm::translate(parent, {0.0f, 0.035f, 0.0f});
    shadow = glm::scale(shadow, {1.65f, 0.025f, 3.25f});
    drawCylinder(shadow, {0.035f, 0.038f, 0.042f}, 2.0f);

    glm::mat4 body = glm::translate(parent, {0.0f, 0.55f, 0.0f});
    body = glm::scale(body, {1.84f, 0.66f, 3.90f});
    drawBeveledCube(body, vehicle.color, white_, {1, 1}, 68.0f);

    glm::mat4 lowerBody = glm::translate(parent, {0.0f, 0.35f, 0.0f});
    lowerBody = glm::scale(lowerBody, {1.88f, 0.22f, 3.62f});
    drawBeveledCube(lowerBody, vehicle.color * 0.52f, white_, {1, 1}, 40.0f);

    glm::mat4 cabin = glm::translate(parent, {0.0f, 1.08f, -0.18f});
    cabin = glm::scale(cabin, {1.50f, 0.76f, 1.92f});
    drawMesh(carCabin_, cabin, {0.045f, 0.16f, 0.24f}, white_, {1, 1}, 96.0f, glm::vec3{0.0f});

    glm::mat4 roof = glm::translate(parent, {0.0f, 1.48f, -0.25f});
    roof = glm::scale(roof, {1.20f, 0.13f, 1.00f});
    drawBeveledCube(roof, vehicle.color * 0.92f, white_, {1, 1}, 72.0f);

    for (float x : {-1.01f, 1.01f})
    {
        glm::mat4 mirror = glm::translate(parent, {x, 1.02f, 0.34f});
        mirror = glm::scale(mirror, {0.24f, 0.16f, 0.34f});
        drawBeveledCube(mirror, vehicle.color * 0.78f, white_, {1, 1}, 60.0f);
    }

    for (float x : {-0.95f, 0.95f})
    {
        for (float z : {-1.25f, 1.25f})
        {
            // Hierarchy: body -> wheel hub -> steering -> rolling axle. Only the
            // front pair (positive z, the end the headlights are on) steers.
            const bool frontWheel = z > 0.0f;

            glm::mat4 hub = glm::translate(parent, {x, 0.42f, z});
            if (frontWheel)
                hub = glm::rotate(hub, glm::radians(vehicle.steerAngleDegrees), {0.0f, 1.0f, 0.0f});
            hub = glm::rotate(hub, glm::radians(90.0f), {0.0f, 0.0f, 1.0f});
            hub = glm::rotate(hub, glm::radians(vehicle.wheelAngleDegrees), {0.0f, 1.0f, 0.0f});

            drawCylinder(glm::scale(hub, {0.68f, 0.28f, 0.68f}), {0.025f, 0.028f, 0.03f}, 18.0f);
            drawCylinder(glm::scale(hub, {0.39f, 0.31f, 0.39f}), {0.58f, 0.61f, 0.64f}, 72.0f);
        }
    }

    for (float x : {-0.58f, 0.58f})
    {
        glm::mat4 headlight = glm::translate(parent, {x, 0.67f, 1.93f});
        headlight = glm::scale(headlight, {0.38f, 0.20f, 0.08f});
        drawBeveledCube(headlight, {1.0f, 0.88f, 0.50f}, white_, {1, 1}, 70.0f, {0.14f, 0.10f, 0.03f});

        glm::mat4 tailLight = glm::translate(parent, {x, 0.67f, -1.93f});
        tailLight = glm::scale(tailLight, {0.38f, 0.20f, 0.08f});
        drawBeveledCube(tailLight, {0.88f, 0.025f, 0.012f}, white_, {1, 1}, 60.0f, {0.10f, 0.005f, 0.002f});
    }

    glm::mat4 frontBumper = glm::translate(parent, {0.0f, 0.34f, 1.98f});
    frontBumper = glm::scale(frontBumper, {1.38f, 0.14f, 0.10f});
    drawBeveledCube(frontBumper, {0.09f, 0.10f, 0.11f}, white_, {1, 1}, 46.0f);

    glm::mat4 rearBumper = glm::translate(parent, {0.0f, 0.34f, -1.98f});
    rearBumper = glm::scale(rearBumper, {1.38f, 0.14f, 0.10f});
    drawBeveledCube(rearBumper, {0.09f, 0.10f, 0.11f}, white_, {1, 1}, 46.0f);
}

void Scene::drawDriverCockpit(const Vehicle& vehicle)
{
    glm::mat4 parent(1.0f);
    parent = glm::translate(parent, vehicle.position);
    parent = glm::rotate(parent, glm::radians(vehicle.yawDegrees), {0.0f, 1.0f, 0.0f});

    glm::mat4 hood = glm::translate(parent, {0.0f, 0.69f, 1.20f});
    hood = glm::scale(hood, {1.82f, 0.24f, 1.58f});
    drawBeveledCube(hood, vehicle.color, white_, {1, 1}, 76.0f);

    glm::mat4 dashboard = glm::translate(parent, {0.0f, 0.96f, 0.70f});
    dashboard = glm::scale(dashboard, {1.86f, 0.11f, 0.24f});
    drawBeveledCube(dashboard, {0.035f, 0.040f, 0.048f}, white_, {1, 1}, 30.0f);

    for (float x : {-0.79f, 0.79f})
    {
        const float pillarX = x < 0.0f ? -0.94f : 0.94f;
        glm::mat4 pillar = glm::translate(parent, {pillarX, 1.31f, 0.88f});
        pillar = glm::rotate(pillar, glm::radians(x < 0.0f ? -10.0f : 10.0f), {0.0f, 0.0f, 1.0f});
        pillar = glm::scale(pillar, {0.07f, 0.62f, 0.08f});
        drawBeveledCube(pillar, vehicle.color * 0.58f, white_, {1, 1}, 48.0f);
    }

    glm::mat4 steeringWheel = glm::translate(parent, {0.43f, 0.99f, 0.73f});
    steeringWheel = glm::rotate(steeringWheel, glm::radians(90.0f), {1.0f, 0.0f, 0.0f});
    steeringWheel = glm::scale(steeringWheel, {0.28f, 0.05f, 0.28f});
    drawCylinder(steeringWheel, {0.025f, 0.028f, 0.032f}, 22.0f);

    glm::mat4 instrumentPanel = glm::translate(parent, {0.43f, 1.01f, 0.80f});
    instrumentPanel = glm::scale(instrumentPanel, {0.30f, 0.08f, 0.05f});
    drawBeveledCube(instrumentPanel, {0.08f, 0.18f, 0.24f}, white_, {1, 1}, 60.0f, {0.01f, 0.06f, 0.08f});
}
