#version 330 core

// The shadow map: the scene seen from the sun (or the moon), depth only.
// Vertices are placed exactly as the scene shader places them.

#include "placement.glsl"

uniform mat4 uLightViewProjection;
uniform vec2 uUvScale;

out vec2 vTexCoord;

void main()
{
    Placed placed = placeVertex();
    vTexCoord = aTexCoord * uUvScale;
    gl_Position = uLightViewProjection * placed.world;
}
