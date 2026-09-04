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
        GLenum magFilter = GL_LINEAR);
    ~Texture();

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    Texture(Texture&& other) noexcept;
    Texture& operator=(Texture&& other) noexcept;

    void bind(unsigned int unit = 0) const;

    static Texture fromFile(
        const std::string& path,
        GLenum wrapS = GL_REPEAT,
        GLenum wrapT = GL_REPEAT,
        GLenum minFilter = GL_LINEAR_MIPMAP_LINEAR,
        GLenum magFilter = GL_LINEAR);

    // Loads an image if it is present and falls back to a generated texture
    // otherwise, so a missing asset never stops the program from running.
    static Texture fromFileOr(
        const std::string& path,
        Texture (*fallback)(int),
        GLenum wrapS = GL_REPEAT,
        GLenum wrapT = GL_REPEAT,
        GLenum minFilter = GL_LINEAR_MIPMAP_LINEAR,
        GLenum magFilter = GL_LINEAR);
    static Texture makeWhite();
    static Texture makeAsphalt(int size = 128);
    static Texture makeGrass(int size = 128);
    static Texture makeSidewalk(int size = 128);
    static Texture makeFacade(int size = 128);

private:
    GLuint id_ = 0;

    static unsigned char clampByte(int value);
};
