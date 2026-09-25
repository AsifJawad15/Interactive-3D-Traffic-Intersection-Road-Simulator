#include "FrameStats.h"

#include <algorithm>
#include <cmath>

namespace
{
    // Exponential smoothing over roughly half a second, whatever the frame rate.
    float smoothTowards(float current, float sample, float seconds)
    {
        if (current <= 0.0f)
            return sample;
        const float blend = 1.0f - std::exp(-seconds / 0.5f);
        return current + (sample - current) * blend;
    }
}

FrameStats::FrameStats()
{
    glGenQueries(static_cast<GLsizei>(queries_.size()), queries_.data());
    glGenQueries(static_cast<GLsizei>(shadowQueries_.size()), shadowQueries_.data());
}

FrameStats::~FrameStats()
{
    glDeleteQueries(static_cast<GLsizei>(queries_.size()), queries_.data());
    glDeleteQueries(static_cast<GLsizei>(shadowQueries_.size()), shadowQueries_.data());
}

void FrameStats::beginGpu()
{
    // Before reusing a query object, collect its result if it has arrived.
    // If it has not (a GPU several frames behind), the old value is dropped.
    if (pending_[querySlot_])
    {
        GLint available = GL_FALSE;
        glGetQueryObjectiv(queries_[querySlot_], GL_QUERY_RESULT_AVAILABLE, &available);
        if (available == GL_TRUE)
        {
            GLuint64 nanoseconds = 0;
            glGetQueryObjectui64v(queries_[querySlot_], GL_QUERY_RESULT, &nanoseconds);
            const float milliseconds = static_cast<float>(static_cast<double>(nanoseconds) * 1.0e-6);
            smoothedGpuMs_ = smoothTowards(smoothedGpuMs_, milliseconds, smoothedFrameMs_ * 0.001f);
        }
        pending_[querySlot_] = false;
    }
    glBeginQuery(GL_TIME_ELAPSED, queries_[querySlot_]);
}

void FrameStats::endGpu()
{
    glEndQuery(GL_TIME_ELAPSED);
    pending_[querySlot_] = true;
    querySlot_ = (querySlot_ + 1) % queries_.size();
}

void FrameStats::beginShadows()
{
    if (shadowPending_[shadowSlot_])
    {
        GLint available = GL_FALSE;
        glGetQueryObjectiv(shadowQueries_[shadowSlot_ * 2 + 1], GL_QUERY_RESULT_AVAILABLE, &available);
        if (available == GL_TRUE)
        {
            GLuint64 start = 0;
            GLuint64 end = 0;
            glGetQueryObjectui64v(shadowQueries_[shadowSlot_ * 2], GL_QUERY_RESULT, &start);
            glGetQueryObjectui64v(shadowQueries_[shadowSlot_ * 2 + 1], GL_QUERY_RESULT, &end);
            const float milliseconds = static_cast<float>(static_cast<double>(end - start) * 1.0e-6);
            smoothedShadowMs_ = smoothTowards(smoothedShadowMs_, milliseconds, smoothedFrameMs_ * 0.001f);
        }
        shadowPending_[shadowSlot_] = false;
    }
    glQueryCounter(shadowQueries_[shadowSlot_ * 2], GL_TIMESTAMP);
}

void FrameStats::endShadows()
{
    glQueryCounter(shadowQueries_[shadowSlot_ * 2 + 1], GL_TIMESTAMP);
    shadowPending_[shadowSlot_] = true;
    shadowSlot_ = (shadowSlot_ + 1) % shadowPending_.size();
}

void FrameStats::recordFrame(float frameSeconds)
{
    const float milliseconds = frameSeconds * 1000.0f;
    frameHistory_[head_] = milliseconds;
    head_ = (head_ + 1) % frameHistory_.size();
    smoothedFrameMs_ = smoothTowards(smoothedFrameMs_, milliseconds, frameSeconds);
}

// ---------------------------------------------------------------------------

void RenderScaler::update(float frameSeconds, float frameMs, float gpuMs)
{
    if (mode_ != ResolutionMode::Automatic)
        return;

    frameSeconds = std::clamp(frameSeconds, 0.0f, 0.25f);

    if (scale_ >= 1.0f)
    {
        // Below 55 FPS, or GPU work too heavy to keep 60, for 3 s: go to 720p.
        const bool struggling = frameMs > 1000.0f / 55.0f || gpuMs > 14.5f;
        slowSeconds_ = struggling ? slowSeconds_ + frameSeconds : 0.0f;
        if (slowSeconds_ > 3.0f)
        {
            scale_ = reducedScale;
            slowSeconds_ = 0.0f;
            fastSeconds_ = 0.0f;
        }
    }
    else
    {
        // Back to full resolution only when the GPU time, scaled up by the
        // pixel count, would still leave plenty of room, for 10 s in a row.
        const float predictedFullMs = gpuMs / (scale_ * scale_);
        const bool comfortable = frameMs < 1000.0f / 55.0f && predictedFullMs < 11.0f;
        fastSeconds_ = comfortable ? fastSeconds_ + frameSeconds : 0.0f;
        if (fastSeconds_ > 10.0f)
        {
            scale_ = 1.0f;
            slowSeconds_ = 0.0f;
            fastSeconds_ = 0.0f;
        }
    }
}

void RenderScaler::setMode(ResolutionMode mode)
{
    mode_ = mode;
    scale_ = mode == ResolutionMode::Reduced ? reducedScale : 1.0f;
    slowSeconds_ = 0.0f;
    fastSeconds_ = 0.0f;
}

void RenderScaler::cycleMode()
{
    switch (mode_)
    {
    case ResolutionMode::Automatic: setMode(ResolutionMode::Native); break;
    case ResolutionMode::Native: setMode(ResolutionMode::Reduced); break;
    case ResolutionMode::Reduced: setMode(ResolutionMode::Automatic); break;
    }
}

const char* RenderScaler::modeName() const
{
    switch (mode_)
    {
    case ResolutionMode::Automatic: return "AUTO";
    case ResolutionMode::Native: return "NATIVE";
    default: return "720P";
    }
}
