#version 330 core

layout (location = 0) in vec2 aPosition;

uniform vec2 uScreenSize;

void main()
{
    vec2 ndc;
    ndc.x = (aPosition.x / uScreenSize.x) * 2.0 - 1.0;
    ndc.y = 1.0 - (aPosition.y / uScreenSize.y) * 2.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
