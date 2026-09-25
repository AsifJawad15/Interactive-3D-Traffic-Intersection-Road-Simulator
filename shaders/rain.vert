#version 330 core

// One raindrop per instance, drawn as a thin quad stretched along the way it
// falls (a streak, as a camera would blur it). The drops fill a box that
// follows the camera, but each drop's place is fixed in the world and only
// wraps round the box as the camera moves: walk or drive through the rain
// and you pass the drops instead of carrying them along.

layout (location = 0) in vec4 aDrop;   // xyz: where in the box (0..1), w: a random number

uniform mat4 uViewProjection;
uniform vec3 uCameraPosition;
uniform float uTime;
uniform vec3 uVelocity;         // m/s: falling, and carried by the wind
uniform float uStreakSeconds;   // how much of its fall one streak shows
uniform float uPixelSize;       // one pixel's width at one metre
uniform vec3 uAmbient;          // the sky's light on a drop

#include "lights.glsl"

out vec2 vCorner;   // x: -1..1 across the streak, y: 0 at the tail, 1 at the head
out vec3 vColor;

const vec3 box = vec3(36.0, 20.0, 36.0);

void main()
{
    // Heavier drops fall a little faster.
    vec3 velocity = uVelocity * (0.85 + 0.3 * aDrop.w);
    vec3 world = aDrop.xyz * box + velocity * uTime;
    vec3 offset = mod(world - uCameraPosition + 0.5 * box, box) - 0.5 * box;
    vec3 head = uCameraPosition + offset;
    vec3 tail = head - velocity * uStreakSeconds;

    // Corners 0 and 1 at the head, 2 and 3 at the tail (a triangle strip).
    bool atHead = gl_VertexID < 2;
    float side = (gl_VertexID & 1) == 0 ? -1.0 : 1.0;
    vec3 point = atHead ? head : tail;
    vec3 toEye = uCameraPosition - point;
    float distanceToEye = length(toEye);
    vec3 across = normalize(cross(head - tail, toEye));
    // Never thinner than most of a pixel, or distant drops would flicker in
    // and out between pixels; a widened drop is fainter to make up for it.
    float width = max(0.0035, distanceToEye * uPixelSize * 0.75);
    point += across * side * width;
    gl_Position = uViewProjection * vec4(point, 1.0);
    vCorner = vec2(side, atHead ? 1.0 : 0.0);

    // Gone near the eye (they would smear across the view), towards the
    // sides of the box, and below the ground.
    float fade = smoothstep(0.5, 1.8, distanceToEye) *
                 (1.0 - smoothstep(0.30 * box.x, 0.5 * box.x, length(offset.xz))) *
                 (1.0 - smoothstep(0.35 * box.y, 0.5 * box.y, abs(offset.y))) *
                 step(0.0, head.y) * clamp(0.0035 / width, 0.3, 1.0);
    // The sky lights a drop softly; under a street lamp or in a headlight
    // beam it glitters.
    vColor = (uAmbient * 0.3 + lightGlow(head) * 0.55) * fade;
}
