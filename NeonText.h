#pragma once

#include "Mesh.h"
#include "MeshBuilder.h"

#include <glm/mat4x4.hpp>

#include <string>
#include <vector>

// Lettering built as geometry. stb_easy_font describes a line of text as a
// list of small rectangles (a capital letter is about 7 units tall); every
// rectangle becomes a thin box, which is how the neon signs, the shop signs,
// the bus stop plates and the speed-limit numbers are all made.

struct TextStroke
{
    float x0, y0, x1, y1;   // stb_easy_font units, y downwards
};

std::vector<TextStroke> textStrokes(const std::string& text);

// Width and height of a line of text, in stb_easy_font units.
glm::vec2 textExtent(const std::vector<TextStroke>& strokes);

// Bakes a line of text into `builder` as boxes: centred on the origin of
// `frame`, reading along its +x with +y up, standing out towards +z by
// `depth`. `letterHeight` is in metres. Strokes one unit thick are drawn
// `thin` times as thick, which reads better for tubes and paint alike.
void appendText(MeshBuilder& builder, const std::string& text, const glm::mat4& frame,
                float letterHeight, float depth, float thin = 0.62f);
