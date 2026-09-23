#include "PostProcess.h"

#include <algorithm>

namespace
{
    // Up to six levels: half, quarter, ... of the screen. Stopping once a level
    // would be smaller than 8 pixels keeps tiny windows working.
    constexpr int maximumBloomLevels = 6;

    // How strongly the blurred light is added back, and where "bright" starts
    // (in exposed linear units, so 1.0 is roughly display white).
    constexpr float bloomStrength = 0.22f;
    constexpr float bloomThreshold = 1.0f;
    constexpr float bloomKnee = 0.5f;
}

PostProcess::PostProcess()
    : downsample_("shaders/post.vert", "shaders/bloom_down.frag"),
      upsample_("shaders/post.vert", "shaders/bloom_up.frag"),
      tonemap_("shaders/post.vert", "shaders/tonemap.frag")
{
    // Core profile requires a bound vertex array even when the vertex shader
    // generates its positions from gl_VertexID.
    glGenVertexArrays(1, &emptyVao_);
    glGenFramebuffers(1, &framebuffer_);

    // Stands in for the bloom texture when bloom is switched off.
    const float black[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    glGenTextures(1, &blackTexture_);
    glBindTexture(GL_TEXTURE_2D, blackTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 1, 1, 0, GL_RGBA, GL_FLOAT, black);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    downsample_.use();
    downsample_.setInt("uSource", 0);
    upsample_.use();
    upsample_.setInt("uSource", 0);
    tonemap_.use();
    tonemap_.setInt("uScene", 0);
    tonemap_.setInt("uBloom", 1);
}

PostProcess::~PostProcess()
{
    releaseLevels();
    if (blackTexture_ != 0)
        glDeleteTextures(1, &blackTexture_);
    if (framebuffer_ != 0)
        glDeleteFramebuffers(1, &framebuffer_);
    if (emptyVao_ != 0)
        glDeleteVertexArrays(1, &emptyVao_);
}

void PostProcess::render(GLuint hdrScene, int sceneWidth, int sceneHeight,
                         int outputWidth, int outputHeight, float exposure)
{
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDepthMask(GL_FALSE);

    if (bloomEnabled_)
        renderBloom(hdrScene, sceneWidth, sceneHeight, exposure);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, outputWidth, outputHeight);

    tonemap_.use();
    tonemap_.setFloat("uExposure", exposure);
    tonemap_.setFloat("uBloomStrength", bloomEnabled_ ? bloomStrength : 0.0f);
    tonemap_.setVec2("uSceneTexelSize",
        {1.0f / static_cast<float>(sceneWidth), 1.0f / static_cast<float>(sceneHeight)});
    tonemap_.setFloat("uSharpen", sceneWidth < outputWidth ? 0.35f : 0.0f);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, hdrScene);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D,
                  bloomEnabled_ && !levels_.empty() ? levels_.front().texture : blackTexture_);
    drawFullscreen();

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void PostProcess::renderBloom(GLuint hdrScene, int width, int height, float exposure)
{
    resizeLevels(width, height);
    if (levels_.empty())
        return;

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glActiveTexture(GL_TEXTURE0);

    // Down-sample: screen -> level 0 -> level 1 -> ...
    downsample_.use();
    downsample_.setFloat("uThreshold", bloomThreshold);
    downsample_.setFloat("uKnee", bloomKnee);
    downsample_.setFloat("uExposure", exposure);

    int sourceWidth = width;
    int sourceHeight = height;
    GLuint source = hdrScene;
    for (std::size_t index = 0; index < levels_.size(); ++index)
    {
        const BloomLevel& level = levels_[index];
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, level.texture, 0);
        glViewport(0, 0, level.width, level.height);

        downsample_.setInt("uPrefilter", index == 0 ? 1 : 0);
        downsample_.setVec2("uSourceTexelSize",
            {1.0f / static_cast<float>(sourceWidth), 1.0f / static_cast<float>(sourceHeight)});
        glBindTexture(GL_TEXTURE_2D, source);
        drawFullscreen();

        source = level.texture;
        sourceWidth = level.width;
        sourceHeight = level.height;
    }

    // Up-sample: each level is blurred and ADDED onto the next larger one.
    upsample_.use();
    upsample_.setFloat("uRadius", 1.0f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    for (std::size_t index = levels_.size() - 1; index > 0; --index)
    {
        const BloomLevel& from = levels_[index];
        const BloomLevel& to = levels_[index - 1];
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, to.texture, 0);
        glViewport(0, 0, to.width, to.height);

        upsample_.setVec2("uSourceTexelSize",
            {1.0f / static_cast<float>(from.width), 1.0f / static_cast<float>(from.height)});
        glBindTexture(GL_TEXTURE_2D, from.texture);
        drawFullscreen();
    }
    glDisable(GL_BLEND);
}

void PostProcess::resizeLevels(int width, int height)
{
    if (width == levelsWidth_ && height == levelsHeight_ && !levels_.empty())
        return;

    releaseLevels();
    levelsWidth_ = width;
    levelsHeight_ = height;

    int levelWidth = width / 2;
    int levelHeight = height / 2;
    for (int index = 0; index < maximumBloomLevels && levelWidth >= 8 && levelHeight >= 8; ++index)
    {
        BloomLevel level;
        level.width = levelWidth;
        level.height = levelHeight;
        glGenTextures(1, &level.texture);
        glBindTexture(GL_TEXTURE_2D, level.texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, levelWidth, levelHeight, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        levels_.push_back(level);

        levelWidth /= 2;
        levelHeight /= 2;
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

void PostProcess::releaseLevels()
{
    for (BloomLevel& level : levels_)
    {
        if (level.texture != 0)
            glDeleteTextures(1, &level.texture);
    }
    levels_.clear();
    levelsWidth_ = levelsHeight_ = 0;
}

void PostProcess::drawFullscreen() const
{
    glBindVertexArray(emptyVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}
