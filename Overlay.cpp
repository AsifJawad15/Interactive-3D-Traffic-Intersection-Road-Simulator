#include "Overlay.h"

#define STB_EASY_FONT_IMPLEMENTATION
#include <stb_easy_font.h>

#include <array>
#include <cstdio>

namespace
{
    struct EasyFontVertex
    {
        float x;
        float y;
        float z;
        unsigned char color[4];
    };
}

Overlay::Overlay() : shader_("shaders/overlay.vert", "shaders/overlay.frag")
{
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec2), nullptr);
    glBindVertexArray(0);
}

Overlay::~Overlay()
{
    if (vbo_ != 0)
        glDeleteBuffers(1, &vbo_);
    if (vao_ != 0)
        glDeleteVertexArrays(1, &vao_);
}

void Overlay::render(
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
    bool showHelp)
{
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // The key list folds away with H so it does not cover the scene during a
    // demonstration; the status lines always stay.
    const float panelHeight = showHelp ? 500.0f : 152.0f;
    drawRectangle(14.0f, 14.0f, 455.0f, panelHeight, {0.015f, 0.025f, 0.045f, 0.90f}, width, height);
    drawRectangle(14.0f, 14.0f, 455.0f, 34.0f, {0.02f, 0.08f, 0.12f, 0.97f}, width, height);

    std::array<char, 96> statusText {};
    std::snprintf(
        statusText.data(), statusText.size(),
        "STATUS: %-7s   CARS: %zu   FPS: %.0f",
        paused ? "PAUSED" : "RUNNING", vehicleCount, fps);

    const float left = 26.0f;
    float line = 27.0f;
    drawText(left, line, 1.55f, "3D TRAFFIC INTERSECTION", {1.0f, 0.82f, 0.08f, 1.0f}, width, height);
    line += 34.0f;
    drawText(left, line, 1.24f, statusText.data(), paused ? glm::vec4{1.0f, 0.65f, 0.15f, 1.0f} : glm::vec4{0.25f, 1.0f, 0.40f, 1.0f}, width, height);
    line += 23.0f;
    drawText(left, line, 1.20f, "CAMERA: " + cameraMode + "   SIGNAL: " + trafficPhase, {0.55f, 0.82f, 1.0f, 1.0f}, width, height);
    line += 21.0f;
    drawText(
        left, line, 1.12f,
        "TIME: " + timeText + (automaticDayNight ? " AUTO" : " MANUAL") +
            "   LAMPS: " + (lampsOn ? "ON" : "OFF") + "   SHADE: " + shadingMode,
        {0.82f, 0.70f, 1.0f, 1.0f}, width, height);
    line += 21.0f;
    drawText(left, line, 1.13f, "AUTONOMOUS TRAFFIC - NO PLAYER CAR", {0.70f, 0.76f, 0.82f, 1.0f}, width, height);
    line += 21.0f;

    if (!showHelp)
    {
        drawText(left, line, 1.08f, "H   SHOW CONTROLS", {0.58f, 0.70f, 0.78f, 1.0f}, width, height);
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
        return;
    }

    line += 8.0f;
    drawText(left, line, 1.28f, "INTERACTION OPTIONS", {1.0f, 0.82f, 0.08f, 1.0f}, width, height);
    line += 24.0f;

    static const std::array<const char*, 16> controls = {
        "W A S D   MOVE FREE CAMERA",
        "Q / E     MOVE DOWN / UP",
        "MOUSE     LOOK AROUND",
        "C         FREE / TOP / FOLLOW / DRIVER",
        "V         DRIVER VIEW / FREE VIEW",
        "TAB       SELECT NEXT VIEW CAR",
        "M         SIGNALS / ROUNDABOUT MODE",
        "G         ADVANCE TRAFFIC SIGNAL",
        "P         PAUSE / RESUME",
        "1 / 2 / 3 FLAT / GOURAUD / PHONG",
        "T         TOGGLE AUTO DAY / NIGHT",
        "Y / N     SET DAY / NIGHT",
        "L         TOGGLE STREET LAMPS",
        "R         RESET CAMERA + TRAFFIC",
        "H         HIDE THIS PANEL",
        "ESC       EXIT"
    };

    for (const char* control : controls)
    {
        drawText(left, line, 1.12f, control, {0.96f, 0.96f, 0.96f, 1.0f}, width, height);
        line += 19.5f;
    }

    line += 6.0f;
    drawText(left, line, 1.08f, "OPENGL 3.3 CORE - REALTIME LIGHTING", {0.58f, 0.70f, 0.78f, 1.0f}, width, height);

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void Overlay::drawRectangle(
    float x,
    float y,
    float width,
    float height,
    const glm::vec4& color,
    int screenWidth,
    int screenHeight)
{
    const std::vector<glm::vec2> vertices = {
        {x, y}, {x + width, y}, {x + width, y + height},
        {x, y}, {x + width, y + height}, {x, y + height}
    };
    uploadAndDraw(vertices, color, screenWidth, screenHeight);
}

void Overlay::drawText(
    float x,
    float y,
    float scale,
    const std::string& text,
    const glm::vec4& color,
    int screenWidth,
    int screenHeight)
{
    std::vector<unsigned char> buffer(128 * 1024);
    const int quadCount = stb_easy_font_print(
        0.0f,
        0.0f,
        const_cast<char*>(text.c_str()),
        nullptr,
        buffer.data(),
        static_cast<int>(buffer.size()));

    const auto* raw = reinterpret_cast<const EasyFontVertex*>(buffer.data());
    std::vector<glm::vec2> triangles;
    triangles.reserve(static_cast<size_t>(quadCount) * 6);

    constexpr int order[6] = {0, 1, 2, 0, 2, 3};
    for (int quad = 0; quad < quadCount; ++quad)
    {
        for (int index : order)
        {
            const EasyFontVertex& vertex = raw[quad * 4 + index];
            triangles.emplace_back(x + vertex.x * scale, y + vertex.y * scale);
        }
    }

    uploadAndDraw(triangles, color, screenWidth, screenHeight);
}

void Overlay::uploadAndDraw(
    const std::vector<glm::vec2>& vertices,
    const glm::vec4& color,
    int screenWidth,
    int screenHeight)
{
    if (vertices.empty())
        return;

    shader_.use();
    shader_.setVec2("uScreenSize", {static_cast<float>(screenWidth), static_cast<float>(screenHeight)});
    shader_.setVec4("uColor", color);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(vertices.size() * sizeof(glm::vec2)),
        vertices.data(),
        GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
    glBindVertexArray(0);
}
