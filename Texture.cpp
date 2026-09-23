#include "Texture.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <utility>

namespace
{
    std::uint32_t hashPixel(int x, int y, std::uint32_t seed)
    {
        std::uint32_t value = static_cast<std::uint32_t>(x) * 374761393u;
        value += static_cast<std::uint32_t>(y) * 668265263u + seed * 2246822519u;
        value = (value ^ (value >> 13u)) * 1274126177u;
        return value ^ (value >> 16u);
    }

    void setPixel(std::vector<unsigned char>& pixels, int size, int x, int y, int r, int g, int b)
    {
        const size_t offset = static_cast<size_t>((y * size + x) * 3);
        pixels[offset] = static_cast<unsigned char>(std::clamp(r, 0, 255));
        pixels[offset + 1] = static_cast<unsigned char>(std::clamp(g, 0, 255));
        pixels[offset + 2] = static_cast<unsigned char>(std::clamp(b, 0, 255));
    }
}

Texture::Texture(
    int width,
    int height,
    const std::vector<unsigned char>& rgb,
    GLenum wrapS,
    GLenum wrapT,
    GLenum minFilter,
    GLenum magFilter,
    bool srgb)
{
    glGenTextures(1, &id_);
    glBindTexture(GL_TEXTURE_2D, id_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, srgb ? GL_SRGB8 : GL_RGB8, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
    glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(wrapS));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(wrapT));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(minFilter));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(magFilter));
}

Texture::~Texture()
{
    if (id_ != 0)
        glDeleteTextures(1, &id_);
}

Texture::Texture(Texture&& other) noexcept : id_(std::exchange(other.id_, 0))
{
}

Texture& Texture::operator=(Texture&& other) noexcept
{
    if (this != &other)
    {
        if (id_ != 0)
            glDeleteTextures(1, &id_);
        id_ = std::exchange(other.id_, 0);
    }
    return *this;
}

void Texture::bind(unsigned int unit) const
{
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, id_);
}

Texture Texture::fromFile(
    const std::string& path, GLenum wrapS, GLenum wrapT, GLenum minFilter, GLenum magFilter, bool srgb)
{
    namespace fs = std::filesystem;
    const fs::path requested(path);
    const fs::path current = fs::current_path();
    const std::vector<fs::path> candidates = {
        requested,
        current / requested,
        current / "OpenGLMiniProject" / requested,
        current / ".." / ".." / requested
    };

    for (const fs::path& candidate : candidates)
    {
        std::error_code error;
        const fs::path resolved = fs::weakly_canonical(candidate, error);
        if (error || !fs::is_regular_file(resolved, error))
            continue;

        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_set_flip_vertically_on_load(1);
        unsigned char* data = stbi_load(resolved.string().c_str(), &width, &height, &channels, STBI_rgb);
        if (data == nullptr)
            throw std::runtime_error("Could not decode texture: " + resolved.string());

        const size_t byteCount = static_cast<size_t>(width) * static_cast<size_t>(height) * 3;
        std::vector<unsigned char> rgb(data, data + byteCount);
        stbi_image_free(data);
        return Texture(width, height, rgb, wrapS, wrapT, minFilter, magFilter, srgb);
    }

    throw std::runtime_error("Texture file not found: " + path);
}

Texture Texture::fromFileOr(
    const std::string& path,
    Texture (*fallback)(int),
    GLenum wrapS,
    GLenum wrapT,
    GLenum minFilter,
    GLenum magFilter,
    bool srgb)
{
    try
    {
        return fromFile(path, wrapS, wrapT, minFilter, magFilter, srgb);
    }
    catch (const std::runtime_error&)
    {
        return fallback(128);
    }
}

Texture Texture::makeWhite()
{
    return Texture(1, 1, {255, 255, 255}, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, GL_NEAREST, GL_NEAREST);
}

Texture Texture::makeAsphalt(int size)
{
    std::vector<unsigned char> pixels(static_cast<size_t>(size * size * 3));
    for (int y = 0; y < size; ++y)
    {
        for (int x = 0; x < size; ++x)
        {
            const int noise = static_cast<int>(hashPixel(x, y, 17u) & 31u) - 15;
            const int fleck = ((hashPixel(x, y, 91u) & 255u) > 248u) ? 30 : 0;
            const int base = 48 + noise + fleck;
            setPixel(pixels, size, x, y, base, base + 1, base + 3);
        }
    }
    return Texture(size, size, pixels, GL_REPEAT, GL_REPEAT, GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, true);
}

Texture Texture::makeGrass(int size)
{
    std::vector<unsigned char> pixels(static_cast<size_t>(size * size * 3));
    for (int y = 0; y < size; ++y)
    {
        for (int x = 0; x < size; ++x)
        {
            const int noise = static_cast<int>(hashPixel(x, y, 31u) & 31u) - 15;
            const int stripe = ((x + y * 2) % 19 == 0) ? 12 : 0;
            setPixel(pixels, size, x, y, 43 + noise / 3, 105 + noise + stripe, 48 + noise / 2);
        }
    }
    return Texture(size, size, pixels, GL_REPEAT, GL_REPEAT, GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, true);
}

Texture Texture::makeSidewalk(int size)
{
    std::vector<unsigned char> pixels(static_cast<size_t>(size * size * 3));
    const int tile = std::max(8, size / 8);
    for (int y = 0; y < size; ++y)
    {
        for (int x = 0; x < size; ++x)
        {
            const bool grout = (x % tile < 2) || (y % tile < 2);
            const int checker = ((x / tile + y / tile) % 2) * 9;
            const int noise = static_cast<int>(hashPixel(x, y, 53u) & 7u) - 3;
            const int base = grout ? 112 : 165 + checker + noise;
            setPixel(pixels, size, x, y, base, base - 2, base - 5);
        }
    }
    return Texture(size, size, pixels, GL_REPEAT, GL_REPEAT, GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, true);
}

Texture Texture::makeFacade(int size)
{
    std::vector<unsigned char> pixels(static_cast<size_t>(size * size * 3));
    const int cellWidth = std::max(16, size / 4);
    const int cellHeight = std::max(16, size / 4);

    for (int y = 0; y < size; ++y)
    {
        for (int x = 0; x < size; ++x)
        {
            const int cellX = x % cellWidth;
            const int cellY = y % cellHeight;
            const bool mortar = (y % 8 < 2) || ((x + ((y / 8) % 2) * 8) % 16 < 2);
            const bool window = cellX > 6 && cellX < cellWidth - 6 && cellY > 5 && cellY < cellHeight - 7;
            const bool frame = window && (cellX < 9 || cellX > cellWidth - 10 || cellY < 8 || cellY > cellHeight - 10);

            if (frame)
                setPixel(pixels, size, x, y, 40, 46, 52);
            else if (window)
            {
                const int reflection = (x + y) % 11 < 3 ? 28 : 0;
                setPixel(pixels, size, x, y, 45 + reflection, 86 + reflection, 112 + reflection);
            }
            else if (mortar)
                setPixel(pixels, size, x, y, 110, 105, 98);
            else
            {
                const int noise = static_cast<int>(hashPixel(x, y, 79u) & 15u) - 7;
                setPixel(pixels, size, x, y, 142 + noise, 124 + noise, 105 + noise);
            }
        }
    }
    return Texture(size, size, pixels, GL_REPEAT, GL_REPEAT, GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, true);
}

unsigned char Texture::clampByte(int value)
{
    return static_cast<unsigned char>(std::clamp(value, 0, 255));
}
