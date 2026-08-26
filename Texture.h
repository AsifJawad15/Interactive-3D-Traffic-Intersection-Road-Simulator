#pragma once

#include <glad/glad.h>

#include <string>
#include <vector>

class Texture
{
public:
    Texture() = default;
    Texture(int width, int height, const std::vector<unsigned char>& rgb, bool repeat = true);
    ~Texture();

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    Texture(Texture&& other) noexcept;
    Texture& operator=(Texture&& other) noexcept;

    void bind(unsigned int unit = 0) const;

    static Texture fromFile(const std::string& path, bool repeat = true);
    static Texture makeWhite();
    static Texture makeAsphalt(int size = 128);
    static Texture makeGrass(int size = 128);
    static Texture makeSidewalk(int size = 128);
    static Texture makeFacade(int size = 128);

private:
    GLuint id_ = 0;

    static unsigned char clampByte(int value);
};
