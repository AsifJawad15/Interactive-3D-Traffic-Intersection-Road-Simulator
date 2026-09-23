#version 330 core

// A single triangle that covers the whole screen, generated from gl_VertexID
// so no vertex buffer is needed. Its corners are (-1,-1), (3,-1) and (-1,3);
// the parts outside the screen are clipped away.
out vec2 vUv;

void main()
{
    vec2 position = vec2(gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? 3.0 : -1.0);
    vUv = position * 0.5 + 0.5;
    gl_Position = vec4(position, 0.0, 1.0);
}
