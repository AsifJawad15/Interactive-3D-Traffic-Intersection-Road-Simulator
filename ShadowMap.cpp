#include "ShadowMap.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace
{
    // A depth texture the scene shader reads with a hardware depth test
    // (sampler2DShadow): linear filtering then averages the test over the
    // four nearest texels. Outside the map everything counts as lit.
    GLuint makeDepthTexture(int size)
    {
        GLuint texture = 0;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, size, size, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        const float farthest[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, farthest);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
        glBindTexture(GL_TEXTURE_2D, 0);
        return texture;
    }

    GLuint makeDepthFramebuffer(GLuint texture)
    {
        GLuint framebuffer = 0;
        glGenFramebuffers(1, &framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, texture, 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("Shadow map framebuffer is incomplete");
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return framebuffer;
    }

    // Maps -1..1 clip space to 0..1 texture space and depth.
    const glm::mat4 toTexture = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.5f)), glm::vec3(0.5f));

    // A little room before and behind the city in depth.
    constexpr float depthMargin = 5.0f;
}

const char* shadowQualityName(ShadowQuality quality)
{
    switch (quality)
    {
    case ShadowQuality::High: return "HIGH";
    case ShadowQuality::Low: return "LOW";
    default: return "OFF";
    }
}

ShadowMap::ShadowMap()
{
    nearDepth_ = makeDepthTexture(nearSize);
    cityDepth_ = makeDepthTexture(citySize);
    nearFramebuffer_ = makeDepthFramebuffer(nearDepth_);
    cityFramebuffer_ = makeDepthFramebuffer(cityDepth_);
}

ShadowMap::~ShadowMap()
{
    glDeleteFramebuffers(1, &nearFramebuffer_);
    glDeleteFramebuffers(1, &cityFramebuffer_);
    glDeleteTextures(1, &nearDepth_);
    glDeleteTextures(1, &cityDepth_);
}

void ShadowViews::update(const glm::vec3& lightDirection, const glm::vec3& cameraPosition,
                         const glm::vec3& cameraForward, const ShadowBounds& bounds)
{
    // The light's view: only a rotation, looking along the light. The sun
    // is never straight overhead (60 degrees at most), so "up" stays sound.
    const glm::vec3 along = glm::normalize(lightDirection);
    const glm::vec3 up = std::abs(along.y) > 0.99f ? glm::vec3 {0.0f, 0.0f, 1.0f} : glm::vec3 {0.0f, 1.0f, 0.0f};
    lightView_ = glm::lookAt(glm::vec3 {0.0f}, along, up);

    // The corners of everything that can cast, seen from the light. The view
    // looks down -z, so the nearest depth is the largest z.
    glm::vec3 low {1.0e9f};
    glm::vec3 high {-1.0e9f};
    for (int corner = 0; corner < 8; ++corner)
    {
        const glm::vec3 world {(corner & 1) != 0 ? bounds.high.x : bounds.low.x,
                               (corner & 2) != 0 ? bounds.high.y : bounds.low.y,
                               (corner & 4) != 0 ? bounds.high.z : bounds.low.z};
        const glm::vec3 seen = glm::vec3(lightView_ * glm::vec4(world, 1.0f));
        low = glm::min(low, seen);
        high = glm::max(high, seen);
    }
    const float zNear = -high.z - depthMargin;
    const float zFar = -low.z + depthMargin;
    depthRange_ = zFar - zNear;

    // The city map: the whole box.
    cityLow_ = {low.x, low.y};
    cityHigh_ = {high.x, high.y};
    cityTexelMetres_ = std::max(high.x - low.x, high.y - low.y) / static_cast<float>(citySize);
    cityViewProjection_ = glm::ortho(low.x, high.x, low.y, high.y, zNear, zFar) * lightView_;

    // The near map: a square round a point ahead of the camera (the whole
    // view out to 30 m fits in it), moved only in whole texels.
    const glm::vec3 centre = cameraPosition + glm::normalize(cameraForward) * (nearRadius - 2.0f);
    glm::vec3 seen = glm::vec3(lightView_ * glm::vec4(centre, 1.0f));
    const float texel = nearTexelMetres();
    seen.x = std::floor(seen.x / texel) * texel;
    seen.y = std::floor(seen.y / texel) * texel;
    nearCentre_ = seen;
    nearViewProjection_ = glm::ortho(seen.x - nearRadius, seen.x + nearRadius, seen.y - nearRadius,
                                     seen.y + nearRadius, zNear, zFar) * lightView_;
}

const glm::mat4& ShadowMap::beginNear()
{
    glBindFramebuffer(GL_FRAMEBUFFER, nearFramebuffer_);
    glViewport(0, 0, nearSize, nearSize);
    glClear(GL_DEPTH_BUFFER_BIT);
    return views_.nearViewProjection();
}

const glm::mat4& ShadowMap::beginCity()
{
    glBindFramebuffer(GL_FRAMEBUFFER, cityFramebuffer_);
    glViewport(0, 0, citySize, citySize);
    glClear(GL_DEPTH_BUFFER_BIT);
    return views_.cityViewProjection();
}

bool ShadowViews::nearReaches(const glm::vec3& centre, float radius) const
{
    const glm::vec3 seen = glm::vec3(lightView_ * glm::vec4(centre, 1.0f));
    return std::abs(seen.x - nearCentre_.x) < nearRadius + radius &&
           std::abs(seen.y - nearCentre_.y) < nearRadius + radius;
}

bool ShadowViews::cityReaches(const glm::vec3& centre, float radius) const
{
    const glm::vec3 seen = glm::vec3(lightView_ * glm::vec4(centre, 1.0f));
    return seen.x > cityLow_.x - radius && seen.x < cityHigh_.x + radius &&
           seen.y > cityLow_.y - radius && seen.y < cityHigh_.y + radius;
}

glm::mat4 ShadowViews::nearLookup() const
{
    return toTexture * nearViewProjection_;
}

glm::mat4 ShadowViews::cityLookup() const
{
    return toTexture * cityViewProjection_;
}
