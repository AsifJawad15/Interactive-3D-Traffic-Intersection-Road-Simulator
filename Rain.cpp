#include "Rain.h"

#include "LightManager.h"

#include <glm/vec4.hpp>

#include <cmath>
#include <cstdint>
#include <vector>

namespace
{
    // A fixed sequence, so the rain looks the same every run.
    float random01(std::uint32_t& state)
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float>(state & 0xffffffU) / static_cast<float>(0xffffffU);
    }

    constexpr float fallSpeed = 9.0f;        // m/s, a light-rain drop
    constexpr float streakSeconds = 0.035f;  // about a third of a metre
}

Rain::Rain() : shader_("shaders/rain.vert", "shaders/rain.frag")
{
    // Each drop: a place in the box and a random number. The order is
    // random too, so drawing only the first N of them thins the rain evenly.
    std::vector<glm::vec4> drops(maximumDrops);
    std::uint32_t state = 0x9e3779b9U;
    for (glm::vec4& drop : drops)
        drop = {random01(state), random01(state), random01(state), random01(state)};

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(drops.size() * sizeof(glm::vec4)), drops.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(glm::vec4), nullptr);
    glVertexAttribDivisor(0, 1);
    glBindVertexArray(0);

    LightManager::attach(shader_.id());
}

Rain::~Rain()
{
    if (vbo_ != 0)
        glDeleteBuffers(1, &vbo_);
    if (vao_ != 0)
        glDeleteVertexArrays(1, &vao_);
}

void Rain::render(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& cameraPosition,
                  float seconds, const Weather& weather, const DayNight& dayNight, int pixelsHigh)
{
    dropsDrawn_ = static_cast<int>(static_cast<float>(maximumDrops) * weather.rain());
    if (dropsDrawn_ <= 0)
        return;

    const glm::vec2 wind = weather.groundWind();
    shader_.use();
    shader_.setMat4("uViewProjection", projection * view);
    shader_.setVec3("uCameraPosition", cameraPosition);
    // Kept small, so the wrap-round sums stay precise after hours of running.
    shader_.setFloat("uTime", std::fmod(seconds, 600.0f));
    shader_.setVec3("uVelocity", {wind.x, -fallSpeed, wind.y});
    shader_.setFloat("uStreakSeconds", streakSeconds);
    shader_.setFloat("uPixelSize", 2.0f / (projection[1][1] * static_cast<float>(pixelsHigh > 0 ? pixelsHigh : 1)));
    shader_.setVec3("uAmbient", dayNight.skyAmbient());
    shader_.setFloat("uStrength", 0.9f);

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glDepthMask(GL_FALSE);
    glBindVertexArray(vao_);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, dropsDrawn_);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}
