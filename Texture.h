#pragma once

#include <glad/glad.h>

#include <string>
#include <vector>

class Texture
{
public:
    Texture() = default;

    // The wrapping and filtering modes are explicit parameters, mirroring the
    // Lab 4 helper
    //   loadTexture(path, wrapS, wrapT, minFilter, magFilter)
    // so the choice of GL_REPEAT versus GL_CLAMP_TO_EDGE and of a mipmapped
    // versus a plain minification filter is visible at every call site.
    Texture(
        int width,
        int height,
        const std::vector<unsigned char>& rgb,
        GLenum wrapS = GL_REPEAT,
        GLenum wrapT = GL_REPEAT,
        GLenum minFilter = GL_LINEAR_MIPMAP_LINEAR,
        GLenum magFilter = GL_LINEAR,
        bool srgb = false);
    ~Texture();

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    Texture(Texture&& other) noexcept;
    Texture& operator=(Texture&& other) noexcept;

    void bind(unsigned int unit = 0) const;

    // `srgb` marks a colour image. Its texels are stored gamma-encoded, so the
    // GPU converts them to linear light when sampling. Data images such as
    // specular maps stay linear.
    static Texture fromFile(
        const std::string& path,
        GLenum wrapS = GL_REPEAT,
        GLenum wrapT = GL_REPEAT,
        GLenum minFilter = GL_LINEAR_MIPMAP_LINEAR,
        GLenum magFilter = GL_LINEAR,
        bool srgb = false);

    // Loads an image if it is present and falls back to a generated texture
    // otherwise, so a missing asset never stops the program from running.
    static Texture fromFileOr(
        const std::string& path,
        Texture (*fallback)(int),
        GLenum wrapS = GL_REPEAT,
        GLenum wrapT = GL_REPEAT,
        GLenum minFilter = GL_LINEAR_MIPMAP_LINEAR,
        GLenum magFilter = GL_LINEAR,
        bool srgb = false);
    // A picture with an alpha channel (leaves, cut-outs), for alpha testing.
    // Its smaller mip levels are made here rather than by the driver: each
    // one is scaled so the share of texels that pass `alphaCutoff` stays the
    // same as in the full picture. Otherwise the averaging thins distant
    // leaves until trees go bald with distance.
    static Texture fromRgba(
        int width,
        int height,
        const std::vector<unsigned char>& rgba,
        float alphaCutoff = 0.5f,
        bool srgb = true);
    static Texture makeWhite();
    // A one-texel grey: as a specular map it scales the highlight down to
    // `level` / 255, for matte surfaces such as grass.
    static Texture makeGrey(unsigned char level);
    static Texture makeAsphalt(int size = 128);
    static Texture makeGrass(int size = 128);
    static Texture makeSidewalk(int size = 128);
    static Texture makeFacade(int size = 128);

private:
    GLuint id_ = 0;

    static unsigned char clampByte(int value);
};
