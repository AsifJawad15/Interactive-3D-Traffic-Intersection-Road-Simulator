#version 330 core

// A streak is brightest along its middle line and towards its head, and
// fades out to both sides and at the tail. Streaks are added onto the scene
// (additive blending): a drop only ever brightens what is behind it. Alpha
// 0 leaves untouched how much of the surface behind is a mirror.

in vec2 vCorner;
in vec3 vColor;

uniform float uStrength;

out vec4 fragmentColor;

void main()
{
    float across = 1.0 - vCorner.x * vCorner.x;
    float along = smoothstep(0.0, 0.6, vCorner.y) * (0.6 + 0.4 * vCorner.y);
    fragmentColor = vec4(vColor * (across * along * uStrength), 0.0);
}
