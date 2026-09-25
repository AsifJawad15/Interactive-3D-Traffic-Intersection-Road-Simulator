#version 330 core

// Enhanced mode, at half resolution: screen-space reflections. From every
// pixel that is partly a mirror (the scene wrote how much into its alpha),
// a ray is reflected off the surface and marched through the depth buffer
// until it passes just behind something: the colour there is what the
// mirror shows. This is ray tracing against the depth buffer instead of
// against the city, so a ray that leaves the screen, or turns back towards
// the camera, finds nothing, and the surface keeps the sky it already
// reflects.
// Out: the colour found times how sure the hit is, and how sure it is.

in vec2 vUv;

#include "enhanced.glsl"

uniform sampler2D uScene;     // the resolved HDR scene; alpha: how much of it is a mirror

out vec4 fragmentColor;

void main()
{
    ivec2 texel = min(ivec2(gl_FragCoord.xy) * 2, ivec2(uFullSize) - 1);
    float mirror = texelFetch(uScene, texel, 0).a;
    float depth = texelFetch(uDepth, texel, 0).r;
    if (mirror < 0.01 || depth >= 1.0)
    {
        fragmentColor = vec4(0.0);
        return;
    }
    vec3 point = viewPositionAt(texel);
    vec3 normal = viewNormalAt(texel, point);
    vec3 ray = reflect(normalize(point), normal);
    // A ray coming back towards the camera meets the backs of things,
    // which the depth buffer does not hold.
    float outward = 1.0 - smoothstep(-0.1, 0.3, ray.z);
    if (outward <= 0.0)
    {
        fragmentColor = vec4(0.0);
        return;
    }

    // Steps that start short (a few centimetres near the camera) and grow,
    // so a ray reaches a hundred metres or more in 32 steps. Where a step
    // lands behind the depth buffer, and not so far behind that it passed
    // behind a whole object, the ray has hit; halving the last step five
    // times finds the surface.
    float eyeDistance = -point.z;
    float stepLength = 0.1 + 0.01 * eyeDistance;
    vec3 previous = point + normal * (0.02 + 0.002 * eyeDistance);
    vec3 current = previous + ray * stepLength * pixelNoise(gl_FragCoord.xy);
    const int steps = 32;
    int taken = 0;
    bool hit = false;
    vec2 uv = vec2(0.0);
    for (; taken < steps; ++taken)
    {
        previous = current;
        current += ray * stepLength;
        stepLength *= 1.12;
        if (current.z > -0.2)
            break;
        uv = screenPoint(current);
        if (!onScreen(uv))
            break;
        float behind = -current.z - viewDepth(textureLod(uDepth, uv, 0.0).r);
        if (behind > 0.0 && behind < max(0.4, 1.5 * stepLength))
        {
            hit = true;
            break;
        }
    }
    if (hit)
    {
        vec3 front = previous;
        vec3 back = current;
        for (int halving = 0; halving < 5; ++halving)
        {
            vec3 middle = 0.5 * (front + back);
            if (-middle.z > viewDepth(textureLod(uDepth, screenPoint(middle), 0.0).r))
                back = middle;
            else
                front = middle;
        }
        uv = screenPoint(back);
        hit = textureLod(uDepth, uv, 0.0).r < 1.0;
    }
    if (!hit)
    {
        fragmentColor = vec4(0.0);
        return;
    }

    // Less sure near the screen's edges (just past them the ray would have
    // found nothing) and at the end of the ray's reach.
    vec2 edge = min(uv, 1.0 - uv);
    float sure = smoothstep(0.0, 0.07, min(edge.x, edge.y)) * outward *
                 (1.0 - smoothstep(0.6, 1.0, float(taken) / float(steps)));
    // A lamp's core is hundreds of times brighter than the road; unclamped,
    // one such pixel would sparkle as the ray lands on it or misses.
    vec3 color = min(textureLod(uScene, uv, 0.0).rgb, vec3(24.0));
    fragmentColor = vec4(color * sure, sure);
}
