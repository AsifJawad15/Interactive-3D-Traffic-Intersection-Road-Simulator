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
            nearShadow = filteredShadow(uShadowNear, near, uShadowTexel.z, softness * 1.6);
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
