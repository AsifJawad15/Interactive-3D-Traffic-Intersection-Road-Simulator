#version 330 core

// The sky is drawn as one full-screen triangle behind everything else. Each
// pixel turns its screen position back into a world-space view direction and
// asks the shared atmosphere function for the colour of the sky that way,
// then adds the sun disc, the moon and, at night, the stars.

in vec2 vUv;

uniform mat4 uInverseViewProjection;
uniform vec3 uViewPosition;
uniform vec3 uSunDiscColor;
uniform vec3 uMoonVector;
uniform vec3 uMoonColor;
uniform float uStarVisibility;
uniform float uTime;

#include "atmosphere.glsl"

out vec4 fragmentColor;

float hash(vec3 cell)
{
    return fract(sin(dot(cell, vec3(12.9898, 78.233, 37.719))) * 43758.5453);
}

void main()
{
    vec4 farPoint = uInverseViewProjection * vec4(vUv * 2.0 - 1.0, 1.0, 1.0);
    vec3 direction = normalize(farPoint.xyz / farPoint.w - uViewPosition);

    vec3 color = atmosphereColor(direction);
    float aboveHorizon = smoothstep(-0.02, 0.02, direction.y);

    // Sun disc, about half a degree across, with a thin soft edge.
    float sunDisc = smoothstep(0.99985, 0.99993, dot(direction, uSunVector));
    color += uSunDiscColor * sunDisc * aboveHorizon;

    // Moon disc plus a faint halo.
    float towardsMoon = dot(direction, uMoonVector);
    float moonDisc = smoothstep(0.99955, 0.99965, towardsMoon);
    color += uMoonColor * (moonDisc + 0.04 * pow(max(towardsMoon, 0.0), 180.0)) * aboveHorizon;

    // Stars: the direction is quantised into small cells and a few cells,
    // chosen by a hash, hold a point of light that twinkles slowly.
    if (uStarVisibility > 0.0 && direction.y > 0.0)
    {
        const float cellsPerUnit = 220.0;
        vec3 scaled = direction * cellsPerUnit;
        vec3 cell = floor(scaled);
        float random = hash(cell);
        if (random > 0.9965)
        {
            float distanceToCentre = length(fract(scaled) - 0.5);
            float point = smoothstep(0.30, 0.0, distanceToCentre);
            float twinkle = 0.65 + 0.35 * sin(uTime * (1.5 + 3.0 * random) + random * 40.0);
            float brightness = 0.6 + 2.4 * fract(random * 173.0);
            color += vec3(0.85, 0.90, 1.0) * point * twinkle * brightness * uStarVisibility *
                     smoothstep(0.0, 0.25, direction.y);
        }
    }

    fragmentColor = vec4(color, 1.0);
}
