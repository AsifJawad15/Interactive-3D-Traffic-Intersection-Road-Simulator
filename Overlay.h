#pragma once

#include "Shader.h"

#include <glad/glad.h>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstddef>
#include <string>
#include <vector>

// Frame timing shown on the HUD: the numbers line, and with F5 the graph of
// the last frames' times.
struct PerformanceInfo
{
    float fps = 0.0f;
    float frameMs = 0.0f;
    float gpuMs = 0.0f;
    int drawCalls = 0;       // the 3D scene's draw calls last frame
    int renderWidth = 0;
    int renderHeight = 0;
    int scalePercent = 100;
    const char* resolutionMode = "";
    bool showGraph = false;
    int pacingHz = 60;
    bool fullRatePacing = false;
    const float* frameHistory = nullptr;   // milliseconds, a ring buffer
    std::size_t historySize = 0;
    std::size_t historyHead = 0;           // index of the oldest entry
};

// The player's corner of the HUD: speed, messages and the minimap. Positions
// are world metres (x, z).
struct HudExtras
{
    bool onPlayer = false;     // the camera is locked onto the player
    bool walking = false;
    float speedKmh = 0.0f;
    const char* message = nullptr;
    float messageAlpha = 0.0f;

    // Minimap. Roads are straight pieces (x0, z0, x1, z1) between junction
    // centres; every signalised junction shows a bar per axis in its colour.
    const std::vector<glm::vec4>* roads = nullptr;
    const std::vector<glm::vec2>* roundabouts = nullptr;
    const std::vector<glm::vec2>* cars = nullptr;
    const std::vector<glm::vec2>* buses = nullptr;       // the line buses, larger and orange
    const std::vector<glm::vec2>* emergency = nullptr;   // police cars and ambulances, red and blue
    float seconds = 0.0f;                                // for their flashing
    const std::vector<glm::vec2>* signals = nullptr;
    const std::vector<glm::vec3>* northSouthColors = nullptr;
    const std::vector<glm::vec3>* eastWestColors = nullptr;
    glm::vec2 playerCar {0.0f};
    float playerCarYawDegrees = 0.0f;
    glm::vec2 walker {0.0f};

    // The people: on the minimap, and how many wait at or are on a crossing.
    const std::vector<glm::vec2>* people = nullptr;
    std::size_t peopleWaiting = 0;
    std::size_t peopleCrossing = 0;

    // The time of day: the part of the day it is (0 Morning .. 4 Night),
    // whether the clock stands on that preset, and the preset names for the
    // corner buttons.
    int period = 0;
    bool onPreset = false;
    const char* periodName = "";
    const char* const* presetNames = nullptr;
    int presetCount = 0;

    // Shadows and headlights, for the status lines.
    const char* shadowQuality = "";
    int shadowMaps = 0;
    float shadowMs = 0.0f;
    int headlights = 0;
    bool moonlight = false;

    // The weather, for its corner buttons: the state it is in or blending
    // to (0 Clear, 1 Cloudy, 2 Rain), the names, and how wet the city is.
    int weather = 0;
    bool weatherBlending = false;
    const char* const* weatherNames = nullptr;
    int weatherCount = 0;
    float wetness = 0.0f;
    float puddles = 0.0f;

    // Enhanced mode (partial ray tracing): on or off, the question asked
    // before switching (open, and the button under the cursor: 0 yes, 1 no,
    // -1 neither), and the GPU time of each of its passes
    // (Enhanced::reflectionsSection ...).
    bool enhanced = false;
    bool confirmOpen = false;
    int confirmHover = -1;
    const float* enhancedMs = nullptr;
    bool softShadows = false;
    bool sunShafts = false;
};

// The clickable time buttons in the top-right corner, as laid out on a
// screen `screenWidth` pixels wide.
struct TimeButtons
{
    static constexpr float buttonWidth = 100.0f;
    static constexpr float buttonHeight = 30.0f;
    static constexpr float gap = 6.0f;
    static constexpr float top = 14.0f;
    static constexpr float header = 26.0f;
    static float left(int screenWidth, int count)
    {
        return static_cast<float>(screenWidth) - 24.0f - count * buttonWidth - (count - 1) * gap;
    }
    // The button under a point in framebuffer pixels, or -1.
    static int at(float x, float y, int screenWidth, int count)
    {
        const float x0 = left(screenWidth, count);
        const float y0 = top + header;
        if (y < y0 || y > y0 + buttonHeight || x < x0)
            return -1;
        const int index = static_cast<int>((x - x0) / (buttonWidth + gap));
        if (index >= count || x - x0 - index * (buttonWidth + gap) > buttonWidth)
            return -1;
        return index;
    }
};

// The weather buttons, a second row under the time buttons, right-aligned
// with them.
struct WeatherButtons
{
    static constexpr float top = TimeButtons::top + TimeButtons::header + TimeButtons::buttonHeight + 18.0f;
    static float left(int screenWidth, int count) { return TimeButtons::left(screenWidth, count); }
    static int at(float x, float y, int screenWidth, int count)
    {
        const float x0 = left(screenWidth, count);
        const float y0 = top + TimeButtons::header;
        if (y < y0 || y > y0 + TimeButtons::buttonHeight || x < x0)
            return -1;
        const int index = static_cast<int>((x - x0) / (TimeButtons::buttonWidth + TimeButtons::gap));
        if (index >= count || x - x0 - index * (TimeButtons::buttonWidth + TimeButtons::gap) > TimeButtons::buttonWidth)
            return -1;
        return index;
    }
};

// The ray-tracing (Enhanced mode) button: one wide button in a third panel
// under the weather's, as wide as the rows of buttons above it.
struct EnhancedButton
{
    static constexpr float top = WeatherButtons::top + TimeButtons::header + TimeButtons::buttonHeight + 18.0f;
    static constexpr int columns = 5;   // as wide as the five time buttons
    static constexpr float width = columns * TimeButtons::buttonWidth + (columns - 1) * TimeButtons::gap;
    static float left(int screenWidth) { return TimeButtons::left(screenWidth, columns); }
    static bool at(float x, float y, int screenWidth)
    {
        const float x0 = left(screenWidth);
        const float y0 = top + TimeButtons::header;
        return x >= x0 && x <= x0 + width && y >= y0 && y <= y0 + TimeButtons::buttonHeight;
    }
};

// The question asked in the middle of the screen before ray tracing is
// switched: "RAY TRACING ON?" with a YES and a NO button.
struct ConfirmDialog
{
    static constexpr float width = 640.0f;
    static constexpr float height = 290.0f;
    static constexpr float buttonWidth = 170.0f;
    static constexpr float buttonHeight = 44.0f;
    static constexpr float buttonGap = 40.0f;
    static float left(int screenWidth) { return 0.5f * (static_cast<float>(screenWidth) - width); }
    static float top(int screenHeight) { return 0.5f * (static_cast<float>(screenHeight) - height); }
    static float buttonLeft(int screenWidth, int button)
    {
        return 0.5f * static_cast<float>(screenWidth) - buttonWidth - 0.5f * buttonGap +
               static_cast<float>(button) * (buttonWidth + buttonGap);
    }
    static float buttonTop(int screenHeight) { return top(screenHeight) + height - buttonHeight - 40.0f; }
    // 0 on YES, 1 on NO, 2 elsewhere on the panel, -1 outside it.
    static int at(float x, float y, int screenWidth, int screenHeight)
    {
        for (int button = 0; button < 2; ++button)
        {
            const float x0 = buttonLeft(screenWidth, button);
            const float y0 = buttonTop(screenHeight);
            if (x >= x0 && x <= x0 + buttonWidth && y >= y0 && y <= y0 + buttonHeight)
                return button;
        }
        const float x0 = left(screenWidth);
        const float y0 = top(screenHeight);
        return x >= x0 && x <= x0 + width && y >= y0 && y <= y0 + height ? 2 : -1;
    }
};

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
        const HudExtras& extras);

private:
    Shader shader_;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;

    // Scratch space, allocated once instead of per string or per frame.
    std::vector<unsigned char> textBuffer_ = std::vector<unsigned char>(128 * 1024);
    std::vector<glm::vec2> vertices_;

    void drawRectangle(float x, float y, float width, float height, const glm::vec4& color, int screenWidth, int screenHeight);
    void drawText(float x, float y, float scale, const char* text, const glm::vec4& color, int screenWidth, int screenHeight);
    void drawFrameGraph(const PerformanceInfo& performance, int screenWidth, int screenHeight);
    void drawMinimap(const HudExtras& extras, int screenWidth, int screenHeight);
    void drawPlayerPanel(const HudExtras& extras, int screenWidth, int screenHeight);
    void drawTimePanel(const HudExtras& extras, const std::string& timeText, int screenWidth, int screenHeight);
    void drawWeatherPanel(const HudExtras& extras, int screenWidth, int screenHeight);
    void drawEnhancedPanel(const HudExtras& extras, int screenWidth, int screenHeight);
    void drawConfirmDialog(const HudExtras& extras, int screenWidth, int screenHeight);
    void appendRectangle(float x, float y, float width, float height);
    void drawVertices(const glm::vec4& color, int screenWidth, int screenHeight);
};
