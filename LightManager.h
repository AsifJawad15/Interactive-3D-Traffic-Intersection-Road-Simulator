#pragma once

#include <glad/glad.h>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cstddef>
#include <vector>

// The simple light budget (plan section 3.4). The city has well over a
// hundred street lamps, but each frame only the 32 that matter most - near
// the camera and inside the view - are handed to the shaders, through one
// uniform buffer shared by every shader that lights surfaces.
//
// Nothing pops: every light's reach is limited by a smooth window, lights
// outside the view are only dropped when their whole reach is outside it,
// and the farthest lights of the chosen set fade out gradually before they
// are replaced. Lamps too far away to matter still glow (their bulbs are
// emissive and bloom); they just stop lighting the ground.
class LightManager
{
public:
    static constexpr int maximumLights = 32;
    static constexpr GLuint bindingPoint = 0;

    struct PointLight
    {
        glm::vec3 position {0.0f};
        glm::vec3 color {1.0f};
        float range = 22.0f;     // beyond this the light is exactly zero
        bool alwaysOn = false;   // chosen before anything else (the Lab 3 lamps)
    };

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
    struct Candidate
    {
        std::size_t light = 0;
        float distance = 0.0f;
    };

    // std140 layout, mirrored by lights.glsl.
    struct Block
    {
        float positionRange[maximumLights][4];
        float colorFade[maximumLights][4];
        int count[4];
    };

    GLuint buffer_ = 0;
    std::vector<PointLight> lights_;
    std::vector<Candidate> candidates_;
    int activeCount_ = 0;
};
