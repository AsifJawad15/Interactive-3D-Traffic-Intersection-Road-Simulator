#pragma once

#include <glad/glad.h>

#include <array>
#include <cstddef>

// Measures every frame: the wall-clock time between frames (what the eye sees
// as smooth or stuttering) and the GPU time of the frame's rendering, taken
// with OpenGL timer queries. The last 240 frames are kept for the HUD graph.
class FrameStats
{
public:
    static constexpr std::size_t historySize = 240;

    FrameStats();
    ~FrameStats();

    FrameStats(const FrameStats&) = delete;
    FrameStats& operator=(const FrameStats&) = delete;

    // Brackets the GL work of one frame. Results arrive a few frames later,
    // so reading them never makes the CPU wait for the GPU.
    void beginGpu();
    void endGpu();

    void recordFrame(float frameSeconds);

    float fps() const { return smoothedFrameMs_ > 0.0f ? 1000.0f / smoothedFrameMs_ : 0.0f; }
    float frameMs() const { return smoothedFrameMs_; }
    float gpuMs() const { return smoothedGpuMs_; }

    // Frame times in milliseconds, oldest first when read from `head()`.
    const std::array<float, historySize>& frameHistory() const { return frameHistory_; }
    std::size_t head() const { return head_; }

private:
    static constexpr std::size_t queryCount = 4;
    std::array<GLuint, queryCount> queries_ {};
    std::array<bool, queryCount> pending_ {};
    std::size_t querySlot_ = 0;

    std::array<float, historySize> frameHistory_ {};
    std::size_t head_ = 0;
    float smoothedFrameMs_ = 0.0f;
    float smoothedGpuMs_ = 0.0f;
};

// Chooses the resolution the scene is rendered at. The window always shows
// the full resolution; only the 3D image underneath may be rendered smaller
// (0.67 = 720p inside a 1080p window) and scaled up, so the HUD stays sharp.
enum class ResolutionMode
{
    Automatic,   // full resolution, dropping to 0.67 when the frame rate suffers
    Native,      // always full resolution
    Reduced      // always 0.67
};

class RenderScaler
{
public:
    static constexpr float reducedScale = 2.0f / 3.0f;

    void update(float frameSeconds, float frameMs, float gpuMs);
    void cycleMode();
    void setMode(ResolutionMode mode);

    float scale() const { return scale_; }
    ResolutionMode mode() const { return mode_; }
    const char* modeName() const;

private:
    ResolutionMode mode_ = ResolutionMode::Automatic;
    float scale_ = 1.0f;
    float slowSeconds_ = 0.0f;
    float fastSeconds_ = 0.0f;
};
