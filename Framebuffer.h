#pragma once

#include <glad/glad.h>

// The scene is rendered into a floating-point (HDR) target instead of the
// window. Light values above 1.0 survive, so bright lamps and neon can bloom,
// and the final tone-mapping pass decides how they are squeezed into the
// displayable range.
//
// Rendering happens into a multisampled target (4x MSAA keeps edges smooth),
// which is then resolved into ordinary textures that post-processing can read.
class HdrTarget
{
public:
    HdrTarget() = default;
    ~HdrTarget();

    HdrTarget(const HdrTarget&) = delete;
    HdrTarget& operator=(const HdrTarget&) = delete;

    // Creates or re-creates the buffers. Cheap to call every frame: it only
    // does work when the size actually changes.
    void resize(int width, int height);

    void bindForScene() const;

    // Averages the MSAA samples into colorTexture() and depthTexture().
    void resolve() const;

    GLuint colorTexture() const { return resolvedColor_; }
    GLuint depthTexture() const { return resolvedDepth_; }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    int width_ = 0;
    int height_ = 0;
    int samples_ = 4;

    GLuint msaaFramebuffer_ = 0;
    GLuint msaaColor_ = 0;
    GLuint msaaDepth_ = 0;

    GLuint resolveFramebuffer_ = 0;
    GLuint resolvedColor_ = 0;
    GLuint resolvedDepth_ = 0;

    void release();
};
