#include "Enhanced.h"

#include "DayNight.h"
#include "FrameStats.h"
#include "Framebuffer.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/matrix.hpp>

#include <algorithm>

namespace
{
    // How far the occlusion looks round a point, in metres, and how much of
    // the ambient light it may take away.
    constexpr float occlusionRadius = 0.9f;
    constexpr float occlusionStrength = 0.8f;
    // How dark a contact shadow is in full sun.
    constexpr float contactStrength = 0.7f;
    // How bright the sun shafts are.
    constexpr float shaftBrightness = 0.5f;

    // Texture units: the passes share one layout.
    constexpr int sceneUnit = 0;
    constexpr int depthUnit = 1;
    constexpr int occlusionUnit = 2;
    constexpr int reflectionUnit = 3;
    constexpr int shaftUnit = 4;

    void bindTexture(int unit, GLuint texture)
    {
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
        glBindTexture(GL_TEXTURE_2D, texture);
    }
}

Enhanced::Enhanced()
    : occlusion_("shaders/post.vert", "shaders/ssao.frag"),
      occlusionBlur_("shaders/post.vert", "shaders/ssao_blur.frag"),
      reflections_("shaders/post.vert", "shaders/ssr.frag"),
      shaftMask_("shaders/post.vert", "shaders/sunshafts_mask.frag"),
      shafts_("shaders/post.vert", "shaders/sunshafts.frag"),
      composite_("shaders/post.vert", "shaders/enhance_composite.frag")
{
    glGenVertexArrays(1, &emptyVao_);

    // Stands in for the shafts when there are none.
    const float black[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    glGenTextures(1, &blackTexture_);
    glBindTexture(GL_TEXTURE_2D, blackTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 1, 1, 0, GL_RGBA, GL_FLOAT, black);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    for (const Shader* shader : {&occlusion_, &reflections_, &shaftMask_, &composite_})
    {
        shader->use();
        shader->setInt("uScene", sceneUnit);
        shader->setInt("uDepth", depthUnit);
    }
    shafts_.use();
    shafts_.setInt("uMask", 0);
    occlusionBlur_.use();
    occlusionBlur_.setInt("uOcclusion", occlusionUnit);
    composite_.use();
    composite_.setInt("uOcclusion", occlusionUnit);
    composite_.setInt("uReflections", reflectionUnit);
    composite_.setInt("uShafts", shaftUnit);
}

Enhanced::~Enhanced()
{
    for (Target* target : {&occlusionTarget_, &smoothTarget_, &reflectionTarget_, &maskTarget_, &shaftTarget_, &compositeTarget_})
        releaseTarget(*target);
    if (blackTexture_ != 0)
        glDeleteTextures(1, &blackTexture_);
    if (emptyVao_ != 0)
        glDeleteVertexArrays(1, &emptyVao_);
}

GLuint Enhanced::render(const HdrTarget& scene, const glm::mat4& view, const glm::mat4& projection,
                        const DayNight& dayNight, GpuSections& timers, float frameMs)
{
    resize(scene.width(), scene.height());
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDepthMask(GL_FALSE);

    const glm::mat4 inverseProjection = glm::inverse(projection);
    const glm::vec2 fullSize {static_cast<float>(width_), static_cast<float>(height_)};
    const auto setView = [&](const Shader& shader)
    {
        shader.setMat4("uProjection", projection);
        shader.setMat4("uInverseProjection", inverseProjection);
        shader.setVec2("uFullSize", fullSize);
    };
    bindTexture(sceneUnit, scene.colorTexture());
    bindTexture(depthUnit, scene.depthTexture());

    // Direct light worth a contact shadow: the sun, less as it sinks or
    // hides behind cloud. The moon's is too faint.
    const float overcast = dayNight.overcast();
    const float sunlight = dayNight.moonlit() ? 0.0f : dayNight.daylightAmount() * (1.0f - overcast);
    const glm::vec3 towardsLight = glm::normalize(glm::mat3(view) * -dayNight.lightDirection());

    // 1. Ambient occlusion and contact shadows, at half resolution.
    timers.begin(occlusionSection, frameMs);
    bindTarget(occlusionTarget_);
    occlusion_.use();
    setView(occlusion_);
    occlusion_.setFloat("uAoRadius", occlusionRadius);
    occlusion_.setVec3("uLightView", towardsLight);
    occlusion_.setFloat("uContact", sunlight > 0.05f ? 1.0f : 0.0f);
    drawFullscreen();
    bindTarget(smoothTarget_);
    occlusionBlur_.use();
    bindTexture(occlusionUnit, occlusionTarget_.texture);
    drawFullscreen();
    timers.end(occlusionSection);

    // 2. Reflections, at half resolution.
    timers.begin(reflectionsSection, frameMs);
    bindTarget(reflectionTarget_);
    reflections_.use();
    setView(reflections_);
    drawFullscreen();
    timers.end(reflectionsSection);

    // 3. Sun shafts, at a quarter of the resolution, while a low sun is in
    //    or near the picture and no cloud sheet hides it.
    const glm::vec3 sun = dayNight.sunVector();
    const glm::vec4 sunSeen = view * glm::vec4(sun, 0.0f);
    float shaftStrength = 0.0f;
    glm::vec2 sunUv {0.5f};
    if (sunSeen.z < -0.05f && !dayNight.moonlit())
    {
        const glm::vec4 clip = projection * sunSeen;
        sunUv = glm::vec2(clip) / clip.w * 0.5f + 0.5f;
        const glm::vec2 fromCentre = glm::abs(sunUv - 0.5f);
        const float inView = 1.0f - glm::smoothstep(0.6f, 1.2f, std::max(fromCentre.x, fromCentre.y));
        const float low = (1.0f - glm::smoothstep(0.12f, 0.55f, sun.y)) * glm::smoothstep(-0.02f, 0.05f, sun.y);
        shaftStrength = low * inView * (1.0f - overcast);
    }
    shaftsShown_ = shaftStrength > 0.01f;
    if (shaftsShown_)
    {
        timers.begin(shaftsSection, frameMs);
        bindTarget(maskTarget_);
        shaftMask_.use();
        shaftMask_.setVec2("uSunUv", sunUv);
        shaftMask_.setFloat("uAspect", fullSize.x / fullSize.y);
        shaftMask_.setFloat("uExposure", dayNight.exposure());
        drawFullscreen();

        bindTarget(shaftTarget_);
        shafts_.use();
        shafts_.setVec2("uSunUv", sunUv);
        bindTexture(0, maskTarget_.texture);
        drawFullscreen();
        bindTexture(sceneUnit, scene.colorTexture());
        timers.end(shaftsSection);
    }
    else
    {
        timers.skip(shaftsSection);
    }

    // 4. Everything together, at full resolution. The shafts take the
    //    sunlight's colour; the mask measured them after exposure, the
    //    scene is before it.
    timers.begin(compositeSection, frameMs);
    bindTarget(compositeTarget_);
    composite_.use();
    setView(composite_);
    composite_.setVec2("uHalfSize", {static_cast<float>(smoothTarget_.width),
                                     static_cast<float>(smoothTarget_.height)});
    composite_.setFloat("uAmbientStrength", occlusionStrength);
    composite_.setFloat("uContactStrength", contactStrength * glm::clamp(sunlight * 1.5f, 0.0f, 1.0f));
    composite_.setFloat("uReflect", 1.0f);
    composite_.setFloat("uExposure", dayNight.exposure());
    const glm::vec3 light = dayNight.lightColor();
    const float lightPeak = std::max({light.r, light.g, light.b, 1e-3f});
    composite_.setVec3("uShaftColor", light / lightPeak * (shaftBrightness * shaftStrength /
                                                            std::max(dayNight.exposure(), 1e-3f)));
    bindTexture(occlusionUnit, smoothTarget_.texture);
    bindTexture(reflectionUnit, reflectionTarget_.texture);
    bindTexture(shaftUnit, shaftsShown_ ? shaftTarget_.texture : blackTexture_);
    drawFullscreen();
    timers.end(compositeSection);

    for (int unit : {shaftUnit, reflectionUnit, occlusionUnit, depthUnit, sceneUnit})
        bindTexture(unit, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    return compositeTarget_.texture;
}

void Enhanced::resize(int width, int height)
{
    if (width == width_ && height == height_ && compositeTarget_.texture != 0)
        return;
    width_ = width;
    height_ = height;
    const int halfWidth = std::max(1, (width + 1) / 2);
    const int halfHeight = std::max(1, (height + 1) / 2);
    makeTarget(occlusionTarget_, halfWidth, halfHeight);
    makeTarget(smoothTarget_, halfWidth, halfHeight);
    makeTarget(reflectionTarget_, halfWidth, halfHeight);
    makeTarget(maskTarget_, std::max(1, width / 4), std::max(1, height / 4));
    makeTarget(shaftTarget_, std::max(1, width / 4), std::max(1, height / 4));
    makeTarget(compositeTarget_, width, height);
}

void Enhanced::makeTarget(Target& target, int width, int height)
{
    releaseTarget(target);
    target.width = width;
    target.height = height;
    glGenTextures(1, &target.texture);
    glBindTexture(GL_TEXTURE_2D, target.texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &target.framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.texture, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Enhanced::releaseTarget(Target& target)
{
    if (target.framebuffer != 0)
        glDeleteFramebuffers(1, &target.framebuffer);
    if (target.texture != 0)
        glDeleteTextures(1, &target.texture);
    target = Target {};
}

void Enhanced::bindTarget(const Target& target)
{
    glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
    glViewport(0, 0, target.width, target.height);
}

void Enhanced::drawFullscreen() const
{
    glBindVertexArray(emptyVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}
