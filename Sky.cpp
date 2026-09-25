#include "Sky.h"

#include <glm/gtc/matrix_inverse.hpp>

Sky::Sky() : shader_("shaders/post.vert", "shaders/sky.frag")
{
    glGenVertexArrays(1, &emptyVao_);
}

Sky::~Sky()
{
    if (emptyVao_ != 0)
        glDeleteVertexArrays(1, &emptyVao_);
}

void Sky::render(
    const glm::mat4& view,
    const glm::mat4& projection,
    const glm::vec3& cameraPosition,
    const DayNight& dayNight,
    const Weather& weather,
    float elapsedSeconds)
{
    shader_.use();
    shader_.setMat4("uInverseViewProjection", glm::inverse(projection * view));
    shader_.setVec3("uViewPosition", cameraPosition);
    shader_.setVec3("uSunDiscColor", dayNight.sunDiscColor());
    shader_.setVec3("uMoonVector", dayNight.moonVector());
    shader_.setVec3("uMoonColor", dayNight.moonColor());
    shader_.setFloat("uStarVisibility", dayNight.starVisibility());
    shader_.setFloat("uTime", elapsedSeconds);
    applyAtmosphereUniforms(shader_, dayNight);
    applyCloudUniforms(shader_, weather);
    shader_.setVec3("uCloudLight", dayNight.cloudLight());
    shader_.setVec3("uCloudLightVector", dayNight.cloudLightVector());
    shader_.setVec3("uCloudAmbient", dayNight.skyAmbient());
    shader_.setFloat("uCloudDarkness", weather.overcast());

    // The sky is the backdrop: it neither tests nor writes depth, so the
    // scene drawn afterwards always covers it.
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glBindVertexArray(emptyVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void applyAtmosphereUniforms(const Shader& shader, const DayNight& dayNight)
{
    shader.setVec3("uSunVector", dayNight.sunVector());
    shader.setVec3("uSkyZenith", dayNight.skyZenithColor());
    shader.setVec3("uSkyHorizon", dayNight.skyHorizonColor());
    shader.setVec3("uSunGlow", dayNight.sunGlowColor());
    shader.setFloat("uFogDensity", dayNight.fogDensity());
    shader.setFloat("uFogFalloff", dayNight.fogFalloff());
}

void applyCloudUniforms(const Shader& shader, const Weather& weather)
{
    shader.setFloat("uCloudCover", weather.cloudCover());
    shader.setVec2("uCloudOffset", weather.cloudOffset());
}
