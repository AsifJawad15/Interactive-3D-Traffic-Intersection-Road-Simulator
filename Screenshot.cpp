#include "Screenshot.h"

#include <glad/glad.h>

// stb_image_write calls sprintf in its HDR writer, which this project's SDL
// checks turn into an error. The PNG path used here never reaches that code.
#pragma warning(push)
#pragma warning(disable : 4996)
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#pragma warning(pop)

#include <vector>

bool saveFramebufferPng(const std::string& path, int width, int height)
{
    if (width <= 0 || height <= 0)
        return false;

    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

    // OpenGL's origin is the bottom-left corner; image files start at the top.
    stbi_flip_vertically_on_write(1);
    return stbi_write_png(path.c_str(), width, height, 3, pixels.data(), width * 3) != 0;
}
