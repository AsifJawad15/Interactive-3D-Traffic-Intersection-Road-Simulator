#include "LightManager.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <utility>

namespace
{
    // Beyond this, lamps only glow; they no longer light the ground.
    constexpr float lightingDistance = 90.0f;

    // The six planes of the view volume, from the view-projection matrix
    // (Gribb and Hartmann). A point p is inside plane (n, d) when n.p + d >= 0.
    std::array<glm::vec4, 6> frustumPlanes(const glm::mat4& m)
    {
        const glm::vec4 row0 {m[0][0], m[1][0], m[2][0], m[3][0]};
        const glm::vec4 row1 {m[0][1], m[1][1], m[2][1], m[3][1]};
        const glm::vec4 row2 {m[0][2], m[1][2], m[2][2], m[3][2]};
        const glm::vec4 row3 {m[0][3], m[1][3], m[2][3], m[3][3]};
        std::array<glm::vec4, 6> planes = {
            row3 + row0, row3 - row0, row3 + row1, row3 - row1, row3 + row2, row3 - row2
        };
        for (glm::vec4& plane : planes)
            plane /= glm::length(glm::vec3(plane));
        return planes;
    }
}

LightManager::LightManager()
{
    glGenBuffers(1, &buffer_);
    glBindBuffer(GL_UNIFORM_BUFFER, buffer_);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(Block), nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
    glBindBufferBase(GL_UNIFORM_BUFFER, bindingPoint, buffer_);
    candidates_.reserve(256);
}

LightManager::~LightManager()
{
    if (buffer_ != 0)
        glDeleteBuffers(1, &buffer_);
}

void LightManager::attach(GLuint program)
{
    const GLuint index = glGetUniformBlockIndex(program, "PointLightBlock");
    if (index != GL_INVALID_INDEX)
        glUniformBlockBinding(program, index, bindingPoint);
}

void LightManager::update(const glm::vec3& cameraPosition, const glm::mat4& viewProjection, bool lampsOn)
{
    Block block {};
    candidates_.clear();

    if (lampsOn)
    {
        const std::array<glm::vec4, 6> planes = frustumPlanes(viewProjection);
        for (std::size_t index = 0; index < lights_.size(); ++index)
        {
            const PointLight& light = lights_[index];
            const float distance = glm::length(light.position - cameraPosition);
            if (!light.alwaysOn && distance > lightingDistance + light.range)
                continue;

            // Dropped only when its whole sphere of reach is outside the view.
            bool visible = true;
            for (const glm::vec4& plane : planes)
            {
                if (glm::dot(glm::vec3(plane), light.position) + plane.w < -light.range)
                {
                    visible = false;
                    break;
                }
            }
            if (visible || light.alwaysOn)
                candidates_.push_back({index, light.alwaysOn ? -1.0f : distance});
        }
    }

    // Nearest first (the always-on lights sort to the front).
    std::sort(candidates_.begin(), candidates_.end(),
              [](const Candidate& a, const Candidate& b) { return a.distance < b.distance; });

    // The chosen set ends at `cutoff`: the lighting distance, or closer when
    // more lights are in range than fit. The last fifth of that distance is
    // a fade, so a light leaving the set has already faded to nothing.
    const std::size_t count = std::min<std::size_t>(candidates_.size(), maximumLights);
    float cutoff = lightingDistance;
    if (candidates_.size() > static_cast<std::size_t>(maximumLights))
        cutoff = std::min(cutoff, candidates_[maximumLights].distance);
    const float fadeStart = cutoff * 0.8f;

    int written = 0;
    for (std::size_t slot = 0; slot < count; ++slot)
    {
        const Candidate& candidate = candidates_[slot];
        const PointLight& light = lights_[candidate.light];
        float fade = 1.0f;
        if (!light.alwaysOn)
        {
            fade = 1.0f - glm::clamp((candidate.distance - fadeStart) / std::max(cutoff - fadeStart, 0.001f), 0.0f, 1.0f);
            if (fade <= 0.0f)
                continue;
        }
        block.positionRange[written][0] = light.position.x;
        block.positionRange[written][1] = light.position.y;
        block.positionRange[written][2] = light.position.z;
        block.positionRange[written][3] = light.range;
        block.colorFade[written][0] = light.color.r * fade;
        block.colorFade[written][1] = light.color.g * fade;
        block.colorFade[written][2] = light.color.b * fade;
        block.colorFade[written][3] = fade;
        ++written;
    }
    block.count[0] = written;
    activeCount_ = written;

    glBindBuffer(GL_UNIFORM_BUFFER, buffer_);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(Block), &block);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}
