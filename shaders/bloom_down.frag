#version 330 core

// One step of the bloom down-sampling chain: a 13-tap filter that halves the
// resolution without the blocky artefacts of a plain 2x2 average.
//
// The first step also keeps only the light brighter than a threshold (with a
// soft knee, so there is no hard edge between glowing and not glowing), and
// weights each group of samples by 1 / (1 + brightness). That "Karis average"
// stops a single very bright pixel, such as a distant lamp, from flickering as
// the camera moves.

in vec2 vUv;

uniform sampler2D uSource;
uniform vec2 uSourceTexelSize;
uniform int uPrefilter;
uniform float uThreshold;
uniform float uKnee;
uniform float uExposure;

out vec4 fragmentColor;

vec3 prefilter(vec3 color)
{
    color *= uExposure;
    float brightness = max(color.r, max(color.g, color.b));
    float soft = clamp(brightness - uThreshold + uKnee, 0.0, 2.0 * uKnee);
    soft = soft * soft / (4.0 * uKnee + 1e-4);
    float contribution = max(soft, brightness - uThreshold) / max(brightness, 1e-4);
    return min(color * contribution, vec3(64.0));
}

float karisWeight(vec3 color)
{
    return 1.0 / (1.0 + max(color.r, max(color.g, color.b)));
}

vec3 tap(vec2 offset)
{
    return texture(uSource, vUv + uSourceTexelSize * offset).rgb;
}

void main()
{
    vec3 a = tap(vec2(-2.0,  2.0));
    vec3 b = tap(vec2( 0.0,  2.0));
    vec3 c = tap(vec2( 2.0,  2.0));
    vec3 d = tap(vec2(-2.0,  0.0));
    vec3 e = tap(vec2( 0.0,  0.0));
    vec3 f = tap(vec2( 2.0,  0.0));
    vec3 g = tap(vec2(-2.0, -2.0));
    vec3 h = tap(vec2( 0.0, -2.0));
    vec3 i = tap(vec2( 2.0, -2.0));
    vec3 j = tap(vec2(-1.0,  1.0));
    vec3 k = tap(vec2( 1.0,  1.0));
    vec3 l = tap(vec2(-1.0, -1.0));
    vec3 m = tap(vec2( 1.0, -1.0));

    vec3 result;
    if (uPrefilter == 1)
    {
        vec3 group0 = prefilter((a + b + d + e) * 0.25);
        vec3 group1 = prefilter((b + c + e + f) * 0.25);
        vec3 group2 = prefilter((d + e + g + h) * 0.25);
        vec3 group3 = prefilter((e + f + h + i) * 0.25);
        vec3 group4 = prefilter((j + k + l + m) * 0.25);

        float weight0 = karisWeight(group0) * 0.125;
        float weight1 = karisWeight(group1) * 0.125;
        float weight2 = karisWeight(group2) * 0.125;
        float weight3 = karisWeight(group3) * 0.125;
        float weight4 = karisWeight(group4) * 0.5;

        result = (group0 * weight0 + group1 * weight1 + group2 * weight2 +
                  group3 * weight3 + group4 * weight4) /
                 (weight0 + weight1 + weight2 + weight3 + weight4);
    }
    else
    {
        result = e * 0.125 +
                 (a + c + g + i) * 0.03125 +
                 (b + d + f + h) * 0.0625 +
                 (j + k + l + m) * 0.125;
    }

    fragmentColor = vec4(max(result, vec3(0.0)), 1.0);
}
