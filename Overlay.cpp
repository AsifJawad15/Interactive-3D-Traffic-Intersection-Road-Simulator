#include "Overlay.h"

#define STB_EASY_FONT_IMPLEMENTATION
#include <stb_easy_font.h>

#include <glm/common.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
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
    bool showHelp,
    const HudExtras& extras)
{
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // The key list folds away with H so it does not cover the scene during a
    // demonstration; the status lines always stay.
    const float panelHeight = showHelp ? 704.0f : 215.0f;
    drawRectangle(14.0f, 14.0f, 470.0f, panelHeight, {0.015f, 0.025f, 0.045f, 0.90f}, width, height);
    drawRectangle(14.0f, 14.0f, 470.0f, 34.0f, {0.02f, 0.08f, 0.12f, 0.97f}, width, height);

    std::array<char, 128> line {};
    const float left = 26.0f;
    float y = 27.0f;
    drawText(left, y, 1.55f, "3D SMART TRAFFIC CITY", {1.0f, 0.82f, 0.08f, 1.0f}, width, height);
    y += 34.0f;

    std::snprintf(line.data(), line.size(), "STATUS: %-7s   CARS: %zu   OVERLAPS: %zu   FPS: %.0f",
                  paused ? "PAUSED" : "RUNNING", vehicleCount, overlappingPairs, performance.fps);
    drawText(left, y, 1.24f, line.data(),
             paused ? glm::vec4{1.0f, 0.65f, 0.15f, 1.0f} : glm::vec4{0.25f, 1.0f, 0.40f, 1.0f}, width, height);
    y += 23.0f;

    std::snprintf(line.data(), line.size(), "CAMERA: %s   SIGNAL: %s", cameraMode.c_str(), trafficPhase.c_str());
    drawText(left, y, 1.20f, line.data(), {0.55f, 0.82f, 1.0f, 1.0f}, width, height);
    y += 21.0f;

    std::snprintf(line.data(), line.size(), "PEOPLE: %zu   WAITING: %zu   CROSSING: %zu",
                  extras.people != nullptr ? extras.people->size() : 0u, extras.peopleWaiting, extras.peopleCrossing);
    drawText(left, y, 1.12f, line.data(), {0.55f, 0.95f, 0.80f, 1.0f}, width, height);
    y += 21.0f;

    std::snprintf(line.data(), line.size(), "TIME: %s %s %s   LAMPS: %s   SHADE: %s",
                  extras.periodName, timeText.c_str(), automaticDayNight ? "AUTO" : "MANUAL", lampsOn ? "ON" : "OFF",
                  shadingMode.c_str());
    drawText(left, y, 1.12f, line.data(), {0.82f, 0.70f, 1.0f, 1.0f}, width, height);
    y += 21.0f;

    std::snprintf(line.data(), line.size(), "RENDER: %dx%d (%d%% %s)   %d HZ %s   GPU %.1f MS   %d DRAWS",
                  performance.renderWidth, performance.renderHeight, performance.scalePercent,
                  performance.resolutionMode, performance.pacingHz,
                  performance.fullRatePacing ? "FULL" : "STEADY", performance.gpuMs, performance.drawCalls);
    drawText(left, y, 1.08f, line.data(), {0.70f, 0.88f, 0.80f, 1.0f}, width, height);
    y += 21.0f;

    if (extras.shadowMaps > 0)
        std::snprintf(line.data(), line.size(), "SHADOWS: %s (%d MAP%s, %s) %.1f MS   HEADLIGHTS LIT: %d",
                      extras.shadowQuality, extras.shadowMaps, extras.shadowMaps > 1 ? "S" : "",
                      extras.moonlight ? "MOON" : "SUN", extras.shadowMs, extras.headlights);
    else
        std::snprintf(line.data(), line.size(), "SHADOWS: %s   HEADLIGHTS LIT: %d", extras.shadowQuality,
                      extras.headlights);
    drawText(left, y, 1.08f, line.data(), {0.70f, 0.88f, 0.80f, 1.0f}, width, height);
    y += 21.0f;

    drawText(left, y, 1.13f, extras.onPlayer ? (extras.walking ? "YOU: ON FOOT" : "YOU: DRIVING")
                                             : "C: TAKE YOUR CAR", {0.70f, 0.76f, 0.82f, 1.0f}, width, height);
    y += 21.0f;

    if (showHelp)
    {
        y += 8.0f;
        drawText(left, y, 1.28f, "INTERACTION OPTIONS", {1.0f, 0.82f, 0.08f, 1.0f}, width, height);
        y += 24.0f;

        static constexpr std::array<const char*, 22> controls = {
            "W A S D   MOVE FREE CAMERA (SHIFT X4)",
            "Q / E     MOVE DOWN / UP",
            "MOUSE     LOOK (HOLD ALT FOR A CURSOR)",
            "C         FOLLOW YOUR CAR / LEAVE IT",
            "ARROWS    DRIVE OR WALK (OR W A S D)",
            "SPACE     HANDBRAKE   SHIFT: BOOST / RUN",
            "V         CHASE VIEW / DRIVER VIEW",
            "B         LOOK BACK (DRIVER VIEW)",
            "F         GET OUT / GET BACK IN",
            "M         TOP VIEW OF THE CITY",
            "TAB       FOLLOW AN AI CAR (V: ITS SEAT)",
            "SHIFT+TAB FOLLOW A PERSON (V: THEIR EYES)",
            "P / R     PAUSE / RESET EVERYTHING",
            "1 / 2 / 3 FLAT / GOURAUD / PHONG",
            "O  [  ]   NEXT TIME PRESET / HOUR -1, +1",
            "T / Y / N AUTO DAY / SET DAY / SET NIGHT",
            "K / L     WEATHER / TOGGLE LIGHTS",
            "F2        SHADOWS HIGH / LOW / OFF",
            "F5 F6 F7  GRAPH / RESOLUTION / PACING",
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
    drawMinimap(extras, width, height);
    drawPlayerPanel(extras, width, height);
    drawTimePanel(extras, timeText, width, height);
    drawWeatherPanel(extras, width, height);

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

void Overlay::drawMinimap(const HudExtras& extras, int screenWidth, int screenHeight)
{
    if (extras.roads == nullptr)
        return;

    // North up, and seen from above: the world's +x is on the left, exactly
    // as in the 3D top view, so turning left in the car turns left here.
    constexpr float size = 236.0f;
    constexpr float metresAcross = 470.0f;
    constexpr float scale = size / metresAcross;
    const float x0 = static_cast<float>(screenWidth) - size - 18.0f;
    const float y0 = static_cast<float>(screenHeight) - size - 18.0f;
    const float cx = x0 + 0.5f * size;
    const float cy = y0 + 0.5f * size;
    const auto toScreen = [cx, cy](glm::vec2 world) { return glm::vec2{cx - world.x * scale, cy - world.y * scale}; };

    drawRectangle(x0 - 6.0f, y0 - 24.0f, size + 12.0f, size + 30.0f, {0.015f, 0.025f, 0.045f, 0.85f}, screenWidth, screenHeight);
    drawText(x0, y0 - 17.0f, 1.0f, "CITY MAP   N UP", {0.85f, 0.90f, 1.0f, 1.0f}, screenWidth, screenHeight);

    // Roads: every piece runs along x or along z.
    vertices_.clear();
    constexpr float roadWidth = 5.0f;
    for (const glm::vec4& road : *extras.roads)
    {
        const glm::vec2 a = toScreen({road.x, road.y});
        const glm::vec2 b = toScreen({road.z, road.w});
        const glm::vec2 low = glm::min(a, b) - glm::vec2 {0.5f * roadWidth};
        const glm::vec2 high = glm::max(a, b) + glm::vec2 {0.5f * roadWidth};
        appendRectangle(low.x, low.y, high.x - low.x, high.y - low.y);
    }
    drawVertices({0.46f, 0.49f, 0.53f, 0.95f}, screenWidth, screenHeight);

    if (extras.roundabouts != nullptr)
    {
        vertices_.clear();
        for (const glm::vec2& centre : *extras.roundabouts)
        {
            const glm::vec2 p = toScreen(centre);
            appendRectangle(p.x - 8.0f, p.y - 8.0f, 16.0f, 16.0f);
        }
        drawVertices({0.46f, 0.49f, 0.53f, 0.95f}, screenWidth, screenHeight);
        vertices_.clear();
        for (const glm::vec2& centre : *extras.roundabouts)
        {
            const glm::vec2 p = toScreen(centre);
            appendRectangle(p.x - 3.5f, p.y - 3.5f, 7.0f, 7.0f);
        }
        drawVertices({0.25f, 0.55f, 0.28f, 1.0f}, screenWidth, screenHeight);
    }

    // Signals: a short bar along each axis in that axis' colour. North-south
    // runs up the map, east-west across it.
    if (extras.signals != nullptr && extras.northSouthColors != nullptr && extras.eastWestColors != nullptr)
    {
        for (std::size_t index = 0; index < extras.signals->size(); ++index)
        {
            const glm::vec2 p = toScreen((*extras.signals)[index]);
            const glm::vec3 ns = (*extras.northSouthColors)[index];
            const glm::vec3 ew = (*extras.eastWestColors)[index];
            drawRectangle(p.x - 1.5f, p.y - 6.0f, 3.0f, 12.0f, {ns, 1.0f}, screenWidth, screenHeight);
            drawRectangle(p.x - 6.0f, p.y - 1.5f, 12.0f, 3.0f, {ew, 1.0f}, screenWidth, screenHeight);
        }
    }

    if (extras.people != nullptr)
    {
        vertices_.clear();
        for (const glm::vec2& person : *extras.people)
        {
            const glm::vec2 p = toScreen(person);
            appendRectangle(p.x - 0.9f, p.y - 0.9f, 1.8f, 1.8f);
        }
        drawVertices({0.45f, 1.0f, 0.70f, 0.9f}, screenWidth, screenHeight);
    }
    if (extras.cars != nullptr)
    {
        vertices_.clear();
        for (const glm::vec2& car : *extras.cars)
        {
            const glm::vec2 p = toScreen(car);
            appendRectangle(p.x - 1.5f, p.y - 1.5f, 3.0f, 3.0f);
        }
        drawVertices({0.92f, 0.94f, 0.98f, 1.0f}, screenWidth, screenHeight);
    }
    if (extras.buses != nullptr)
    {
        vertices_.clear();
        for (const glm::vec2& bus : *extras.buses)
        {
            const glm::vec2 p = toScreen(bus);
            appendRectangle(p.x - 3.0f, p.y - 3.0f, 6.0f, 6.0f);
        }
        drawVertices({1.0f, 0.55f, 0.10f, 1.0f}, screenWidth, screenHeight);
    }
    if (extras.emergency != nullptr)
    {
        vertices_.clear();
        for (const glm::vec2& vehicle : *extras.emergency)
        {
            const glm::vec2 p = toScreen(vehicle);
            appendRectangle(p.x - 2.5f, p.y - 2.5f, 5.0f, 5.0f);
        }
        const bool red = std::fmod(extras.seconds, 0.8f) < 0.4f;
        drawVertices(red ? glm::vec4{1.0f, 0.2f, 0.15f, 1.0f} : glm::vec4{0.25f, 0.45f, 1.0f, 1.0f}, screenWidth, screenHeight);
    }

    // Your car: a yellow square with a line of dots pointing where it faces.
    const glm::vec2 car = toScreen(extras.playerCar);
    const float yaw = glm::radians(extras.playerCarYawDegrees);
    const glm::vec2 facing {-std::sin(yaw), -std::cos(yaw)};   // world (sin, cos) on the mirrored map
    vertices_.clear();
    appendRectangle(car.x - 3.5f, car.y - 3.5f, 7.0f, 7.0f);
    for (float step : {6.0f, 9.0f, 12.0f})
        appendRectangle(car.x + facing.x * step - 1.2f, car.y + facing.y * step - 1.2f, 2.4f, 2.4f);
    drawVertices({1.0f, 0.82f, 0.08f, 1.0f}, screenWidth, screenHeight);

    if (extras.walking)
    {
        const glm::vec2 walker = toScreen(extras.walker);
        drawRectangle(walker.x - 3.0f, walker.y - 3.0f, 6.0f, 6.0f, {0.25f, 0.95f, 1.0f, 1.0f}, screenWidth, screenHeight);
    }
}

void Overlay::drawPlayerPanel(const HudExtras& extras, int screenWidth, int screenHeight)
{
    std::array<char, 64> line {};
    const float centreX = 0.5f * static_cast<float>(screenWidth);

    if (extras.onPlayer)
    {
        const float x0 = centreX - 130.0f;
        const float y0 = static_cast<float>(screenHeight) - 92.0f;
        drawRectangle(x0, y0, 260.0f, 74.0f, {0.015f, 0.025f, 0.045f, 0.80f}, screenWidth, screenHeight);
        if (extras.walking)
        {
            drawText(x0 + 16.0f, y0 + 14.0f, 2.2f, "ON FOOT", {0.25f, 0.95f, 1.0f, 1.0f}, screenWidth, screenHeight);
            drawText(x0 + 16.0f, y0 + 48.0f, 1.0f, "SHIFT RUN   F NEAR THE CAR TO DRIVE",
                     {0.80f, 0.86f, 0.92f, 1.0f}, screenWidth, screenHeight);
        }
        else
        {
            std::snprintf(line.data(), line.size(), "%3.0f", std::abs(extras.speedKmh));
            drawText(x0 + 14.0f, y0 + 8.0f, 4.2f, line.data(), {1.0f, 0.82f, 0.08f, 1.0f}, screenWidth, screenHeight);
            drawText(x0 + 138.0f, y0 + 16.0f, 1.5f, extras.speedKmh < -0.5f ? "KM/H  R" : "KM/H",
                     {0.85f, 0.90f, 1.0f, 1.0f}, screenWidth, screenHeight);
            // A bar filling towards 80 km/h, the boosted top speed.
            const float fill = std::min(std::abs(extras.speedKmh) / 80.0f, 1.0f);
            drawRectangle(x0 + 138.0f, y0 + 40.0f, 106.0f, 8.0f, {0.20f, 0.24f, 0.30f, 1.0f}, screenWidth, screenHeight);
            drawRectangle(x0 + 138.0f, y0 + 40.0f, 106.0f * fill, 8.0f,
                          fill > 0.63f ? glm::vec4{1.0f, 0.40f, 0.15f, 1.0f} : glm::vec4{0.25f, 0.95f, 0.45f, 1.0f},
                          screenWidth, screenHeight);
            drawText(x0 + 138.0f, y0 + 54.0f, 0.95f, "V VIEW  F GET OUT", {0.80f, 0.86f, 0.92f, 1.0f}, screenWidth, screenHeight);
        }
    }

    if (extras.message != nullptr && extras.messageAlpha > 0.01f)
    {
        const float textWidth = static_cast<float>(stb_easy_font_width(const_cast<char*>(extras.message))) * 1.6f;
        const float y = static_cast<float>(screenHeight) - 136.0f;
        drawRectangle(centreX - 0.5f * textWidth - 14.0f, y - 8.0f, textWidth + 28.0f, 34.0f,
                      {0.015f, 0.025f, 0.045f, 0.80f * extras.messageAlpha}, screenWidth, screenHeight);
        drawText(centreX - 0.5f * textWidth, y, 1.6f, extras.message, {1.0f, 0.90f, 0.40f, extras.messageAlpha},
                 screenWidth, screenHeight);
    }
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

void Overlay::drawTimePanel(const HudExtras& extras, const std::string& timeText, int screenWidth, int screenHeight)
{
    if (extras.presetNames == nullptr || extras.presetCount <= 0)
        return;

    // Top-right: a button per preset. The part of the day it is now is lit;
    // brightly when the clock stands on that preset (or glides to it).
    const int count = extras.presetCount;
    const float x0 = TimeButtons::left(screenWidth, count);
    const float y0 = TimeButtons::top + TimeButtons::header;
    const float panelWidth = count * TimeButtons::buttonWidth + (count - 1) * TimeButtons::gap + 20.0f;
    drawRectangle(x0 - 10.0f, TimeButtons::top, panelWidth, TimeButtons::header + TimeButtons::buttonHeight + 10.0f,
                  {0.015f, 0.025f, 0.045f, 0.88f}, screenWidth, screenHeight);

    std::array<char, 64> line {};
    std::snprintf(line.data(), line.size(), "TIME OF DAY   %s %s", extras.periodName, timeText.c_str());
    drawText(x0, TimeButtons::top + 8.0f, 1.1f, line.data(), {1.0f, 0.82f, 0.08f, 1.0f}, screenWidth, screenHeight);

    for (int index = 0; index < count; ++index)
    {
        const float x = x0 + index * (TimeButtons::buttonWidth + TimeButtons::gap);
        const bool current = index == extras.period;
        const glm::vec4 fill = current ? (extras.onPreset ? glm::vec4{0.95f, 0.66f, 0.10f, 0.95f}
                                                          : glm::vec4{0.45f, 0.33f, 0.08f, 0.95f})
                                       : glm::vec4{0.10f, 0.16f, 0.24f, 0.95f};
        drawRectangle(x, y0, TimeButtons::buttonWidth, TimeButtons::buttonHeight, fill, screenWidth, screenHeight);
        const char* name = extras.presetNames[index];
        const float scale = 1.15f;
        const float textWidth = static_cast<float>(stb_easy_font_width(const_cast<char*>(name))) * scale;
        drawText(x + 0.5f * (TimeButtons::buttonWidth - textWidth), y0 + 9.0f, scale, name,
                 current && extras.onPreset ? glm::vec4{0.05f, 0.04f, 0.02f, 1.0f} : glm::vec4{0.92f, 0.94f, 0.98f, 1.0f},
                 screenWidth, screenHeight);
    }
}

void Overlay::drawWeatherPanel(const HudExtras& extras, int screenWidth, int screenHeight)
{
    if (extras.weatherNames == nullptr || extras.weatherCount <= 0)
        return;

    // Under the time buttons: the weather's buttons, the one it is in (or
    // blending to) lit, and how wet the streets are.
    const int count = extras.weatherCount;
    const float x0 = WeatherButtons::left(screenWidth, count);
    const float y0 = WeatherButtons::top + TimeButtons::header;
    const float panelWidth = count * TimeButtons::buttonWidth + (count - 1) * TimeButtons::gap + 20.0f;
    drawRectangle(x0 - 10.0f, WeatherButtons::top, panelWidth, TimeButtons::header + TimeButtons::buttonHeight + 10.0f,
                  {0.015f, 0.025f, 0.045f, 0.88f}, screenWidth, screenHeight);

    std::array<char, 64> line {};
    std::snprintf(line.data(), line.size(), "WEATHER   WET %d%%  PUDDLES %d%%",
                  static_cast<int>(std::lround(extras.wetness * 100.0f)),
                  static_cast<int>(std::lround(extras.puddles * 100.0f)));
    drawText(x0, WeatherButtons::top + 8.0f, 1.1f, line.data(), {0.45f, 0.85f, 1.0f, 1.0f}, screenWidth, screenHeight);

    for (int index = 0; index < count; ++index)
    {
        const float x = x0 + index * (TimeButtons::buttonWidth + TimeButtons::gap);
        const bool current = index == extras.weather;
        const bool settled = current && !extras.weatherBlending;
        const glm::vec4 fill = current ? (settled ? glm::vec4{0.25f, 0.62f, 0.90f, 0.95f}
                                                  : glm::vec4{0.14f, 0.32f, 0.48f, 0.95f})
                                       : glm::vec4{0.10f, 0.16f, 0.24f, 0.95f};
        drawRectangle(x, y0, TimeButtons::buttonWidth, TimeButtons::buttonHeight, fill, screenWidth, screenHeight);
        const char* name = extras.weatherNames[index];
        const float scale = 1.15f;
        const float textWidth = static_cast<float>(stb_easy_font_width(const_cast<char*>(name))) * scale;
        drawText(x + 0.5f * (TimeButtons::buttonWidth - textWidth), y0 + 9.0f, scale, name,
                 settled ? glm::vec4{0.02f, 0.04f, 0.07f, 1.0f} : glm::vec4{0.92f, 0.94f, 0.98f, 1.0f},
                 screenWidth, screenHeight);
    }
}
