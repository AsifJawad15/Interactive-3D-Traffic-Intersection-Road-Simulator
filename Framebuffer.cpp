#include "Framebuffer.h"

#include <algorithm>
#include <stdexcept>

HdrTarget::~HdrTarget()
{
    release();
}

void HdrTarget::resize(int width, int height)
{
    width = std::max(width, 1);
    height = std::max(height, 1);
    if (width == width_ && height == height_ && msaaFramebuffer_ != 0)
        return;

    release();
    width_ = width;
    height_ = height;

    GLint maxSamples = 1;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    samples_ = std::clamp(4, 1, static_cast<int>(maxSamples));

    // Multisampled HDR colour and depth, rendered into by the scene.
    glGenRenderbuffers(1, &msaaColor_);
    glBindRenderbuffer(GL_RENDERBUFFER, msaaColor_);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples_, GL_RGBA16F, width_, height_);

    glGenRenderbuffers(1, &msaaDepth_);
    glBindRenderbuffer(GL_RENDERBUFFER, msaaDepth_);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples_, GL_DEPTH_COMPONENT24, width_, height_);

    glGenFramebuffers(1, &msaaFramebuffer_);
    glBindFramebuffer(GL_FRAMEBUFFER, msaaFramebuffer_);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msaaColor_);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, msaaDepth_);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("HDR multisample framebuffer is incomplete");

    // Single-sample textures that the resolve writes into and that
    // post-processing samples from.
    glGenTextures(1, &resolvedColor_);
    glBindTexture(GL_TEXTURE_2D, resolvedColor_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width_, height_, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenTextures(1, &resolvedDepth_);
    glBindTexture(GL_TEXTURE_2D, resolvedDepth_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width_, height_, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &resolveFramebuffer_);
    glBindFramebuffer(GL_FRAMEBUFFER, resolveFramebuffer_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resolvedColor_, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, resolvedDepth_, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("HDR resolve framebuffer is incomplete");

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void HdrTarget::bindForScene() const
{
    glBindFramebuffer(GL_FRAMEBUFFER, msaaFramebuffer_);
    glViewport(0, 0, width_, height_);
}

void HdrTarget::resolve() const
{
    glBindFramebuffer(GL_READ_FRAMEBUFFER, msaaFramebuffer_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveFramebuffer_);
    glBlitFramebuffer(0, 0, width_, height_, 0, 0, width_, height_, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBlitFramebuffer(0, 0, width_, height_, 0, 0, width_, height_, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void HdrTarget::release()
{
    if (msaaFramebuffer_ != 0)
        glDeleteFramebuffers(1, &msaaFramebuffer_);
    if (resolveFramebuffer_ != 0)
        glDeleteFramebuffers(1, &resolveFramebuffer_);
    if (msaaColor_ != 0)
        glDeleteRenderbuffers(1, &msaaColor_);
    if (msaaDepth_ != 0)
        glDeleteRenderbuffers(1, &msaaDepth_);
    if (resolvedColor_ != 0)
        glDeleteTextures(1, &resolvedColor_);
    if (resolvedDepth_ != 0)
        glDeleteTextures(1, &resolvedDepth_);

    msaaFramebuffer_ = resolveFramebuffer_ = 0;
    msaaColor_ = msaaDepth_ = 0;
    resolvedColor_ = resolvedDepth_ = 0;
    width_ = height_ = 0;
}
