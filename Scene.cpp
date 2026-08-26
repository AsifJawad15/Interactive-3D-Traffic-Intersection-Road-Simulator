#include "Scene.h"

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cmath>

namespace
{
    glm::mat4 transformed(const glm::vec3& position, const glm::vec3& scale)
    {
        glm::mat4 model(1.0f);
        model = glm::translate(model, position);
        return glm::scale(model, scale);
    }
}

Scene::Scene()
    : shader_("shaders/scene.vert", "shaders/scene.frag"),
      cube_(Mesh::makeCube()),
      beveledCube_(Mesh::makeBeveledCube(0.09f)),
      buildingMesh_(Mesh::makeBeveledCube(0.025f)),
      carCabin_(Mesh::makeCarCabin()),
      cylinder_(Mesh::makeCylinder(32)),
      white_(Texture::makeWhite()),
      asphalt_(Texture::fromFile("assets/asphalt-photoreal.png")),
      grass_(Texture::makeGrass()),
      sidewalk_(Texture::makeSidewalk()),
      facade_(Texture::makeFacade())
{
    shader_.use();
    shader_.setInt("uDiffuseTexture", 0);
}

void Scene::render(
    const glm::mat4& view,
    const glm::mat4& projection,
    const glm::vec3& cameraPosition,
    const TrafficSystem& traffic,
    const DayNight& dayNight,
    int shadingMode,
    bool driverView,
    std::size_t selectedVehicleIndex)
{
    shader_.use();
    shader_.setMat4("uView", view);
    shader_.setMat4("uProjection", projection);
    shader_.setVec3("uViewPosition", cameraPosition);
    shader_.setVec3("uAmbient", dayNight.ambientLight());
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

    drawCube(transformed({0.0f, -0.30f, 0.0f}, {82.0f, 0.5f, 82.0f}), {0.72f, 0.86f, 0.72f}, grass_, {32.0f, 32.0f}, 6.0f);
    drawRoads();
    drawRoadMarkings();
    drawBuildings();

    for (const glm::vec3 position : std::array<glm::vec3, 4>{
             glm::vec3{-11.0f, 0.0f, -11.0f}, glm::vec3{11.0f, 0.0f, -11.0f},
             glm::vec3{-11.0f, 0.0f, 11.0f}, glm::vec3{11.0f, 0.0f, 11.0f}})
    {
        drawStreetLamp(position, dayNight.streetLampsOn());
    }

    drawTrafficSignal({-7.0f, 0.0f, -8.2f}, 0.0f, traffic.signalFor(Lane::Northbound));
    drawTrafficSignal({7.0f, 0.0f, 8.2f}, 180.0f, traffic.signalFor(Lane::Southbound));
    drawTrafficSignal({-8.2f, 0.0f, 7.0f}, 90.0f, traffic.signalFor(Lane::Eastbound));
    drawTrafficSignal({8.2f, 0.0f, -7.0f}, -90.0f, traffic.signalFor(Lane::Westbound));

    const auto& vehicles = traffic.vehicles();
    for (std::size_t index = 0; index < vehicles.size(); ++index)
    {
        if (!driverView || index != selectedVehicleIndex)
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
    const glm::vec3& emissive)
{
    shader_.setMat4("uModel", model);
    shader_.setMat3("uNormalMatrix", glm::inverseTranspose(glm::mat3(model)));
    shader_.setVec3("uBaseColor", color);
    shader_.setVec3("uEmissiveColor", emissive);
    shader_.setVec2("uUvScale", uvScale);
    shader_.setFloat("uShininess", shininess);
    texture.bind();
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
    drawCube(transformed({0.0f, 0.0f, 0.0f}, {12.0f, 0.12f, 80.0f}), {0.96f, 0.96f, 0.98f}, asphalt_, {4.0f, 28.0f}, 8.0f);
    drawCube(transformed({0.0f, 0.01f, 0.0f}, {80.0f, 0.12f, 12.0f}), {0.96f, 0.96f, 0.98f}, asphalt_, {28.0f, 4.0f}, 8.0f);

    const std::array<glm::vec3, 4> pavementCenters = {
        glm::vec3{-23.0f, 0.12f, -23.0f}, glm::vec3{23.0f, 0.12f, -23.0f},
        glm::vec3{-23.0f, 0.12f, 23.0f}, glm::vec3{23.0f, 0.12f, 23.0f}
    };
    for (const glm::vec3 center : pavementCenters)
        drawCube(transformed(center, {32.0f, 0.20f, 32.0f}), {0.92f, 0.92f, 0.92f}, sidewalk_, {8.0f, 8.0f}, 12.0f);
}

void Scene::drawRoadMarkings()
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

    drawCube(transformed({-5.7f, 0.09f, 0.0f}, {0.12f, 0.035f, 80.0f}), white, white_, {1, 1}, 4.0f);
    drawCube(transformed({5.7f, 0.09f, 0.0f}, {0.12f, 0.035f, 80.0f}), white, white_, {1, 1}, 4.0f);
    drawCube(transformed({0.0f, 0.10f, -5.7f}, {80.0f, 0.035f, 0.12f}), white, white_, {1, 1}, 4.0f);
    drawCube(transformed({0.0f, 0.10f, 5.7f}, {80.0f, 0.035f, 0.12f}), white, white_, {1, 1}, 4.0f);

    for (int stripe = -5; stripe <= 5; stripe += 2)
    {
        drawCube(transformed({static_cast<float>(stripe), 0.115f, -7.2f}, {0.75f, 0.035f, 2.2f}), white, white_, {1, 1}, 4.0f);
        drawCube(transformed({static_cast<float>(stripe), 0.115f, 7.2f}, {0.75f, 0.035f, 2.2f}), white, white_, {1, 1}, 4.0f);
        drawCube(transformed({-7.2f, 0.12f, static_cast<float>(stripe)}, {2.2f, 0.035f, 0.75f}), white, white_, {1, 1}, 4.0f);
        drawCube(transformed({7.2f, 0.12f, static_cast<float>(stripe)}, {2.2f, 0.035f, 0.75f}), white, white_, {1, 1}, 4.0f);
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

void Scene::drawStreetLamp(const glm::vec3& position, bool illuminated)
{
    glm::mat4 pole(1.0f);
    pole = glm::translate(pole, position + glm::vec3(0.0f, 2.8f, 0.0f));
    pole = glm::scale(pole, {0.16f, 5.6f, 0.16f});
    drawCylinder(pole, {0.08f, 0.09f, 0.11f}, 34.0f);

    drawBeveledCube(transformed(position + glm::vec3(0.0f, 5.75f, 0.0f), {0.75f, 0.28f, 0.75f}), {0.12f, 0.13f, 0.16f}, white_, {1, 1}, 40.0f);
    drawBeveledCube(
        transformed(position + glm::vec3(0.0f, 5.58f, 0.0f), {0.46f, 0.16f, 0.46f}),
        illuminated ? glm::vec3{1.0f, 0.72f, 0.30f} : glm::vec3{0.24f, 0.21f, 0.16f},
        white_, {1, 1}, 64.0f,
        illuminated ? glm::vec3{0.72f, 0.38f, 0.08f} : glm::vec3{0.0f});
}

void Scene::drawTrafficSignal(const glm::vec3& position, float yawDegrees, SignalState state)
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
        const bool active = (index == 0 && state == SignalState::Red) ||
                            (index == 1 && state == SignalState::Yellow) ||
                            (index == 2 && state == SignalState::Green);
        const glm::vec3 color = active ? activeColors[static_cast<size_t>(index)] : inactiveColors[static_cast<size_t>(index)];
        const glm::vec3 emissive = active ? color * 0.62f : glm::vec3{0.0f};

        glm::mat4 lens = glm::translate(parent, {0.0f, 5.02f - index * 0.58f, -0.33f});
        lens = glm::rotate(lens, glm::radians(90.0f), {1.0f, 0.0f, 0.0f});
        lens = glm::scale(lens, {0.38f, 0.15f, 0.38f});
        drawCylinder(lens, color, 54.0f, emissive);
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
            glm::mat4 wheel = glm::translate(parent, {x, 0.42f, z});
            wheel = glm::rotate(wheel, glm::radians(90.0f), {0.0f, 0.0f, 1.0f});
            wheel = glm::rotate(wheel, glm::radians(vehicle.wheelAngleDegrees), {0.0f, 1.0f, 0.0f});
            wheel = glm::scale(wheel, {0.68f, 0.28f, 0.68f});
            drawCylinder(wheel, {0.025f, 0.028f, 0.03f}, 18.0f);

            glm::mat4 rim = glm::translate(parent, {x, 0.42f, z});
            rim = glm::rotate(rim, glm::radians(90.0f), {0.0f, 0.0f, 1.0f});
            rim = glm::rotate(rim, glm::radians(vehicle.wheelAngleDegrees), {0.0f, 1.0f, 0.0f});
            rim = glm::scale(rim, {0.39f, 0.31f, 0.39f});
            drawCylinder(rim, {0.58f, 0.61f, 0.64f}, 72.0f);
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
