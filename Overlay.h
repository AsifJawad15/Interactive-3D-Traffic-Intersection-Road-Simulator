#pragma once

#include "Shader.h"

#include <glad/glad.h>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <string>
#include <vector>

class Overlay
{
public:
    Overlay();
    ~Overlay();

    Overlay(const Overlay&) = delete;
    Overlay& operator=(const Overlay&) = delete;

    void render(
        int width,
        int height,
        float fps,
        bool paused,
        const std::string& cameraMode,
        const std::string& trafficPhase,
        const std::string& shadingMode,
        const std::string& timeText,
        bool automaticDayNight,
        bool lampsOn,
        std::size_t vehicleCount,
        bool showHelp);

private:
    Shader shader_;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;

    // Scratch space for stb_easy_font, allocated once instead of per string.
    std::vector<unsigned char> textBuffer_ = std::vector<unsigned char>(128 * 1024);

    void drawRectangle(float x, float y, float width, float height, const glm::vec4& color, int screenWidth, int screenHeight);
    void drawText(float x, float y, float scale, const std::string& text, const glm::vec4& color, int screenWidth, int screenHeight);
    void uploadAndDraw(const std::vector<glm::vec2>& vertices, const glm::vec4& color, int screenWidth, int screenHeight);
};
