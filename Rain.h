#pragma once

#include "DayNight.h"
#include "Shader.h"
#include "Weather.h"

#include <glad/glad.h>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

// The falling rain: up to 5,000 streaks in a box round the camera, one
// instanced draw call. Drawn after the scene into the same target, tested
// against its depth (so walls and cars hide the drops behind them) and added
// onto it. How many drops fall follows how hard it rains.
class Rain
{
public:
    static constexpr int maximumDrops = 5000;

    Rain();
    ~Rain();

    Rain(const Rain&) = delete;
    Rain& operator=(const Rain&) = delete;

    // `pixelsHigh` is the height of the target, for keeping distant drops
    // at least a pixel wide.
    void render(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& cameraPosition,
                float seconds, const Weather& weather, const DayNight& dayNight, int pixelsHigh);

    // How many drops the last frame drew.
    int dropsDrawn() const { return dropsDrawn_; }

private:
    Shader shader_;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    int dropsDrawn_ = 0;
};
