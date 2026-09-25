// Shared by Enhanced mode's passes (Enhanced.cpp): the scene's depth buffer
// turned back into points and surfaces in view space (the camera at the
// origin, looking down -z), which the passes march their rays through.

uniform sampler2D uDepth;         // the resolved scene depth, full resolution, nearest
uniform mat4 uProjection;
uniform mat4 uInverseProjection;
uniform vec2 uFullSize;           // the scene's size in pixels

// How far in front of the camera (metres) a depth-buffer value lies.
float viewDepth(float depth)
{
    return uProjection[3][2] / (depth * 2.0 - 1.0 + uProjection[2][2]);
}

vec3 viewPosition(vec2 uv, float depth)
{
    vec4 point = uInverseProjection * vec4(vec3(uv, depth) * 2.0 - 1.0, 1.0);
    return point.xyz / point.w;
}

vec3 viewPositionAt(ivec2 texel)
{
    return viewPosition((vec2(texel) + 0.5) / uFullSize, texelFetch(uDepth, texel, 0).r);
}

// The surface's normal from its neighbours' depths. On each axis it takes
// the neighbour nearer in depth, so at an object's edge the normal is the
// object's own, not a slope down to whatever lies behind it.
vec3 viewNormalAt(ivec2 texel, vec3 centre)
{
    ivec2 last = ivec2(uFullSize) - 1;
    vec3 left = viewPositionAt(clamp(texel - ivec2(1, 0), ivec2(0), last));
    vec3 right = viewPositionAt(clamp(texel + ivec2(1, 0), ivec2(0), last));
    vec3 below = viewPositionAt(clamp(texel - ivec2(0, 1), ivec2(0), last));
    vec3 above = viewPositionAt(clamp(texel + ivec2(0, 1), ivec2(0), last));
    vec3 across = abs(right.z - centre.z) < abs(centre.z - left.z) ? right - centre : centre - left;
    vec3 upward = abs(above.z - centre.z) < abs(centre.z - below.z) ? above - centre : centre - below;
    return normalize(cross(across, upward));
}

// Where a point in view space lands on the screen (0..1).
vec2 screenPoint(vec3 point)
{
    vec4 clip = uProjection * vec4(point, 1.0);
    return clip.xy / clip.w * 0.5 + 0.5;
}

bool onScreen(vec2 uv)
{
    return all(greaterThanEqual(uv, vec2(0.0))) && all(lessThanEqual(uv, vec2(1.0)));
}

// A different value at every pixel in a pattern with no visible structure
// (interleaved gradient noise): it staggers where rays start, and the
// upsample and the image's own detail hide what is left of it.
float pixelNoise(vec2 pixel)
{
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}
