#version 330 core

// Final pass: HDR scene + bloom  ->  exposure  ->  ACES tone curve  ->  sRGB.
//
// The scene is lit in linear light, where values can exceed 1.0. The ACES
// curve compresses that range smoothly into 0..1 (bright areas roll off
// instead of clipping to white), and the sRGB encoding converts linear light
// into the gamma-encoded values a monitor expects. Skipping that encoding is
// what made the old renderer's dark areas look crushed and black.

in vec2 vUv;

uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform float uExposure;
uniform float uBloomStrength;

out vec4 fragmentColor;

vec3 acesFilm(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 linearToSrgb(vec3 color)
{
    vec3 low = color * 12.92;
    vec3 high = 1.055 * pow(color, vec3(1.0 / 2.4)) - 0.055;
    return mix(low, high, step(vec3(0.0031308), color));
}

// A tiny amount of noise hides the banding that 8-bit output would otherwise
// show in smooth gradients such as the sky.
float dither(vec2 pixel)
{
    return fract(sin(dot(pixel, vec2(12.9898, 78.233))) * 43758.5453) - 0.5;
}

void main()
{
    vec3 scene = texture(uScene, vUv).rgb * uExposure;
    vec3 bloom = texture(uBloom, vUv).rgb;   // exposure was applied when it was extracted
    vec3 color = scene + bloom * uBloomStrength;

    vec3 display = linearToSrgb(acesFilm(color));
    display += dither(gl_FragCoord.xy) / 255.0;
    fragmentColor = vec4(display, 1.0);
}
