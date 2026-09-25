// Shadows of the sun (or the moon), from two depth maps seen from the light
// (ShadowMap.cpp):
//   near: a sharp map of the 64 m round the camera, following it in whole
//         texels so shadow edges never crawl as it moves;
//   city: one map over the whole city, which only changes as the light moves.
// A point uses the near map where it has one and the city map elsewhere,
// blending across the near map's edge so the change never shows.

uniform sampler2DShadow uShadowNear;
uniform sampler2DShadow uShadowCity;
uniform mat4 uShadowNearMatrix;   // world -> 0..1 map coordinates and depth
uniform mat4 uShadowCityMatrix;
// x, y: one texel of the near and city maps, in metres on the ground;
// z, w: one texel as a share of the near and city maps.
uniform vec4 uShadowTexel;
// x: maps in use (0 none, 1 the city map only, 2 both); y: how soft the
// edges are, in texels (wider in moonlight); z: how dark a shadow is (1 full).
uniform vec4 uShadowSettings;

// Percentage-closer filtering: 16 taps of the hardware's own 2x2 depth test,
// on a grid fixed in the map, so the soft edge is smooth and does not shimmer.
float filteredShadow(sampler2DShadow map, vec3 coordinates, float texel, float softness)
{
    float lit = 0.0;
    float spacing = texel * softness;
    for (int y = 0; y < 4; ++y)
    {
        for (int x = 0; x < 4; ++x)
        {
            vec2 offset = (vec2(x, y) - 1.5) * spacing;
            lit += texture(map, vec3(coordinates.xy + offset, coordinates.z));
        }
    }
    return lit / 16.0;
}

#ifdef SOFT_SHADOWS
// Enhanced mode's soft shadows (percentage-closer soft shadows, PCSS) in
// the near map. The sun is a disc, not a point, so a shadow is sharp where
// it touches its caster and widens the further the ground lies below it.
// A first search finds how far above the point the things casting on it
// are; the edge is then filtered that wide.
uniform sampler2D uShadowNearDepth;   // the near map again, read as plain depth
uniform float uShadowNearRange;       // metres from depth 0 to depth 1 in the near map
uniform float uSunSpread;             // metres of soft edge per metre from caster to ground
// The widest edge, in texels. A low sun stretches every texel of the map
// far along the ground (seven times at 8 degrees), so then the edge may be
// no wider than the plain filter's, or leaf shadows smear into haze.
uniform float uSoftMaxTexels;

const vec2 poissonDisc[16] = vec2[](
    vec2(-0.94201624, -0.39906216), vec2(0.94558609, -0.76890725), vec2(-0.09418410, -0.92938870),
    vec2(0.34495938, 0.29387760), vec2(-0.91588581, 0.45771432), vec2(-0.81544232, -0.87912464),
    vec2(-0.38277543, 0.27676845), vec2(0.97484398, 0.75648379), vec2(0.44323325, -0.97511554),
    vec2(0.53742981, -0.47373420), vec2(-0.26496911, -0.41893023), vec2(0.79197514, 0.19090188),
    vec2(-0.24188840, 0.99706507), vec2(-0.81409955, 0.91437590), vec2(0.19984126, 0.78641367),
    vec2(0.14383161, -0.14100790));

// The taps lie in the same pattern at every pixel: turning it per pixel
// hides its shape but leaves grain along every soft edge, which nothing
// later in the frame would smooth away. Each tap is the hardware's own 2x2
// filtered test, so the fixed pattern blends into an even gradient.
float softShadow(vec3 coordinates, float texel, float softness)
{
    // The casters: the depths nearer the light than the point, at the point
    // itself and round it, within the widest edge a shadow here could have.
    // A leaf's shadow can be smaller than the search: when none of the
    // taps finds a caster, the plain filter decides.
    float searchTexels = uSoftMaxTexels;
    float casterDepth = 0.0;
    float casters = 0.0;
    for (int index = 0; index < 9; ++index)
    {
        vec2 offset = index == 8 ? vec2(0.0) : poissonDisc[index * 2] * searchTexels * texel;
        float depth = texture(uShadowNearDepth, coordinates.xy + offset).r;
        if (depth < coordinates.z)
        {
            casterDepth += depth;
            casters += 1.0;
        }
    }
    if (casters == 0.0)
        return filteredShadow(uShadowNear, coordinates, texel, softness * 1.6);

    // The edge widens with the gap from caster to point, never narrower
    // than the plain filter's.
    float gap = (coordinates.z - casterDepth / casters) * uShadowNearRange;
    float edgeTexels = clamp(gap * uSunSpread / uShadowTexel.x, 1.6 * softness, max(searchTexels, 1.6 * softness));
    float lit = 0.0;
    for (int index = 0; index < 16; ++index)
    {
        vec2 offset = poissonDisc[index] * edgeTexels * texel;
        lit += texture(uShadowNear, vec3(coordinates.xy + offset, coordinates.z));
    }
    return lit / 16.0;
}
#endif

// How far inside a map's square a point is: 1 well inside, 0 at the edge
// (or outside it, or beyond its depth).
float insideMap(vec3 coordinates, float band)
{
    vec2 edge = min(coordinates.xy, 1.0 - coordinates.xy);
    float inside = clamp(min(edge.x, edge.y) / band, 0.0, 1.0);
    return coordinates.z < 1.0 ? inside : 0.0;
}

// 1 where the light reaches the point, 0 in full shadow. The point is first
// pushed out along its normal by about a texel (more where the light grazes
// the surface), which keeps a lit surface from shadowing itself (acne)
// without lifting shadows off their casters (peter-panning).
float lightVisibility(vec3 worldPosition, vec3 normal, vec3 towardsLight)
{
    if (uShadowSettings.x < 0.5)
        return 1.0;
    float facing = dot(normal, towardsLight);
    if (!(facing > 0.0))
        return 1.0;   // facing away: no direct light to take away
    float slope = 1.0 - facing;
    float softness = uShadowSettings.y;

    float nearWeight = 0.0;
    float nearShadow = 1.0;
    if (uShadowSettings.x > 1.5)
    {
        vec3 nearPoint = worldPosition + normal * uShadowTexel.x * (1.0 + 2.0 * slope);
        vec3 near = (uShadowNearMatrix * vec4(nearPoint, 1.0)).xyz;
        nearWeight = insideMap(near, 0.12);
        // Softer in texels in the near map, so the edge is about as wide in
        // metres where the two maps meet.
        if (nearWeight > 0.0)
#ifdef SOFT_SHADOWS
            nearShadow = softShadow(near, uShadowTexel.z, softness);
#else
            nearShadow = filteredShadow(uShadowNear, near, uShadowTexel.z, softness * 1.6);
#endif
    }

    float cityShadow = 1.0;
    if (nearWeight < 1.0)
    {
        vec3 cityPoint = worldPosition + normal * uShadowTexel.y * (1.0 + 2.0 * slope);
        vec3 city = (uShadowCityMatrix * vec4(cityPoint, 1.0)).xyz;
        float cityWeight = insideMap(city, 0.02);
        if (cityWeight > 0.0)
            cityShadow = mix(1.0, filteredShadow(uShadowCity, city, uShadowTexel.w, softness), cityWeight);
    }
    float visible = mix(cityShadow, nearShadow, nearWeight);
    return mix(1.0, visible, uShadowSettings.z);
}
