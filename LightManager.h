#pragma once

#include <glad/glad.h>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cstddef>
#include <vector>

struct StreetLamp;

// The simple light budget (plan section 3.4). The city has several hundred
// street lamps, but each frame only the 32 that matter most - near the camera
// and inside the view - are handed to the shaders, through one uniform buffer
// shared by every shader that lights surfaces.
//
// Nothing pops: every light's reach is limited by a smooth window, lights
// outside the view are only dropped when their whole reach is outside it,
// and the farthest lights of the chosen set fade out gradually before they
// are replaced. Lamps too far away to matter still glow (their bulbs are
// emissive and bloom); they just stop lighting the ground.

struct PointLight
{
    glm::vec3 position {0.0f};
    glm::vec3 color {1.0f};
    float range = 22.0f;     // beyond this the light is exactly zero
    bool alwaysOn = false;   // chosen before anything else (the Lab 3 lamps)

    // The light a street lamp casts, from the bulb under its head.
    static PointLight fromStreetLamp(const StreetLamp& lamp);
};

// Which lights the shaders get this frame, and how strongly. Pure arithmetic
// with no OpenGL, so --light-test can drive it along a camera path with no
// window and measure how smoothly lights come and go.
class LightBudget
{
public:
    static constexpr int maximumLights = 32;

    struct Choice
    {
        std::size_t light = 0;
        float fade = 1.0f;   // 0..1, multiplies the light's colour
    };

    void choose(const std::vector<PointLight>& lights, const glm::vec3& cameraPosition,
                const glm::mat4& viewProjection, bool lampsOn, std::vector<Choice>& chosen);

    // Whether any part of the light's reach is inside the view.
    static bool reachInView(const PointLight& light, const glm::mat4& viewProjection);

private:
    struct Candidate
    {
        std::size_t light = 0;
        float distance = 0.0f;
    };
    std::vector<Candidate> candidates_;
};

class LightManager
{
public:
    static constexpr int maximumLights = LightBudget::maximumLights;
    static constexpr GLuint bindingPoint = 0;
    using PointLight = ::PointLight;

    LightManager();
    ~LightManager();

    LightManager(const LightManager&) = delete;
    LightManager& operator=(const LightManager&) = delete;

    void setLights(std::vector<PointLight> lights) { lights_ = std::move(lights); }

    // Chooses this frame's lights and uploads them.
    void update(const glm::vec3& cameraPosition, const glm::mat4& viewProjection, bool lampsOn);

    // Connects a shader's "PointLightBlock" to this buffer.
    static void attach(GLuint program);

    int activeCount() const { return activeCount_; }
    std::size_t totalCount() const { return lights_.size(); }

private:
    // std140 layout, mirrored by lights.glsl.
    struct Block
    {
        float positionRange[maximumLights][4];
        float colorFade[maximumLights][4];
        int count[4];
    };

    GLuint buffer_ = 0;
    std::vector<PointLight> lights_;
    LightBudget budget_;
    std::vector<LightBudget::Choice> chosen_;
    int activeCount_ = 0;
};
