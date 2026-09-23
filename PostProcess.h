#pragma once

#include "Shader.h"

#include <glad/glad.h>

#include <vector>

// Turns the HDR scene into the final image on screen:
//
//   1. Bloom: the bright parts are extracted and blurred through a chain of
//      progressively smaller textures (down-sample), then summed back up
//      (up-sample). Lamps, signal lenses and neon get a soft glow.
//   2. Tone mapping: exposure, the ACES curve and sRGB encoding.
class PostProcess
{
public:
    PostProcess();
    ~PostProcess();

    PostProcess(const PostProcess&) = delete;
    PostProcess& operator=(const PostProcess&) = delete;

    // Draws into the default framebuffer (the window). The scene may have been
    // rendered smaller than the window; it is then scaled up here, with a
    // light sharpening pass to win back some of the lost crispness.
    void render(GLuint hdrScene, int sceneWidth, int sceneHeight,
                int outputWidth, int outputHeight, float exposure);

    void setBloomEnabled(bool enabled) { bloomEnabled_ = enabled; }
    bool bloomEnabled() const { return bloomEnabled_; }

private:
    struct BloomLevel
    {
        GLuint texture = 0;
        int width = 0;
        int height = 0;
    };

    Shader downsample_;
    Shader upsample_;
    Shader tonemap_;
    GLuint emptyVao_ = 0;
    GLuint framebuffer_ = 0;
    GLuint blackTexture_ = 0;

    std::vector<BloomLevel> levels_;
    int levelsWidth_ = 0;
    int levelsHeight_ = 0;
    bool bloomEnabled_ = true;

    void resizeLevels(int width, int height);
    void releaseLevels();
    void renderBloom(GLuint hdrScene, int width, int height, float exposure);
    void drawFullscreen() const;
};
