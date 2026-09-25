#pragma once

#include "Shader.h"

#include <glad/glad.h>
#include <glm/mat4x4.hpp>

#include <cstddef>

class DayNight;
class GpuSections;
class HdrTarget;

// Enhanced mode: the look of partial ray tracing, with no ray tracing
// hardware (OpenGL 3.3 has none). After the scene is resolved, passes march
// rays through its depth buffer instead of through the city:
//   reflections (ssr.frag): wet roads, puddles, car paint and glass mirror
//     the neon, the lamps and the lit windows;
//   ambient occlusion and contact shadows (ssao.frag, smoothed by
//     ssao_blur.frag): soft darkening where things meet, and the thin
//     shadows under tyres and feet;
//   sun shafts (sunshafts*.frag): beams from a low sun past buildings;
// and a composite pass (enhance_composite.frag) puts them together. The
// first two run at half resolution and the shafts at a quarter. The soft
// shadows (PCSS) are the fifth part; they happen in the scene's own shader
// (Scene, SceneFrame::softShadows).
class Enhanced
{
public:
    // The parts timed on the GPU (GpuSections), in the HUD's order. The
    // scene pass is timed too, for the soft shadows' share.
    static constexpr std::size_t reflectionsSection = 0;
    static constexpr std::size_t occlusionSection = 1;
    static constexpr std::size_t shaftsSection = 2;
    static constexpr std::size_t compositeSection = 3;
    static constexpr std::size_t sceneSection = 4;

    Enhanced();
    ~Enhanced();

    Enhanced(const Enhanced&) = delete;
    Enhanced& operator=(const Enhanced&) = delete;

    // Runs the passes on the resolved scene and returns the finished HDR
    // image, the size of the scene, for bloom and tone mapping.
    GLuint render(const HdrTarget& scene, const glm::mat4& view, const glm::mat4& projection,
                  const DayNight& dayNight, GpuSections& timers, float frameMs);

    // Whether the last frame drew sun shafts (a low sun in view).
    bool shaftsShown() const { return shaftsShown_; }

private:
    struct Target
    {
        GLuint texture = 0;
        GLuint framebuffer = 0;
        int width = 0;
        int height = 0;
    };

    Shader occlusion_;
    Shader occlusionBlur_;
    Shader reflections_;
    Shader shaftMask_;
    Shader shafts_;
    Shader composite_;
    GLuint emptyVao_ = 0;
    GLuint blackTexture_ = 0;

    Target occlusionTarget_;     // half
    Target smoothTarget_;        // half: the occlusion, smoothed
    Target reflectionTarget_;    // half
    Target maskTarget_;          // quarter
    Target shaftTarget_;         // quarter
    Target compositeTarget_;     // full
    int width_ = 0;
    int height_ = 0;
    bool shaftsShown_ = false;

    void resize(int width, int height);
    static void makeTarget(Target& target, int width, int height);
    static void releaseTarget(Target& target);
    static void bindTarget(const Target& target);
    void drawFullscreen() const;
};
