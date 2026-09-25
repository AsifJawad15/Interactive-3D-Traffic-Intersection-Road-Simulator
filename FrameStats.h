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

    // Brackets the shadow maps inside the frame, with two GPU timestamps
    // (a time-elapsed query cannot nest inside the frame's own). A frame
    // that draws no shadows calls neither, and the time falls to zero.
    void beginShadows();
    void endShadows();
    void noShadows() { smoothedShadowMs_ = 0.0f; }
    float shadowMs() const { return smoothedShadowMs_; }

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

    std::array<GLuint, queryCount * 2> shadowQueries_ {};
    std::array<bool, queryCount> shadowPending_ {};
    std::size_t shadowSlot_ = 0;
    float smoothedShadowMs_ = 0.0f;

    std::array<float, historySize> frameHistory_ {};
    std::size_t head_ = 0;
    float smoothedFrameMs_ = 0.0f;
    float smoothedGpuMs_ = 0.0f;
};

// GPU time of a few parts of the frame (Enhanced mode's passes), each
// bracketed by two timestamps and read a few frames later, like the frame's
// own time. A part that did not run this frame is skipped and reads zero.
class GpuSections
{
public:
    static constexpr std::size_t count = 5;

    GpuSections();
    ~GpuSections();

    GpuSections(const GpuSections&) = delete;
    GpuSections& operator=(const GpuSections&) = delete;

    // `frameMs` sets how quickly the reading follows (about half a second).
    void begin(std::size_t section, float frameMs);
    void end(std::size_t section);
    void skip(std::size_t section) { smoothed_[section] = 0.0f; }
    float ms(std::size_t section) const { return smoothed_[section]; }

private:
    static constexpr std::size_t slots = 4;
    std::array<GLuint, count * slots * 2> queries_ {};
    std::array<bool, count * slots> pending_ {};
    std::array<std::size_t, count> slot_ {};
    std::array<float, count> smoothed_ {};
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
