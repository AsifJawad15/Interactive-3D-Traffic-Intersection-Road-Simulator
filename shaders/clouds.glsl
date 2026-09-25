// The cloud layer: a flat sheet 1.5 km up whose thickness at each point is
// fractal noise (fbm), drifting with the wind. Shared by the sky, which draws
// it, and by the scene, whose sunlight it shades, so every cloud's shadow on
// the ground lies exactly under that cloud as seen from the sun.

uniform float uCloudCover;   // 0 a few wisps .. 1 overcast
uniform vec2 uCloudOffset;   // how far the wind has carried the clouds, metres

const float cloudBase = 1500.0;

float cloudHash(vec2 p)
{
    vec3 q = fract(vec3(p.xyx) * 0.1031);
    q += dot(q, q.yzx + 33.33);
    return fract((q.x + q.y) * q.z);
}

// Value noise: random heights on a grid, blended smoothly between.
float cloudNoise(vec2 p)
{
    vec2 cell = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = cloudHash(cell);
    float b = cloudHash(cell + vec2(1.0, 0.0));
    float c = cloudHash(cell + vec2(0.0, 1.0));
    float d = cloudHash(cell + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

// Octaves of finer and fainter noise, each turned a little so no grid shows.
// The result stays within 0..1, most of it near 0.5.
float cloudFbm(vec2 p, int octaves)
{
    const mat2 turn = mat2(1.6, 1.2, -1.2, 1.6);
    float sum = 0.0;
    float weight = 0.5;
    float total = 0.0;
    for (int octave = 0; octave < octaves; ++octave)
    {
        sum += weight * cloudNoise(p);
        total += weight;
        p = turn * p + vec2(17.3, -9.1);
        weight *= 0.5;
    }
    return sum / total;
}

// How thick the cloud is over a point of the sheet (0 clear sky .. 1 thick).
// More cover lowers the threshold the noise must pass to become cloud.
float cloudDensity(vec2 xz, int octaves)
{
    vec2 p = (xz + uCloudOffset) * (1.0 / 520.0);
    float n = cloudFbm(p, octaves);
    float threshold = mix(0.62, 0.12, uCloudCover);
    return smoothstep(threshold, threshold + 0.26, n);
}

// Where the ray from a point towards the light meets the sheet.
vec2 cloudSheetPoint(vec3 from, vec3 towards)
{
    return from.xz + towards.xz * ((cloudBase - from.y) / max(towards.y, 0.02));
}
