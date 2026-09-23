#version 330 core

// One step of the bloom up-sampling chain: a 3x3 tent filter whose result is
// ADDED (with additive blending) to the next larger level. Summing every level
// gives a glow that is tight near the light and wide and faint further out.

in vec2 vUv;

uniform sampler2D uSource;
uniform vec2 uSourceTexelSize;
uniform float uRadius;

out vec4 fragmentColor;

vec3 tap(vec2 offset)
{
    return texture(uSource, vUv + uSourceTexelSize * uRadius * offset).rgb;
}

void main()
{
    vec3 result = tap(vec2(0.0, 0.0)) * 4.0;
    result += (tap(vec2(-1.0, 0.0)) + tap(vec2(1.0, 0.0)) + tap(vec2(0.0, -1.0)) + tap(vec2(0.0, 1.0))) * 2.0;
    result += tap(vec2(-1.0, -1.0)) + tap(vec2(1.0, -1.0)) + tap(vec2(-1.0, 1.0)) + tap(vec2(1.0, 1.0));
    fragmentColor = vec4(result / 16.0, 1.0);
}
