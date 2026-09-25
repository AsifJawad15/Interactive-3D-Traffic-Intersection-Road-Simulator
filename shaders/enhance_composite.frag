#version 330 core

// Enhanced mode's last pass, at full resolution: the scene with its
// ambient occlusion and contact shadows, its reflections and the sun
// shafts. The half-resolution results are brought up with a depth-aware
// (bilateral) filter: of the four half-resolution pixels round a pixel, the
// ones at its own depth count, so a car's shading never bleeds onto the
// road behind it.

in vec2 vUv;

#include "enhanced.glsl"

uniform sampler2D uScene;         // the HDR scene; alpha: how much of it is a mirror
uniform sampler2D uOcclusion;     // half resolution: ambient kept, sunlight kept, distance
uniform sampler2D uReflections;   // half resolution: colour found times sureness, sureness
uniform sampler2D uShafts;        // quarter resolution
uniform vec2 uHalfSize;
uniform float uAmbientStrength;
uniform float uContactStrength;
uniform float uReflect;           // 1: reflections on
uniform float uExposure;
uniform vec3 uShaftColor;

out vec4 fragmentColor;

void main()
{
    ivec2 texel = ivec2(gl_FragCoord.xy);
    vec4 scene = texelFetch(uScene, texel, 0);
    vec3 color = scene.rgb;
    float depth = texelFetch(uDepth, texel, 0).r;

    if (depth < 1.0)
    {
        // Half-resolution pixel i was worked out at full-resolution pixel 2i.
        float eyeDistance = viewDepth(depth);
        vec2 halfPoint = vec2(texel) * 0.5;
        ivec2 base = ivec2(floor(halfPoint));
        vec2 blend = halfPoint - vec2(base);
        ivec2 last = ivec2(uHalfSize) - 1;
        vec4 occlusion = vec4(0.0);
        vec4 reflection = vec4(0.0);
        float total = 0.0;
        for (int corner = 0; corner < 4; ++corner)
        {
            ivec2 offset = ivec2(corner & 1, corner >> 1);
            ivec2 at = min(base + offset, last);
            vec4 shading = texelFetch(uOcclusion, at, 0);
            float bilinear = mix(1.0 - blend.x, blend.x, float(offset.x)) * mix(1.0 - blend.y, blend.y, float(offset.y));
            float weight = bilinear * (exp(-abs(shading.b - eyeDistance) / (0.03 * eyeDistance + 0.05)) + 1e-4);
            occlusion += shading * weight;
            reflection += texelFetch(uReflections, at, 0) * weight;
            total += weight;
        }
        occlusion /= total;
        reflection /= total;

        // Shade the lit surfaces, but not what glows: a neon tube or a lit
        // window in a corner stays as bright as it is.
        float glow = dot(color, vec3(0.2126, 0.7152, 0.0722)) * uExposure;
        float shade = mix(1.0, occlusion.r, uAmbientStrength) * mix(1.0, occlusion.g, uContactStrength);
        color *= mix(shade, 1.0, smoothstep(1.5, 4.0, glow));

        // A rough mirror (a wet road rather than a puddle) smears what it
        // reflects up and down: the long streaks of lamps and neon on a
        // wet street at night.
        if (uReflect > 0.5 && scene.a > 0.01)
        {
            float rough = 1.0 - smoothstep(0.2, 0.55, scene.a);
            if (rough > 0.0)
            {
                vec2 halfUv = (halfPoint + 0.5) / uHalfSize;
                vec4 streak = reflection;
                for (int tap = 1; tap <= 3; ++tap)
                {
                    vec2 offset = vec2(0.0, 1.6 * float(tap) / uHalfSize.y);
                    streak += texture(uReflections, halfUv + offset) + texture(uReflections, halfUv - offset);
                }
                reflection = mix(reflection, streak / 7.0, rough);
            }
            vec3 found = reflection.rgb / max(reflection.a, 1e-3);
            color = mix(color, found, clamp(scene.a * reflection.a, 0.0, 1.0));
        }
    }

    color += texture(uShafts, vUv).rgb * uShaftColor;
    fragmentColor = vec4(color, 1.0);
}
