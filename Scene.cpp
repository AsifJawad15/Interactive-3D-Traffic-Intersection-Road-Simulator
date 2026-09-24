#include "Scene.h"

#include "MeshBuilder.h"
#include "Sky.h"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <stb_easy_font.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <string>
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

    struct EasyFontVertex
    {
        float x;
        float y;
        float z;
        unsigned char color[4];
    };

    // The pixel rectangles stb_easy_font draws a line of text with, in its
    // own units (a capital letter is about 7 units tall).
    struct Stroke
    {
        float x0, y0, x1, y1;
    };

    std::vector<Stroke> textStrokes(const std::string& text)
    {
        std::vector<char> buffer(text.begin(), text.end());
        buffer.push_back('\0');
        std::vector<unsigned char> vertices(64 * 1024);
        const int quads = stb_easy_font_print(0.0f, 0.0f, buffer.data(), nullptr,
                                              vertices.data(), static_cast<int>(vertices.size()));
        const auto* raw = reinterpret_cast<const EasyFontVertex*>(vertices.data());
        std::vector<Stroke> strokes;
        for (int quad = 0; quad < quads; ++quad)
        {
            const EasyFontVertex& a = raw[quad * 4];
            const EasyFontVertex& c = raw[quad * 4 + 2];
            strokes.push_back({std::min(a.x, c.x), std::min(a.y, c.y), std::max(a.x, c.x), std::max(a.y, c.y)});
        }
        return strokes;
    }

    // The printed face of a billboard: a colour gradient, a light frame and
    // two lines of text ("TOP|BOTTOM"), row 0 at the top of the picture.
    Texture makeBillboardFace(const std::string& text, int design)
    {
        constexpr int width = 512;
        constexpr int height = 256;
        struct Palette
        {
            glm::vec3 top, bottom, frame, big, small;
        };
        static const std::array<Palette, 6> palettes = {{
            {{0.05f, 0.16f, 0.36f}, {0.02f, 0.06f, 0.16f}, {0.95f, 0.78f, 0.12f}, {1.0f, 0.86f, 0.20f}, {0.90f, 0.94f, 1.0f}},
            {{0.70f, 0.10f, 0.08f}, {0.36f, 0.03f, 0.03f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.86f, 0.30f}},
            {{0.42f, 0.10f, 0.55f}, {0.12f, 0.03f, 0.22f}, {0.30f, 0.95f, 1.0f}, {0.30f, 0.95f, 1.0f}, {1.0f, 1.0f, 1.0f}},
            {{0.10f, 0.45f, 0.30f}, {0.03f, 0.18f, 0.12f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {0.75f, 1.0f, 0.80f}},
            {{0.95f, 0.55f, 0.08f}, {0.60f, 0.20f, 0.03f}, {0.20f, 0.10f, 0.05f}, {0.18f, 0.08f, 0.03f}, {1.0f, 1.0f, 1.0f}},
            {{0.95f, 0.95f, 0.92f}, {0.78f, 0.80f, 0.82f}, {0.80f, 0.10f, 0.08f}, {0.80f, 0.10f, 0.08f}, {0.10f, 0.12f, 0.16f}}
        }};
        const Palette& palette = palettes[static_cast<std::size_t>(design) % palettes.size()];

        std::vector<unsigned char> rgb(static_cast<std::size_t>(width) * height * 3);
        const auto put = [&rgb](int x, int y, glm::vec3 color)
        {
            if (x < 0 || y < 0 || x >= width || y >= height)
                return;
            unsigned char* pixel = &rgb[(static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)) * 3];
            for (int channel = 0; channel < 3; ++channel)
                pixel[channel] = static_cast<unsigned char>(glm::clamp(color[channel], 0.0f, 1.0f) * 255.0f + 0.5f);
        };
        for (int y = 0; y < height; ++y)
        {
            const glm::vec3 row = glm::mix(palette.top, palette.bottom, static_cast<float>(y) / (height - 1));
            for (int x = 0; x < width; ++x)
            {
                const bool frame = x < 10 || y < 10 || x >= width - 10 || y >= height - 10;
                put(x, y, frame ? palette.frame : row);
            }
        }

        const std::size_t split = text.find('|');
        const std::string lines[2] = {text.substr(0, split), split == std::string::npos ? "" : text.substr(split + 1)};
        const float scales[2] = {7.5f, 4.6f};
        const float tops[2] = {46.0f, 158.0f};
        const glm::vec3 colors[2] = {palette.big, palette.small};
        for (int line = 0; line < 2; ++line)
        {
            if (lines[line].empty())
                continue;
            const std::vector<Stroke> strokes = textStrokes(lines[line]);
            float right = 0.0f;
            for (const Stroke& stroke : strokes)
                right = std::max(right, stroke.x1);
            const float scale = std::min(scales[line], (width - 60.0f) / std::max(right, 1.0f));
            const float left = 0.5f * (width - right * scale);
            for (const Stroke& stroke : strokes)
            {
                for (int y = static_cast<int>(tops[line] + stroke.y0 * scale); y < static_cast<int>(tops[line] + stroke.y1 * scale); ++y)
                {
                    for (int x = static_cast<int>(left + stroke.x0 * scale); x < static_cast<int>(left + stroke.x1 * scale); ++x)
                        put(x, y, colors[line]);
                }
            }
        }
        return Texture(width, height, rgb, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, true);
    }

    // A unit square in the x-y plane facing +z, with the picture upright:
    // the top edge (y = +0.5) takes the first row of the image.
    Mesh makeFaceQuad()
    {
        const glm::vec3 normal {0.0f, 0.0f, 1.0f};
        const std::vector<Vertex> vertices = {
            {{-0.5f, -0.5f, 0.0f}, normal, {0.0f, 1.0f}},
            {{0.5f, -0.5f, 0.0f}, normal, {1.0f, 1.0f}},
            {{0.5f, 0.5f, 0.0f}, normal, {1.0f, 0.0f}},
            {{-0.5f, 0.5f, 0.0f}, normal, {0.0f, 0.0f}}
        };
        return Mesh(vertices, {0, 1, 2, 0, 2, 3});
    }

    // Model matrix of something standing at `ground`, turned to face along
    // the facing angle (heading convention: local +z points that way).
    glm::mat4 facingFrame(const glm::vec3& ground, float facingDegrees)
    {
        return glm::rotate(glm::translate(glm::mat4(1.0f), ground), glm::radians(facingDegrees), {0.0f, 1.0f, 0.0f});
    }

    // A small repeatable pseudo-random number in 0..1.
    float hash01(std::uint32_t value)
    {
        value ^= value >> 16;
        value *= 0x7feb352dU;
        value ^= value >> 15;
        value *= 0x846ca68bU;
        value ^= value >> 16;
        return static_cast<float>(value & 0xffffU) / 65535.0f;
    }
}

Scene::Scene(const TrafficSystem& traffic, const World& world)
    : world_(world),
      shader_("shaders/scene.vert", "shaders/scene.frag"),
      cube_(Mesh::makeCube()),
      beveledCube_(Mesh::makeBeveledCube(0.09f)),
      buildingMesh_(Mesh::makeBeveledCube(0.025f)),
      cylinder_(Mesh::makeCylinder(32)),
      faceQuad_(makeFaceQuad()),
      fountainBasin_(Mesh::makeBezierRevolution(fountainBasinProfile(), 22, 28)),
      fountainColumn_(Mesh::makeBezierRevolution(fountainColumnProfile(), 22, 24)),
      treeTrunk_(Mesh::makeBezierRevolution(treeTrunkProfile(), 12, 12)),
      treeCanopy_(Mesh::makeBezierRevolution(treeCanopyProfile(), 16, 16)),
      lampPost_(Mesh::makeBezierRevolution(lampPostProfile(), 12, 12)),
      white_(Texture::makeWhite()),
      matte_(Texture::makeGrey(20)),
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
      signFace_(Texture::makeSidewalk()),
      roads_(traffic)
{
    shader_.use();
    shader_.setInt("uDiffuseTexture", 0);
    shader_.setInt("uSpecularTexture", 1);
    LightManager::attach(shader_.id());
    buildStreetLamps(traffic.network());
    buildSigns();
    buildBusShelters();
}

void Scene::buildSigns()
{
    for (std::size_t design = 0; design < World::billboardText().size(); ++design)
        billboardFaces_.push_back(makeBillboardFace(World::billboardText()[design], static_cast<int>(design)));

    // Neon lettering: every stroke of the text becomes a thin glass tube.
    const MeshData tube = Mesh::beveledCubeData(0.12f);
    for (const NeonSign& sign : world_.neonSigns())
    {
        const std::vector<Stroke> strokes = textStrokes(sign.text);
        float right = 0.0f;
        float bottom = 0.0f;
        for (const Stroke& stroke : strokes)
        {
            right = std::max(right, stroke.x1);
            bottom = std::max(bottom, stroke.y1);
        }
        const float scale = sign.letterHeight / std::max(bottom, 1.0f);
        const glm::mat4 frame = facingFrame(sign.centre, sign.facingDegrees);

        MeshBuilder builder;
        for (const Stroke& stroke : strokes)
        {
            // Strokes are one unit thick; tubes a little thinner read better.
            float width = stroke.x1 - stroke.x0;
            float height = stroke.y1 - stroke.y0;
            if (width <= 1.01f)
                width *= 0.62f;
            if (height <= 1.01f)
                height *= 0.62f;
            // The sign faces local +z, so someone reading it from the street
            // has local +x on their right: the text runs that way.
            const float u = 0.5f * (stroke.x0 + stroke.x1) - 0.5f * right;
            const float v = 0.5f * bottom - 0.5f * (stroke.y0 + stroke.y1);
            glm::mat4 model = glm::translate(frame, {u * scale, v * scale, 0.0f});
            model = glm::scale(model, {width * scale, height * scale, 0.07f});
            builder.append(tube, model);
        }
        neonLetters_.push_back(builder.build());
    }
}

void Scene::buildStreetLamps(const RoadNetwork& network)
{
    // Every lamp in the city is baked into one mesh per material, so a
    // hundred-odd lamps cost three draw calls. The light each one casts goes
    // to the light budget, which picks the ones that matter each frame.
    MeshBuilder posts;
    MeshBuilder heads;
    MeshBuilder bulbs;
    const MeshData post = Mesh::bezierRevolutionData(lampPostProfile(), 12, 12);
    const MeshData box = Mesh::beveledCubeData(0.09f);

    std::vector<PointLight> lights;
    for (const StreetLamp& lamp : network.streetLamps())
    {
        posts.append(post, glm::translate(glm::mat4(1.0f), lamp.position));
        heads.append(box, transformed(lamp.position + glm::vec3(0.0f, 5.75f, 0.0f), {0.75f, 0.28f, 0.75f}));
        bulbs.append(box, transformed(lamp.position + glm::vec3(0.0f, 5.58f, 0.0f), {0.46f, 0.16f, 0.46f}));

        lights.push_back(PointLight::fromStreetLamp(lamp));
    }
    // The neon spill and the glow in front of the back-lit billboards share
    // the same budget.
    for (const PointLight& light : world_.signLights())
        lights.push_back(light);

    lampPosts_ = posts.build();
    lampHeads_ = heads.build();
    lampBulbs_ = bulbs.build();
    lights_.setLights(std::move(lights));
}

void Scene::render(
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

    // This frame's lights: the 32 that matter most of the street lamps, the
    // neon spill and the billboard glow, with the four Lab 3 lamps of the
    // central crossroads always among them.
    // At night the light bars of police cars and ambulances flash red and
    // blue on everything round them; they join the same budget.
    const bool night = dayNight.streetLampsOn();
    movingLights_.clear();
    if (night)
    {
        for (const VehiclePose& pose : vehicles)
        {
            PointLight light;
            if (pose.active && vehicleLooks_.lightBarLight(pose, elapsedSeconds, light) && light.color != glm::vec3(0.0f))
                movingLights_.push_back(light);
        }
    }
    lights_.update(cameraPosition, projection * view, night, movingLights_);

    // The Lab 3 spot light: the small lamp on the arm of the billboard at the
    // central crossroads, aimed at the middle of the picture. Its cut-off
    // angles are uploaded as cosines so the shader can test the cone with a
    // dot product.
    const SpotLamp& spot = world_.spotLamp();
    shader_.setVec3("uSpotPosition", spot.position);
    shader_.setVec3("uSpotDirection", glm::normalize(spot.target - spot.position));
    shader_.setVec3("uSpotColor", night ? glm::vec3{2.4f, 2.2f, 1.8f} : glm::vec3{0.0f});
    shader_.setFloat("uSpotCutOff", std::cos(glm::radians(spot.innerDegrees)));
    shader_.setFloat("uSpotOuterCutOff", std::cos(glm::radians(spot.outerDegrees)));

    // The ground runs out to two kilometres so it reaches the fog and the
    // horizon. The city and the lawn round it out to 900 m lie on top, so the
    // ground sits well below them: close to the road it could fight the
    // asphalt for depth when seen from high up.
    drawMesh(cube_, transformed({0.0f, -1.25f, 0.0f}, {2000.0f, 0.5f, 2000.0f}), {0.72f, 0.86f, 0.72f}, grass_,
             {780.0f, 780.0f}, 6.0f, glm::vec3{0.0f}, &matte_);
    drawRoads();

    const RoadNetwork& network = traffic.network();
    for (const Junction& junction : network.junctions())
    {
        if (junction.type != JunctionType::Roundabout)
            continue;
        drawIsland(junction.centre);
        if (junction.fountain)
            drawFountain(junction.centre);
    }

    drawBuildings();
    drawTrees();
    drawStreetFurniture();
    drawBusShelters(night);
    drawStreetLamps(night);
    drawBillboards(night);
    drawNeonSigns(night);
    drawSignals(traffic);
    drawGiveWaySigns();

    // Every vehicle in view. A vehicle whose whole body is outside the view
    // is skipped; the one you ride in shows only what its driver sees.
    const VehicleLamps lamps {night, elapsedSeconds};
    const glm::mat4 viewProjection = projection * view;
    vehicleParts_.clear();

    // The first frame (the hidden warm-up frame) also draws every part of
    // every kind once, far below the ground where nothing is seen, so the
    // driver has uploaded and prepared all of them before the first visible
    // frame.
    if (!vehiclesWarmed_)
    {
        vehiclesWarmed_ = true;
        for (std::size_t kind = 0; kind < vehicleKindCount; ++kind)
        {
            VehiclePose hidden;
            hidden.kind = static_cast<VehicleKind>(kind);
            hidden.position = {0.0f, -500.0f, 0.0f};
            hidden.doorOpen = 1.0f;
            vehicleLooks_.collect(hidden, {true, 0.0f}, vehicleParts_);
            vehicleLooks_.collectDriverView(hidden.kind, hidden.position, 0.0f, glm::vec3(1.0f), true, vehicleParts_);
        }
    }
    for (std::size_t index = 0; index < vehicles.size(); ++index)
    {
        const VehiclePose& pose = vehicles[index];
        if (!pose.active || (driverView && index == selectedVehicleIndex))
            continue;
        PointLight reach;
        reach.position = pose.position;
        reach.range = 0.5f * vehicleSpec(pose.kind).length + 1.5f;
        if (LightBudget::reachInView(reach, viewProjection))
            vehicleLooks_.collect(pose, lamps, vehicleParts_);
    }
    if (driverView && !vehicles.empty())
    {
        const VehiclePose& ridden = vehicles[selectedVehicleIndex % vehicles.size()];
        vehicleLooks_.collectDriverView(ridden.kind, ridden.position, ridden.yawDegrees, ridden.color, true, vehicleParts_);
    }

    // Your car, a yellow hatchback: from outside, or just its bonnet in the
    // driver view. You on foot are drawn unless you are looking through your
    // own eyes.
    const glm::vec3 playerColor {0.98f, 0.80f, 0.06f};
    if (playerDrawMode == PlayerDrawMode::DriverSeat)
    {
        vehicleLooks_.collectDriverView(VehicleKind::Hatchback, player.carPosition, player.carYawDegrees, playerColor,
                                        false, vehicleParts_);
    }
    else
    {
        VehiclePose car;
        car.active = true;
        car.kind = VehicleKind::Hatchback;
        car.position = player.carPosition;
        car.yawDegrees = player.carYawDegrees;
        car.wheelAngleDegrees = player.carWheelDegrees;
        car.steerAngleDegrees = player.carSteerDegrees;
        car.color = playerColor;
        // Brake lights while you brake, and while you sit in it standing still.
        car.braking = !player.walking && (std::abs(player.carSpeed) < 0.2f ||
                                          (player.carSpeed > 0.3f && player.longitudinalAcceleration < -1.0f));
        vehicleLooks_.collect(car, lamps, vehicleParts_);
    }
    drawVehicleParts();
    if (player.walking && playerDrawMode != PlayerDrawMode::OwnEyes)
        drawWalker(player);
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
    shader_.setFloat("uEmissiveTextured", emissiveTextured_);

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
    // The whole network is six static meshes. The asphalt photograph is very
    // dark, so it is tinted brighter: real worn asphalt reflects roughly 7-10 %
    // of the light, not the 4 % of the image. Texture coordinates were baked
    // in world metres, so the uv scale is 1.
    const glm::mat4 identity(1.0f);
    drawMesh(roads_.asphalt(), identity, {1.28f, 1.28f, 1.30f}, asphalt_, {1.0f, 1.0f}, 8.0f, glm::vec3{0.0f});
    drawMesh(roads_.kerbs(), identity, {0.70f, 0.70f, 0.72f}, sidewalk_, {1.0f, 1.0f}, 12.0f, glm::vec3{0.0f});
    drawMesh(roads_.sidewalks(), identity, {0.92f, 0.92f, 0.92f}, sidewalk_, {1.0f, 1.0f}, 12.0f, glm::vec3{0.0f});
    drawMesh(roads_.lawns(), identity, {0.60f, 0.80f, 0.55f}, grass_, {1.0f, 1.0f}, 6.0f, glm::vec3{0.0f}, &matte_);

    // Paint lies 12 mm above the asphalt; a polygon offset keeps it winning
    // the depth test even hundreds of metres away.
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.0f, -2.0f);
    drawMesh(roads_.whitePaint(), identity, {0.96f, 0.96f, 0.90f}, white_, {1.0f, 1.0f}, 4.0f, glm::vec3{0.0f});
    drawMesh(roads_.yellowPaint(), identity, {0.95f, 0.72f, 0.08f}, white_, {1.0f, 1.0f}, 4.0f, glm::vec3{0.0f});
    glDisable(GL_POLYGON_OFFSET_FILL);
}

void Scene::drawBuildings()
{
    for (const Building& building : world_.buildings())
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

void Scene::drawIsland(const glm::vec2& centre)
{
    const float radius = RoadNetwork::islandRadius;

    // The island stands only as proud of the carriageway as a real kerb does
    // - fifteen centimetres - so it reads as an island and not as raised road.
    const float lift = RoadNetwork::roadY;
    constexpr float kerbThickness = 0.30f;

    // Kerb first: wider than the island but with its top face LOWER, so the
    // grass disc that follows stands proud of it and the kerb is left showing
    // as a rim rather than as a lid over the grass.
    drawMesh(
        cylinder_,
        transformed({centre.x, lift, centre.y},
                    {radius * 2.0f + 0.6f, kerbThickness, radius * 2.0f + 0.6f}),
        {0.80f, 0.80f, 0.82f}, sidewalk_, {5.0f, 5.0f}, 16.0f, glm::vec3{0.0f});

    drawMesh(
        cylinder_,
        transformed({centre.x, lift + 0.04f, centre.y},
                    {radius * 2.0f, kerbThickness, radius * 2.0f}),
        {0.60f, 0.80f, 0.55f}, grass_, {4.0f, 4.0f}, 6.0f, glm::vec3{0.0f}, &matte_);
}

void Scene::drawFountain(const glm::vec2& centre)
{
    // Sits on the top face of the island's grass disc.
    const glm::vec3 origin {centre.x, RoadNetwork::roadY + 0.04f + 0.15f, centre.y};

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

    drawWaterJets(origin + glm::vec3(0.0f, 2.45f, 0.0f));
}

void Scene::drawWaterJets(const glm::vec3& origin)
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
                glm::vec3{0.22f, 0.34f, 0.42f});
        }
    }
}

void Scene::drawTrees()
{
    const std::vector<Tree>& trees = world_.trees();
    for (const Tree& tree : trees)
    {
        const glm::vec3 root {tree.position.x, 0.22f, tree.position.y};
        const float variation = tree.scale;
        const float twist = tree.twistDegrees;

        glm::mat4 trunk = glm::translate(glm::mat4(1.0f), root);
        trunk = glm::rotate(trunk, glm::radians(twist), {0.0f, 1.0f, 0.0f});
        trunk = glm::scale(trunk, {variation, variation, variation});
        drawMesh(treeTrunk_, trunk, {0.34f, 0.24f, 0.16f}, white_, {1.0f, 1.0f}, 12.0f, glm::vec3{0.0f});

        glm::mat4 canopy = glm::translate(glm::mat4(1.0f), root + glm::vec3(0.0f, 2.15f * variation, 0.0f));
        canopy = glm::rotate(canopy, glm::radians(twist * 1.7f), {0.0f, 1.0f, 0.0f});
        canopy = glm::scale(canopy, {variation, variation, variation});
        drawMesh(treeCanopy_, canopy, {0.20f, 0.44f, 0.20f}, grass_, {2.0f, 2.0f}, 8.0f, glm::vec3{0.0f}, &matte_);
    }
}

void Scene::drawStreetFurniture()
{
    // Roadside crates carrying the Lab 4 diffuse + specular pair. The specular
    // map is the metal frame of the container, so the painted panels stay flat
    // while the banding picks up the sun and the street lamps.
    for (const Crate& placed : world_.crates())
    {
        glm::mat4 crate = glm::translate(glm::mat4(1.0f), placed.position);
        crate = glm::rotate(crate, glm::radians(placed.yawDegrees), {0.0f, 1.0f, 0.0f});
        crate = glm::scale(crate, {1.05f, 1.05f, 1.05f});
        drawMesh(cube_, crate, {1.0f, 1.0f, 1.0f}, crateDiffuse_, {1.0f, 1.0f}, 64.0f,
                 glm::vec3{0.0f}, &crateSpecular_);
    }

    // Road signs: a Bezier post carrying a plate that faces oncoming traffic.
    for (const RoadSign& sign : world_.roadSigns())
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

void Scene::drawBillboards(bool illuminated)
{
    for (const Billboard& billboard : world_.billboards())
    {
        const glm::vec3 ground {billboard.centre.x, RoadNetwork::kerbTopY, billboard.centre.y};
        const glm::mat4 frame = facingFrame(ground, billboard.facingDegrees);
        const float middle = World::billboardBottom + 0.5f * World::billboardHeight;
        const float top = World::billboardBottom + World::billboardHeight;

        // Two steel posts, and the dark box the picture is mounted on.
        for (float side : {-1.0f, 1.0f})
        {
            const float x = side * (0.5f * World::billboardWidth - 0.4f);
            drawCylinder(glm::scale(glm::translate(frame, {x, 0.5f * top, -0.18f}), {0.24f, top, 0.24f}),
                         {0.22f, 0.23f, 0.25f}, 40.0f);
        }
        drawBeveledCube(glm::scale(glm::translate(frame, {0.0f, middle, -0.14f}),
                                   {World::billboardWidth + 0.3f, World::billboardHeight + 0.3f, 0.22f}),
                        {0.10f, 0.11f, 0.13f}, white_, {1.0f, 1.0f}, 20.0f);

        // The picture. Back-lit ones glow in their own colours at night; the
        // spot-lit one is lit only by its lamp, so the cone shows on it.
        const bool glows = illuminated && !billboard.spotLit;
        emissiveTextured_ = 1.0f;
        drawMesh(faceQuad_,
                 glm::scale(glm::translate(frame, {0.0f, middle, -0.02f}), {World::billboardWidth, World::billboardHeight, 1.0f}),
                 {0.92f, 0.92f, 0.92f}, billboardFaces_[static_cast<std::size_t>(billboard.design) % billboardFaces_.size()],
                 {1.0f, 1.0f}, 14.0f, glows ? glm::vec3{0.42f} : glm::vec3{0.0f}, &matte_);
        emissiveTextured_ = 0.0f;

        if (!billboard.spotLit)
            continue;

        // The lamp arm reaching out from the foot of the picture, and the
        // lamp at its end tilted up at it (Lab 3's spot light shines from
        // here).
        const SpotLamp& spot = world_.spotLamp();
        const float reach = glm::length(glm::vec2{spot.position.x, spot.position.z} - billboard.centre);
        const float armHeight = spot.position.y - 0.16f;
        drawBeveledCube(glm::scale(glm::translate(frame, {0.0f, armHeight, 0.5f * (reach - 0.14f)}),
                                   {0.10f, 0.10f, reach + 0.14f}),
                        {0.18f, 0.19f, 0.21f}, white_, {1.0f, 1.0f}, 40.0f);
        glm::mat4 head = glm::translate(frame, {0.0f, spot.position.y, reach});
        const float tilt = glm::degrees(std::atan2(spot.target.y - spot.position.y, reach));
        head = glm::rotate(head, glm::radians(180.0f), {0.0f, 1.0f, 0.0f});
        head = glm::rotate(head, glm::radians(-tilt), {1.0f, 0.0f, 0.0f});
        drawBeveledCube(glm::scale(head, {0.70f, 0.24f, 0.34f}), {0.14f, 0.15f, 0.17f}, white_, {1.0f, 1.0f}, 40.0f);
        drawBeveledCube(glm::scale(glm::translate(head, {0.0f, 0.10f, 0.02f}), {0.58f, 0.06f, 0.26f}),
                        illuminated ? glm::vec3{1.0f, 0.95f, 0.82f} : glm::vec3{0.30f, 0.29f, 0.26f},
                        white_, {1.0f, 1.0f}, 90.0f,
                        illuminated ? glm::vec3{0.85f, 0.78f, 0.58f} : glm::vec3{0.0f});
    }
}

void Scene::drawNeonSigns(bool illuminated)
{
    const std::vector<NeonSign>& signs = world_.neonSigns();
    for (std::size_t index = 0; index < signs.size() && index < neonLetters_.size(); ++index)
    {
        const NeonSign& sign = signs[index];

        // A dark rail behind the letters, fixed to the wall.
        const glm::mat4 frame = facingFrame(sign.centre, sign.facingDegrees);
        const float railWidth = sign.letterHeight * 0.95f * static_cast<float>(sign.text.size()) + 0.5f;
        drawBeveledCube(glm::scale(glm::translate(frame, {0.0f, 0.0f, -0.07f}), {railWidth, sign.letterHeight + 0.35f, 0.06f}),
                        {0.06f, 0.06f, 0.07f}, white_, {1.0f, 1.0f}, 30.0f);

        // By day the glass just shows its colour; at night it glows, and the
        // tired one stutters now and then.
        float strength = illuminated ? 1.0f : 0.0f;
        if (illuminated && sign.flickers)
        {
            const auto tick = static_cast<std::uint32_t>(elapsedSeconds_ * 14.0f);
            if (hash01(tick * 7u + static_cast<std::uint32_t>(index)) < 0.10f)
                strength = 0.12f;
        }
        drawMesh(neonLetters_[index], glm::mat4(1.0f), glm::mix(sign.color * 0.55f, glm::vec3{1.0f}, 0.25f * strength),
                 white_, {1.0f, 1.0f}, 90.0f, sign.color * (0.08f + 1.25f * strength));
    }
}

void Scene::drawStreetLamps(bool illuminated)
{
    // Tapered Bezier posts, housings and bulbs of every lamp in the city: one
    // draw call each. The bulbs glow (and bloom) even where the lamp is too
    // far away to be one of the lights that actually light the ground.
    const glm::mat4 identity(1.0f);
    drawMesh(lampPosts_, identity, {0.08f, 0.09f, 0.11f}, white_, {1.0f, 1.0f}, 34.0f, glm::vec3{0.0f});
    drawMesh(lampHeads_, identity, {0.12f, 0.13f, 0.16f}, white_, {1.0f, 1.0f}, 40.0f, glm::vec3{0.0f});
    drawMesh(lampBulbs_, identity,
             illuminated ? glm::vec3{1.0f, 0.72f, 0.30f} : glm::vec3{0.24f, 0.21f, 0.16f},
             white_, {1.0f, 1.0f}, 64.0f,
             illuminated ? glm::vec3{0.72f, 0.38f, 0.08f} : glm::vec3{0.0f});
}

void Scene::drawSignals(const TrafficSystem& traffic)
{
    // A signal head on the driver's right of every approach to a signalised
    // junction, just behind the stop line, facing the oncoming cars.
    for (const SignalHead& head : world_.signalHeads())
    {
        drawTrafficSignal({head.foot.x, RoadNetwork::kerbTopY, head.foot.y}, head.yawDegrees,
                          traffic.signalFor(head.junction, head.arm),
                          traffic.leftArrowFor(head.junction, head.arm) == SignalState::Green, true);
    }
}

void Scene::drawGiveWaySigns()
{
    // A red give-way sign at every approach that must yield: the entries of
    // the roundabouts and the side roads of the give-way T-junctions.
    for (const GiveWaySign& sign : world_.giveWaySigns())
    {
        glm::mat4 parent = glm::translate(glm::mat4(1.0f), {sign.foot.x, RoadNetwork::kerbTopY, sign.foot.y});
        parent = glm::rotate(parent, glm::radians(sign.yawDegrees), {0.0f, 1.0f, 0.0f});
        drawMesh(lampPost_, glm::scale(parent, {0.42f, 0.42f, 0.42f}),
                 {0.62f, 0.63f, 0.66f}, white_, {1.0f, 1.0f}, 48.0f, glm::vec3{0.0f});

        glm::mat4 plate = glm::translate(parent, {0.0f, 2.15f, 0.0f});
        plate = glm::rotate(plate, glm::radians(45.0f), {0.0f, 0.0f, 1.0f});
        drawBeveledCube(glm::scale(plate, {0.70f, 0.70f, 0.08f}), {0.86f, 0.12f, 0.10f}, white_, {1.0f, 1.0f}, 52.0f);
        glm::mat4 face = glm::translate(parent, {0.0f, 2.15f, -0.05f});
        face = glm::rotate(face, glm::radians(45.0f), {0.0f, 0.0f, 1.0f});
        drawBeveledCube(glm::scale(face, {0.46f, 0.46f, 0.04f}), {0.96f, 0.96f, 0.94f}, white_, {1.0f, 1.0f}, 60.0f);
    }
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

void Scene::drawVehicleParts()
{
    for (const VehiclePart& part : vehicleParts_)
        drawMesh(*part.mesh, part.model, part.color, white_, {1.0f, 1.0f}, part.shininess, part.emissive,
                 part.matte ? &matte_ : nullptr);
}

void Scene::buildBusShelters()
{
    // Every shelter in the city, baked into one mesh per material: a steel
    // frame, glass back and end, a roof, a bench, and the stop sign on its
    // pole at the kerb, lettered BUS on both faces.
    MeshBuilder frames;
    MeshBuilder glass;
    MeshBuilder roofs;
    MeshBuilder benches;
    MeshBuilder signs;
    MeshBuilder letters;
    const MeshData box = Mesh::beveledCubeData(0.06f);
    const float halfLength = 0.5f * World::shelterLength;
    const float halfDepth = 0.5f * World::shelterDepth;
    const float height = World::shelterHeight;

    for (const BusShelter& shelter : world_.busShelters())
    {
        const glm::mat4 frame = facingFrame({shelter.centre.x, RoadNetwork::kerbTopY, shelter.centre.y}, shelter.facingDegrees);
        const auto part = [&frame, &box](MeshBuilder& builder, glm::vec3 centre, glm::vec3 size)
        {
            builder.append(box, glm::scale(glm::translate(frame, centre), size));
        };
        for (float x : {-halfLength, halfLength})
        {
            for (float z : {-halfDepth + 0.05f, halfDepth - 0.05f})
                part(frames, {x, 0.5f * height, z}, {0.08f, height, 0.08f});
        }
        part(roofs, {0.0f, height + 0.05f, 0.0f}, {World::shelterLength + 0.3f, 0.10f, World::shelterDepth + 0.25f});
        part(glass, {0.0f, 0.2f + 0.5f * (height - 0.4f), -halfDepth + 0.05f}, {World::shelterLength - 0.1f, height - 0.4f, 0.03f});
        part(glass, {-halfLength, 0.2f + 0.5f * (height - 0.4f), -0.1f}, {0.03f, height - 0.4f, World::shelterDepth - 0.4f});
        // The advertising panel's case, at the other end (the picture is drawn per frame).
        part(frames, {halfLength, 1.35f, -0.1f}, {0.14f, 2.0f, World::shelterDepth - 0.3f});
        part(benches, {0.0f, 0.46f, -halfDepth + 0.35f}, {2.0f, 0.06f, 0.40f});
        for (float x : {-0.8f, 0.8f})
            part(frames, {x, 0.23f, -halfDepth + 0.35f}, {0.06f, 0.46f, 0.34f});

        // The stop sign, facing along the kerb so an arriving bus sees it.
        const glm::vec2 pole = World::shelterPole(shelter);
        const glm::mat4 signFrame = facingFrame({pole.x, RoadNetwork::kerbTopY, pole.y}, shelter.facingDegrees + 90.0f);
        letters.append(box, glm::scale(glm::translate(signFrame, {0.0f, 1.3f, 0.0f}), {0.08f, 2.6f, 0.08f}));
        signs.append(box, glm::scale(glm::translate(signFrame, {0.0f, 2.45f, 0.0f}), {0.62f, 0.50f, 0.05f}));
        const std::vector<Stroke> strokes = textStrokes("BUS");
        float right = 0.0f;
        for (const Stroke& stroke : strokes)
            right = std::max(right, stroke.x1);
        const float scale = 0.22f / 7.0f;
        for (int face = 0; face < 2; ++face)
        {
            const glm::mat4 plate = glm::rotate(glm::translate(signFrame, {0.0f, 2.45f, 0.0f}),
                                                glm::radians(face == 0 ? 0.0f : 180.0f), {0.0f, 1.0f, 0.0f});
            for (const Stroke& stroke : strokes)
            {
                const float u = (0.5f * (stroke.x0 + stroke.x1) - 0.5f * right) * scale;
                const float v = (3.5f - 0.5f * (stroke.y0 + stroke.y1)) * scale;
                letters.append(box, glm::scale(glm::translate(plate, {u, v, 0.03f}),
                                               {(stroke.x1 - stroke.x0) * scale, (stroke.y1 - stroke.y0) * scale, 0.012f}));
            }
        }
    }

    shelterFrames_ = frames.build();
    shelterGlass_ = glass.build();
    shelterRoofs_ = roofs.build();
    shelterBenches_ = benches.build();
    shelterSigns_ = signs.build();
    shelterLetters_ = letters.build();
}

void Scene::drawBusShelters(bool illuminated)
{
    if (world_.busShelters().empty())
        return;
    const glm::mat4 identity(1.0f);
    drawMesh(shelterFrames_, identity, {0.30f, 0.32f, 0.35f}, white_, {1.0f, 1.0f}, 48.0f, glm::vec3{0.0f});
    drawMesh(shelterGlass_, identity, {0.10f, 0.14f, 0.17f}, white_, {1.0f, 1.0f}, 120.0f, glm::vec3{0.0f});
    drawMesh(shelterRoofs_, identity, {0.74f, 0.76f, 0.79f}, white_, {1.0f, 1.0f}, 40.0f, glm::vec3{0.0f});
    drawMesh(shelterBenches_, identity, {0.46f, 0.30f, 0.18f}, white_, {1.0f, 1.0f}, 20.0f, glm::vec3{0.0f});
    drawMesh(shelterSigns_, identity, {0.07f, 0.24f, 0.72f}, white_, {1.0f, 1.0f}, 50.0f, glm::vec3{0.0f});
    drawMesh(shelterLetters_, identity, {0.94f, 0.95f, 0.96f}, white_, {1.0f, 1.0f}, 50.0f, glm::vec3{0.0f});

    // The advertising pictures on both faces of the end panel, lit from
    // inside at night like the billboards.
    const float halfLength = 0.5f * World::shelterLength;
    for (const BusShelter& shelter : world_.busShelters())
    {
        const glm::mat4 frame = facingFrame({shelter.centre.x, RoadNetwork::kerbTopY, shelter.centre.y}, shelter.facingDegrees);
        for (float side : {-1.0f, 1.0f})
        {
            glm::mat4 face = glm::translate(frame, {halfLength + side * 0.075f, 1.35f, -0.1f});
            face = glm::rotate(face, glm::radians(side * 90.0f), {0.0f, 1.0f, 0.0f});
            emissiveTextured_ = 1.0f;
            drawMesh(faceQuad_, glm::scale(face, {World::shelterDepth - 0.45f, 1.8f, 1.0f}), {0.92f, 0.92f, 0.92f},
                     billboardFaces_[static_cast<std::size_t>(shelter.design) % billboardFaces_.size()],
                     {1.0f, 1.0f}, 14.0f, illuminated ? glm::vec3{0.45f} : glm::vec3{0.0f}, &matte_);
            emissiveTextured_ = 0.0f;
        }
    }
}

void Scene::drawWalker(const PlayerView& player)
{
    // You, seen from outside: a simple figure 1.8 m tall.
    glm::mat4 parent = glm::translate(glm::mat4(1.0f), player.walkerPosition);
    parent = glm::rotate(parent, glm::radians(player.walkerYawDegrees), {0.0f, 1.0f, 0.0f});
    for (float side : {-1.0f, 1.0f})
    {
        drawBeveledCube(glm::scale(glm::translate(parent, {side * 0.11f, 0.45f, 0.0f}), {0.16f, 0.90f, 0.20f}),
                        {0.10f, 0.12f, 0.20f}, white_, {1, 1}, 20.0f);
        drawBeveledCube(glm::scale(glm::translate(parent, {side * 0.29f, 1.18f, 0.0f}), {0.12f, 0.62f, 0.14f}),
                        {0.08f, 0.55f, 0.72f}, white_, {1, 1}, 20.0f);
    }
    drawBeveledCube(glm::scale(glm::translate(parent, {0.0f, 1.20f, 0.0f}), {0.46f, 0.66f, 0.26f}),
                    {0.08f, 0.55f, 0.72f}, white_, {1, 1}, 20.0f);
    drawBeveledCube(glm::scale(glm::translate(parent, {0.0f, 1.66f, 0.02f}), {0.22f, 0.24f, 0.22f}),
                    {0.86f, 0.66f, 0.52f}, white_, {1, 1}, 20.0f);
}
