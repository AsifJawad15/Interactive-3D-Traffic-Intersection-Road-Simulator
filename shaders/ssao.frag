#version 330 core

// Enhanced mode, at half resolution, two effects that share one look at
// the depth buffer:
//   ambient occlusion (SSAO): how much of the sky's soft light the nearby
//     surfaces keep off a point: under a car, where a wall meets the
//     pavement, round a bin;
//   contact shadows: a short march from the point towards the sun, which
//     finds the thin shadows the shadow map is too coarse to hold (under a
//     tyre, at a foot, along a kerb).
// Out: r = the ambient light kept, g = the sunlight kept, b = the point's
// distance, which the composite's depth-aware upsample compares against.

in vec2 vUv;

#include "enhanced.glsl"

uniform float uAoRadius;      // metres
uniform vec3 uLightView;      // towards the sun (or the moon), in view space
uniform float uContact;       // 0: no contact shadows (no direct light to speak of)

out vec4 fragmentColor;

void main()
{
    ivec2 texel = min(ivec2(gl_FragCoord.xy) * 2, ivec2(uFullSize) - 1);
    float depth = texelFetch(uDepth, texel, 0).r;
    if (depth >= 1.0)
    {
        fragmentColor = vec4(1.0, 1.0, 1.0e5, 1.0);
        return;
    }
    vec3 point = viewPositionAt(texel);
    vec3 normal = viewNormalAt(texel, point);
    float eyeDistance = -point.z;
    float noise = pixelNoise(gl_FragCoord.xy);

    // Occlusion: points scattered over the half ball above the surface
    // (more of them close in, and more towards the normal, as the light it
    // gets comes mostly from overhead). Each one that lies behind the
    // depth buffer is shut in by something, unless that something is far
    // in front of it (a pole metres nearer the camera shuts in nothing).
    float ambient = 1.0;
    float aoFade = 1.0 - smoothstep(45.0, 90.0, eyeDistance);
    if (aoFade > 0.0)
    {
        vec3 helper = abs(normal.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        vec3 tangent = normalize(cross(helper, normal));
        vec3 bitangent = cross(normal, tangent);
        const int samples = 12;
        float occluded = 0.0;
        for (int index = 0; index < samples; ++index)
        {
            float share = (float(index) + 0.5) / float(samples);
            float angle = float(index) * 2.3999632 + noise * 6.2831853;
            float spread = sqrt(share);
            vec3 direction = tangent * (cos(angle) * spread) + bitangent * (sin(angle) * spread) +
                             normal * sqrt(1.0 - share);
            float reach = fract(share * 5.0 + noise);
            vec3 probe = point + direction * uAoRadius * mix(0.12, 1.0, reach * reach);
            vec2 uv = screenPoint(probe);
            if (!onScreen(uv))
                continue;
            float sceneDistance = viewDepth(textureLod(uDepth, uv, 0.0).r);
            float inFront = -probe.z - sceneDistance;
            float near = clamp(uAoRadius / max(abs(eyeDistance - sceneDistance), 1e-3), 0.0, 1.0);
            occluded += step(0.02 + 0.0015 * eyeDistance, inFront) * near * near;
        }
        ambient = 1.0 - occluded / float(samples) * aoFade;
    }

    // Contact shadows: a few steps up the sunbeam, less than a metre (a
    // little more far off, where a pixel is wider). The first step that
    // lies behind something close in front of it is in that something's
    // shadow; a hit near the start is darker than one at the far end.
    float sun = 1.0;
    float contactFade = uContact * (1.0 - smoothstep(18.0, 32.0, eyeDistance));
    if (contactFade > 0.0 && dot(normal, uLightView) > 0.05)
    {
        const int steps = 10;
        float beam = 0.4 + 0.015 * eyeDistance;
        vec3 start = point + normal * (0.01 + 0.002 * eyeDistance);
        for (int index = 1; index <= steps; ++index)
        {
            vec3 along = start + uLightView * beam * ((float(index) - noise) / float(steps));
            vec2 uv = screenPoint(along);
            if (!onScreen(uv))
                break;
            float inFront = -along.z - viewDepth(textureLod(uDepth, uv, 0.0).r);
            if (inFront > 0.015 + 0.001 * eyeDistance && inFront < 0.35)
            {
                sun = 1.0 - contactFade * (1.0 - 0.5 * float(index) / float(steps));
                break;
            }
        }
    }

    fragmentColor = vec4(ambient, sun, eyeDistance, 1.0);
}
