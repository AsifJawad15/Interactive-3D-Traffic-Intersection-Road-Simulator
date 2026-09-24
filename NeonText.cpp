#include "NeonText.h"

#include <glm/gtc/matrix_transform.hpp>

#include <stb_easy_font.h>

#include <algorithm>

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

std::vector<TextStroke> textStrokes(const std::string& text)
{
    std::vector<char> buffer(text.begin(), text.end());
    buffer.push_back('\0');
    std::vector<unsigned char> vertices(64 * 1024);
    const int quads = stb_easy_font_print(0.0f, 0.0f, buffer.data(), nullptr,
                                          vertices.data(), static_cast<int>(vertices.size()));
    const auto* raw = reinterpret_cast<const EasyFontVertex*>(vertices.data());
    std::vector<TextStroke> strokes;
    for (int quad = 0; quad < quads; ++quad)
    {
        const EasyFontVertex& a = raw[quad * 4];
        const EasyFontVertex& c = raw[quad * 4 + 2];
        strokes.push_back({std::min(a.x, c.x), std::min(a.y, c.y), std::max(a.x, c.x), std::max(a.y, c.y)});
    }
    return strokes;
}

glm::vec2 textExtent(const std::vector<TextStroke>& strokes)
{
    glm::vec2 extent {0.0f};
    for (const TextStroke& stroke : strokes)
        extent = glm::max(extent, glm::vec2{stroke.x1, stroke.y1});
    return extent;
}

void appendText(MeshBuilder& builder, const std::string& text, const glm::mat4& frame,
                float letterHeight, float depth, float thin)
{
    static const MeshData box = Mesh::beveledCubeData(0.12f);
    const std::vector<TextStroke> strokes = textStrokes(text);
    const glm::vec2 extent = textExtent(strokes);
    const float scale = letterHeight / std::max(extent.y, 1.0f);
    for (const TextStroke& stroke : strokes)
    {
        float width = stroke.x1 - stroke.x0;
        float height = stroke.y1 - stroke.y0;
        if (width <= 1.01f)
            width *= thin;
        if (height <= 1.01f)
            height *= thin;
        const float u = 0.5f * (stroke.x0 + stroke.x1) - 0.5f * extent.x;
        const float v = 0.5f * extent.y - 0.5f * (stroke.y0 + stroke.y1);
        glm::mat4 model = glm::translate(frame, {u * scale, v * scale, 0.5f * depth});
        model = glm::scale(model, {width * scale, height * scale, depth});
        builder.append(box, model);
    }
}
