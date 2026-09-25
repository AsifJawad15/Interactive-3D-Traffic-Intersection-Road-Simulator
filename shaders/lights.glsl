// The street lamps chosen this frame by the light budget (LightManager.cpp).
// One uniform buffer, shared by the vertex shader (Gouraud) and the fragment
// shader (Phong), laid out by the std140 rules.
layout(std140) uniform PointLightBlock
{
    vec4 uPointPositionRange[32];   // xyz = position, w = range in metres
    vec4 uPointColorFade[32];       // rgb = colour (already faded), w = fade
    ivec4 uPointCount;              // x = number of lights in use
};

// Lab 3 attenuation, 1 / (1 + 0.09 d + 0.032 d^2), multiplied by a window that
// reaches exactly zero at the light's range. A light can then be added to or
// dropped from the budget, beyond its range, with no visible change.
void addPointLights(vec3 position, vec3 normal, vec3 viewDirection, float shininess,
                    inout vec3 diffuse, inout vec3 specular)
{
    for (int index = 0; index < uPointCount.x; ++index)
    {
        vec3 toLight = uPointPositionRange[index].xyz - position;
        float distanceToLight = length(toLight);
        float range = uPointPositionRange[index].w;
        if (distanceToLight >= range)
            continue;

        vec3 lightDirection = toLight / max(distanceToLight, 1e-4);
        float x = distanceToLight / range;
        float window = clamp(1.0 - x * x * x * x, 0.0, 1.0);
        window *= window;
        float attenuation = window /
            (1.0 + 0.09 * distanceToLight + 0.032 * distanceToLight * distanceToLight);

        vec3 color = uPointColorFade[index].rgb;
        diffuse += color * max(dot(normal, lightDirection), 0.0) * attenuation;
        vec3 reflection = reflect(-lightDirection, normal);
        specular += color * pow(max(dot(viewDirection, reflection), 0.0), shininess) * attenuation * 0.45;
    }
}

// Headlights, chosen by the same kind of budget: up to 8 cones.
layout(std140) uniform SpotLightBlock
{
    vec4 uConePositionRange[8];    // xyz = position, w = range in metres
    vec4 uConeDirectionOuter[8];   // xyz = the way the light shines, w = cos(outer angle)
    vec4 uConeColorInner[8];       // rgb = colour (already faded), w = cos(inner angle)
    ivec4 uConeCount;              // x = number of cones in use
};

// A headlight throws its light far, so it uses gentle long-throw attenuation
// (like the Lab 3 floodlight), again windowed to exactly zero at its range.
void addSpotLights(vec3 position, vec3 normal, vec3 viewDirection, float shininess,
                   inout vec3 diffuse, inout vec3 specular)
{
    for (int index = 0; index < uConeCount.x; ++index)
    {
        vec3 toLight = uConePositionRange[index].xyz - position;
        float distanceToLight = length(toLight);
        float range = uConePositionRange[index].w;
        if (distanceToLight >= range)
            continue;

        vec3 lightDirection = toLight / max(distanceToLight, 1e-4);
        float theta = dot(-lightDirection, uConeDirectionOuter[index].xyz);
        float outer = uConeDirectionOuter[index].w;
        float inner = uConeColorInner[index].w;
        float cone = clamp((theta - outer) / max(inner - outer, 1e-4), 0.0, 1.0);
        if (cone <= 0.0)
            continue;

        float x = distanceToLight / range;
        float window = clamp(1.0 - x * x * x * x, 0.0, 1.0);
        window *= window;
        float attenuation = cone * cone * window /
            (1.0 + 0.014 * distanceToLight + 0.0007 * distanceToLight * distanceToLight);

        vec3 color = uConeColorInner[index].rgb;
        diffuse += color * max(dot(normal, lightDirection), 0.0) * attenuation;
        vec3 reflection = reflect(-lightDirection, normal);
        specular += color * pow(max(dot(viewDirection, reflection), 0.0), shininess) * attenuation * 0.45;
    }
}

// How brightly the lamps and the headlights light a raindrop at `position`:
// a drop has no side facing away, so there is no angle term, only the
// distance (and the cone, for a headlight).
vec3 lightGlow(vec3 position)
{
    vec3 glow = vec3(0.0);
    for (int index = 0; index < uPointCount.x; ++index)
    {
        vec3 toLight = uPointPositionRange[index].xyz - position;
        float distanceToLight = length(toLight);
        float range = uPointPositionRange[index].w;
        if (distanceToLight >= range)
            continue;
        float x = distanceToLight / range;
        float window = clamp(1.0 - x * x * x * x, 0.0, 1.0);
        glow += uPointColorFade[index].rgb * window * window /
                (1.0 + 0.09 * distanceToLight + 0.032 * distanceToLight * distanceToLight);
    }
    for (int index = 0; index < uConeCount.x; ++index)
    {
        vec3 toLight = uConePositionRange[index].xyz - position;
        float distanceToLight = length(toLight);
        float range = uConePositionRange[index].w;
        if (distanceToLight >= range)
            continue;
        float theta = dot(-toLight / max(distanceToLight, 1e-4), uConeDirectionOuter[index].xyz);
        float outer = uConeDirectionOuter[index].w;
        float cone = clamp((theta - outer) / max(uConeColorInner[index].w - outer, 1e-4), 0.0, 1.0);
        float x = distanceToLight / range;
        float window = clamp(1.0 - x * x * x * x, 0.0, 1.0);
        glow += uConeColorInner[index].rgb * cone * cone * window * window /
                (1.0 + 0.014 * distanceToLight + 0.0007 * distanceToLight * distanceToLight);
    }
    return glow;
}
