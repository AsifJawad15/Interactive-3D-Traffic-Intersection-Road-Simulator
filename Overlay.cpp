#include "Overlay.h"

#define STB_EASY_FONT_IMPLEMENTATION
#include <stb_easy_font.h>

#include <algorithm>
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

    vertices_.reserve(16 * 1024);
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
    bool paused,
    const std::string& cameraMode,
    const std::string& trafficPhase,
    const std::string& shadingMode,
    const std::string& timeText,
    bool automaticDayNight,
    bool lampsOn,
    std::size_t vehicleCount,
    std::size_t overlappingPairs,
    const PerformanceInfo& performance,
    bool showHelp)
{
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // The key list folds away with H so it does not cover the scene during a
    // demonstration; the status lines always stay.
    const float panelHeight = showHelp ? 602.0f : 173.0f;
    drawRectangle(14.0f, 14.0f, 470.0f, panelHeight, {0.015f, 0.025f, 0.045f, 0.90f}, width, height);
    drawRectangle(14.0f, 14.0f, 470.0f, 34.0f, {0.02f, 0.08f, 0.12f, 0.97f}, width, height);

    std::array<char, 128> line {};
    const float left = 26.0f;
    float y = 27.0f;
    drawText(left, y, 1.55f, "3D TRAFFIC INTERSECTION", {1.0f, 0.82f, 0.08f, 1.0f}, width, height);
    y += 34.0f;

    std::snprintf(line.data(), line.size(), "STATUS: %-7s   CARS: %zu   OVERLAPS: %zu   FPS: %.0f",
                  paused ? "PAUSED" : "RUNNING", vehicleCount, overlappingPairs, performance.fps);
    drawText(left, y, 1.24f, line.data(),
             paused ? glm::vec4{1.0f, 0.65f, 0.15f, 1.0f} : glm::vec4{0.25f, 1.0f, 0.40f, 1.0f}, width, height);
    y += 23.0f;

    std::snprintf(line.data(), line.size(), "CAMERA: %s   SIGNAL: %s", cameraMode.c_str(), trafficPhase.c_str());
    drawText(left, y, 1.20f, line.data(), {0.55f, 0.82f, 1.0f, 1.0f}, width, height);
    y += 21.0f;

    std::snprintf(line.data(), line.size(), "TIME: %s %s   LAMPS: %s   SHADE: %s",
                  timeText.c_str(), automaticDayNight ? "AUTO" : "MANUAL", lampsOn ? "ON" : "OFF",
                  shadingMode.c_str());
    drawText(left, y, 1.12f, line.data(), {0.82f, 0.70f, 1.0f, 1.0f}, width, height);
    y += 21.0f;

    std::snprintf(line.data(), line.size(), "RENDER: %dx%d (%d%% %s)   %d HZ %s   GPU %.1f MS",
                  performance.renderWidth, performance.renderHeight, performance.scalePercent,
                  performance.resolutionMode, performance.pacingHz,
                  performance.fullRatePacing ? "FULL" : "STEADY", performance.gpuMs);
    drawText(left, y, 1.08f, line.data(), {0.70f, 0.88f, 0.80f, 1.0f}, width, height);
    y += 21.0f;

    drawText(left, y, 1.13f, "AUTONOMOUS TRAFFIC - NO PLAYER CAR", {0.70f, 0.76f, 0.82f, 1.0f}, width, height);
    y += 21.0f;

    if (showHelp)
    {
        y += 8.0f;
        drawText(left, y, 1.28f, "INTERACTION OPTIONS", {1.0f, 0.82f, 0.08f, 1.0f}, width, height);
        y += 24.0f;

        static constexpr std::array<const char*, 20> controls = {
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
            "F5        FRAME-TIME GRAPH",
            "F6        RESOLUTION AUTO / NATIVE / 720P",
            "F7        FRAME PACING STEADY / FULL RATE",
            "F11       FULLSCREEN",
            "H         HIDE THIS PANEL",
            "ESC       EXIT"
        };

        for (const char* control : controls)
        {
            drawText(left, y, 1.12f, control, {0.96f, 0.96f, 0.96f, 1.0f}, width, height);
            y += 19.5f;
        }

        y += 6.0f;
        drawText(left, y, 1.08f, "OPENGL 3.3 CORE - REALTIME LIGHTING", {0.58f, 0.70f, 0.78f, 1.0f}, width, height);
    }
    else
    {
        drawText(left, y, 1.08f, "H   SHOW CONTROLS", {0.58f, 0.70f, 0.78f, 1.0f}, width, height);
    }

    if (performance.showGraph)
        drawFrameGraph(performance, width, height);

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void Overlay::drawFrameGraph(const PerformanceInfo& performance, int screenWidth, int screenHeight)
{
    if (performance.frameHistory == nullptr || performance.historySize == 0)
        return;

    // One bar per frame, bottom-left, scaled so the panel top is 50 ms.
    // Green: fast enough for 60 FPS. Yellow: under 25 ms. Red: a visible hitch.
    constexpr float barWidth = 2.0f;
    constexpr float graphHeight = 120.0f;
    constexpr float topMs = 50.0f;
    const float graphWidth = barWidth * static_cast<float>(performance.historySize);
    const float x0 = 14.0f;
    const float y0 = static_cast<float>(screenHeight) - graphHeight - 30.0f;

    drawRectangle(x0 - 6.0f, y0 - 22.0f, graphWidth + 12.0f, graphHeight + 40.0f,
                  {0.015f, 0.025f, 0.045f, 0.85f}, screenWidth, screenHeight);

    const std::array<glm::vec4, 3> colors = {
        glm::vec4{0.25f, 0.95f, 0.40f, 0.95f},
        glm::vec4{1.00f, 0.80f, 0.15f, 0.95f},
        glm::vec4{1.00f, 0.25f, 0.20f, 0.95f}
    };
    float worstMs = 0.0f;
    for (std::size_t band = 0; band < colors.size(); ++band)
    {
        vertices_.clear();
        for (std::size_t index = 0; index < performance.historySize; ++index)
        {
            const float ms = performance.frameHistory[(performance.historyHead + index) % performance.historySize];
            worstMs = std::max(worstMs, ms);
            const std::size_t frameBand = ms <= 1000.0f / 58.0f ? 0 : (ms <= 25.0f ? 1 : 2);
            if (frameBand != band || ms <= 0.0f)
                continue;
            const float barHeight = std::min(ms / topMs, 1.0f) * graphHeight;
            appendRectangle(x0 + barWidth * static_cast<float>(index), y0 + graphHeight - barHeight,
                            barWidth * 0.8f, barHeight);
        }
        drawVertices(colors[band], screenWidth, screenHeight);
    }

    // Reference lines at 16.7 ms (60 FPS) and 33.3 ms (30 FPS).
    vertices_.clear();
    for (float ms : {1000.0f / 60.0f, 1000.0f / 30.0f})
        appendRectangle(x0, y0 + graphHeight - ms / topMs * graphHeight, graphWidth, 1.0f);
    drawVertices({0.85f, 0.90f, 1.0f, 0.55f}, screenWidth, screenHeight);

    std::array<char, 96> label {};
    std::snprintf(label.data(), label.size(), "FRAME TIME (LAST %zu)   WORST %.1f MS   LINES 60 / 30 FPS",
                  performance.historySize, worstMs);
    drawText(x0, y0 - 16.0f, 1.0f, label.data(), {0.85f, 0.90f, 1.0f, 1.0f}, screenWidth, screenHeight);
}

void Overlay::appendRectangle(float x, float y, float width, float height)
{
    vertices_.insert(vertices_.end(), {
        {x, y}, {x + width, y}, {x + width, y + height},
        {x, y}, {x + width, y + height}, {x, y + height}
    });
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
    vertices_.clear();
    appendRectangle(x, y, width, height);
    drawVertices(color, screenWidth, screenHeight);
}

void Overlay::drawText(
    float x,
    float y,
    float scale,
    const char* text,
    const glm::vec4& color,
    int screenWidth,
    int screenHeight)
{
    const int quadCount = stb_easy_font_print(
        0.0f,
        0.0f,
        const_cast<char*>(text),
        nullptr,
        textBuffer_.data(),
        static_cast<int>(textBuffer_.size()));

    const auto* raw = reinterpret_cast<const EasyFontVertex*>(textBuffer_.data());
    vertices_.clear();

    constexpr int order[6] = {0, 1, 2, 0, 2, 3};
    for (int quad = 0; quad < quadCount; ++quad)
    {
        for (int index : order)
        {
            const EasyFontVertex& vertex = raw[quad * 4 + index];
            vertices_.emplace_back(x + vertex.x * scale, y + vertex.y * scale);
        }
    }

    drawVertices(color, screenWidth, screenHeight);
}

void Overlay::drawVertices(const glm::vec4& color, int screenWidth, int screenHeight)
{
    if (vertices_.empty())
        return;

    shader_.use();
    shader_.setVec2("uScreenSize", {static_cast<float>(screenWidth), static_cast<float>(screenHeight)});
    shader_.setVec4("uColor", color);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(vertices_.size() * sizeof(glm::vec2)),
        vertices_.data(),
        GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices_.size()));
    glBindVertexArray(0);
}
