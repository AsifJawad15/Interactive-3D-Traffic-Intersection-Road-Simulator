#version 330 core

// Enhanced mode's sun shafts, step 2, at a quarter of the resolution: every
// pixel gathers the light of step 1 along the line from itself towards the
// sun, the nearer the more. Where buildings and trees stand in the way the
// line picks up nothing, so they cast dark beams through the bright haze
// (light scattering as a post-process, after Mitchell, GPU Gems 3 ch. 13).

in vec2 vUv;

uniform sampler2D uMask;
uniform vec2 uSunUv;

out vec4 fragmentColor;

void main()
{
    const int samples = 40;
    // The whole way to the sun, in 40 steps; the first one staggered per
    // pixel, so the steps do not show as rings.
    vec2 stride = (uSunUv - vUv) / float(samples);
    float noise = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    vec2 uv = vUv + stride * noise;
    vec3 light = vec3(0.0);
    float weight = 1.0;
    for (int index = 0; index < samples; ++index)
    {
        light += textureLod(uMask, uv, 0.0).rgb * weight;
        weight *= 0.95;
        uv += stride;
    }
    fragmentColor = vec4(light / float(samples), 1.0);
}
