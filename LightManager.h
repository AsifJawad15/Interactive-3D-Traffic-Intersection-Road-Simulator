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
                const glm::mat4& viewProjection, bool lampsOn, std::vector<Choice>& chosen,
                int maximum = maximumLights);

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

// A cone of light: a vehicle's headlights (both lamps as one cone).
struct SpotLight
{
    glm::vec3 position {0.0f};
    glm::vec3 direction {0.0f, 0.0f, 1.0f};   // the way it shines
    glm::vec3 color {1.0f};
    float range = 40.0f;
    float innerDegrees = 12.0f;
    float outerDegrees = 28.0f;
    bool alwaysOn = false;   // chosen first (your own car)
};

class LightManager
{
public:
    static constexpr int maximumLights = LightBudget::maximumLights;
    static constexpr int maximumSpots = 8;
    static constexpr GLuint bindingPoint = 0;
    static constexpr GLuint spotBindingPoint = 1;
    using PointLight = ::PointLight;

    LightManager();
    ~LightManager();

    LightManager(const LightManager&) = delete;
    LightManager& operator=(const LightManager&) = delete;

    void setLights(std::vector<PointLight> lights) { fixed_ = std::move(lights); }

    // Chooses this frame's lights and uploads them. `moving` are this frame's
    // lights that belong to no fixed lamp: the light bars of police cars and
    // ambulances. They compete for the same budget.
    void update(const glm::vec3& cameraPosition, const glm::mat4& viewProjection, bool lampsOn,
                const std::vector<PointLight>& moving = {});

    // This frame's headlights: the 8 nearest whose light reaches into view,
    // the farthest of them fading out as others come nearer.
    void updateSpots(const glm::vec3& cameraPosition, const glm::mat4& viewProjection,
                     const std::vector<SpotLight>& spots);

    // Connects a shader's "PointLightBlock" and "SpotLightBlock" to these buffers.
    static void attach(GLuint program);

    int activeCount() const { return activeCount_; }
    int activeSpotCount() const { return activeSpotCount_; }
    std::size_t totalCount() const { return fixed_.size(); }

private:
    // std140 layout, mirrored by lights.glsl.
    struct Block
    {
        float positionRange[maximumLights][4];
        float colorFade[maximumLights][4];
        int count[4];
    };
    struct SpotBlock
    {
        float positionRange[maximumSpots][4];
        float directionOuter[maximumSpots][4];
        float colorInner[maximumSpots][4];
        int count[4];
    };

    GLuint buffer_ = 0;
    GLuint spotBuffer_ = 0;
    std::vector<PointLight> spotReach_;   // a sphere round each cone, for choosing
    LightBudget spotBudget_;
    std::vector<LightBudget::Choice> chosenSpots_;
    int activeSpotCount_ = 0;
    std::vector<PointLight> fixed_;
    std::vector<PointLight> lights_;   // this frame's: the fixed ones, then the moving ones
    LightBudget budget_;
    std::vector<LightBudget::Choice> chosen_;
    int activeCount_ = 0;
};
