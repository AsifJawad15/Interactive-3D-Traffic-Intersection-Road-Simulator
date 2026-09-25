#pragma once

#include <glad/glad.h>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

// How many shadow maps are drawn (F2 cycles High -> Low -> Off).
enum class ShadowQuality
{
    High,   // the sharp map round the camera and the city map
    Low,    // the city map only
    Off
};

const char* shadowQualityName(ShadowQuality quality);

// An axis-aligned box in world space: everything that can cast a shadow.
struct ShadowBounds
{
    glm::vec3 low {-250.0f, -1.5f, -250.0f};
    glm::vec3 high {250.0f, 80.0f, 250.0f};
};

// Where the two shadow maps look, worked out each frame. Pure arithmetic,
// so --sun-test can check with no window that the near map never swims.
//
// Both maps look along the light:
//   near: a 64 m square round the camera (3 cm a texel at 2048 x 2048). It
//         moves with the camera in steps of one texel, so every texel always
//         covers the same patch of ground and shadow edges never crawl.
//   city: the whole city (about 12 cm a texel at 4096 x 4096). It only
//         changes when the light moves, and covers everything the near map
//         does not, out to the top view of the whole city.
class ShadowViews
{
public:
    static constexpr int nearSize = 2048;
    static constexpr int citySize = 4096;
    // Half the side of the near map's square, in metres.
    static constexpr float nearRadius = 32.0f;

    // `lightDirection` is the way the light travels. The near map is centred
    // ahead of the camera.
    void update(const glm::vec3& lightDirection, const glm::vec3& cameraPosition, const glm::vec3& cameraForward,
                const ShadowBounds& bounds);

    const glm::mat4& nearViewProjection() const { return nearViewProjection_; }
    const glm::mat4& cityViewProjection() const { return cityViewProjection_; }

    // Whether a sphere can cast into a map: inside its square seen from the
    // light (depth does not matter; the maps reach from the top of the city
    // to below the ground).
    bool nearReaches(const glm::vec3& centre, float radius) const;
    bool cityReaches(const glm::vec3& centre, float radius) const;

    // World -> 0..1 map coordinates and depth.
    glm::mat4 nearLookup() const;
    glm::mat4 cityLookup() const;
    // One texel in metres.
    static float nearTexelMetres() { return 2.0f * nearRadius / static_cast<float>(nearSize); }
    float cityTexelMetres() const { return cityTexelMetres_; }
    // Metres from depth 0 to depth 1 (both maps look through the same depth).
    float nearDepthRange() const { return depthRange_; }

private:
    glm::mat4 lightView_ {1.0f};
    glm::mat4 nearViewProjection_ {1.0f};
    glm::mat4 cityViewProjection_ {1.0f};
    glm::vec3 nearCentre_ {0.0f};    // in the light's view space
    glm::vec2 cityLow_ {0.0f};       // the city map's square, in the light's view space
    glm::vec2 cityHigh_ {0.0f};
    float cityTexelMetres_ = 0.12f;
    float depthRange_ = 100.0f;
};

// The sun's (or the moon's) view of the city, as depth maps. A point is in
// shadow when something stands between it and the light, which is when the
// map, seen from the light, holds a nearer depth than the point's own.
class ShadowMap
{
public:
    static constexpr int nearSize = ShadowViews::nearSize;
    static constexpr int citySize = ShadowViews::citySize;

    ShadowMap();
    ~ShadowMap();

    ShadowMap(const ShadowMap&) = delete;
    ShadowMap& operator=(const ShadowMap&) = delete;

    void update(const glm::vec3& lightDirection, const glm::vec3& cameraPosition, const glm::vec3& cameraForward,
                const ShadowBounds& bounds)
    {
        views_.update(lightDirection, cameraPosition, cameraForward, bounds);
    }
    const ShadowViews& views() const { return views_; }

    // Binds a map's framebuffer, clears it, and returns its view-projection.
    const glm::mat4& beginNear();
    const glm::mat4& beginCity();

    bool nearReaches(const glm::vec3& centre, float radius) const { return views_.nearReaches(centre, radius); }
    glm::mat4 nearLookup() const { return views_.nearLookup(); }
    glm::mat4 cityLookup() const { return views_.cityLookup(); }
    static float nearTexelMetres() { return ShadowViews::nearTexelMetres(); }
    float cityTexelMetres() const { return views_.cityTexelMetres(); }

    GLuint nearTexture() const { return nearDepth_; }
    GLuint cityTexture() const { return cityDepth_; }

private:
    GLuint nearFramebuffer_ = 0;
    GLuint cityFramebuffer_ = 0;
    GLuint nearDepth_ = 0;
    GLuint cityDepth_ = 0;
    ShadowViews views_;
};
