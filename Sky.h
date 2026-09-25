#pragma once

#include "DayNight.h"
#include "Shader.h"
#include "Weather.h"

#include <glad/glad.h>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

// Draws the sky behind the scene: a gradient from the horizon to the zenith
// that follows the time of day, the sun disc and its glow, the moon and stars,
// and the weather's cloud layer in front of them.
class Sky
{
public:
    Sky();
    ~Sky();

    Sky(const Sky&) = delete;
    Sky& operator=(const Sky&) = delete;

    void render(
        const glm::mat4& view,
        const glm::mat4& projection,
        const glm::vec3& cameraPosition,
        const DayNight& dayNight,
        const Weather& weather,
        float elapsedSeconds);

private:
    Shader shader_;
    GLuint emptyVao_ = 0;
};

// Uploads the uniforms declared in shaders/atmosphere.glsl. Every shader that
// includes that file calls this, so the sky and the fog always agree.
void applyAtmosphereUniforms(const Shader& shader, const DayNight& dayNight);

// Uploads the cloud sheet's uniforms (shaders/clouds.glsl): the sky draws it,
// the scene is shaded by it.
void applyCloudUniforms(const Shader& shader, const Weather& weather);
