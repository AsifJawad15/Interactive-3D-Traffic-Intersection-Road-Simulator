#pragma once

#include "Shader.h"

#include <glad/glad.h>
#include <glm/vec2.hpp>
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
        bool showHelp);

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
    void appendRectangle(float x, float y, float width, float height);
    void drawVertices(const glm::vec4& color, int screenWidth, int screenHeight);
};
