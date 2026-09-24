#version 330 core

in vec3 vWorldPosition;
in vec3 vNormal;
in vec2 vTexCoord;
in vec3 vGouraudDiffuse;
in vec3 vGouraudSpecular;
in vec4 vColor;
flat in float vSeed;
flat in float vGlow;

uniform sampler2D uDiffuseTexture;

// Lab 4 pairs a diffuse map with a specular map, exactly as the lab's
// Material struct does. Objects without a specular map bind a white texture,
// which reduces to a plain uniform highlight.
uniform sampler2D uSpecularTexture;
uniform vec3 uBaseColor;
uniform vec3 uEmissiveColor;

// Emissive colours are written in the source as ordinary 0..1 colours. In the
// HDR pipeline they are scaled up so lamps, lenses and screens are brighter
// than lit surfaces and pick up bloom.
uniform float uEmissiveStrength;

// 1 for a back-lit picture (a billboard at night): the glow takes the colour
// of the texture instead of being one flat colour.
uniform float uEmissiveTextured;

// Leaf cards: texels whose alpha is below the cut-off are not drawn (0 for
// everything solid).
uniform float uAlphaCutoff;

// Building walls. Their texture coordinates count window bays across and
// storeys up, so every cell of the grid gets a window: uFacade holds its
// width and height (as shares of the cell), its centre height in the cell,
// and 1 to switch the pattern on. uFacadeGlass is the glass by day, and
// uWindowLight the share of rooms lit (0 by day). The vertex alpha is the
// building's own seed, so no two buildings light the same rooms.
uniform vec4 uFacade;
uniform vec3 uFacadeGlass;
uniform float uWindowLight;

// Extra haze for the far skyline, which would otherwise stand out sharp
// against the sky on a clear day: 0 for everything else.
uniform float uHaze;

// Hemisphere ambient: sky light from above, bounce light from the ground.
uniform vec3 uAmbientSky;
uniform vec3 uAmbientGround;
uniform vec3 uLightDirection;
uniform vec3 uLightColor;

// Lab 3 completes the illumination model with a spot light: a point light
// whose intensity is gated by the angle between the fragment direction and the
// axis of the cone. Both cut-off angles are stored as cosines so the test is a
// dot product with no inverse trigonometry per fragment.
uniform vec3 uSpotPosition;
uniform vec3 uSpotDirection;
uniform vec3 uSpotColor;
uniform float uSpotCutOff;
uniform float uSpotOuterCutOff;
uniform vec3 uViewPosition;
uniform float uShininess;

#include "lights.glsl"
uniform int uShadingMode;

#include "atmosphere.glsl"

out vec4 fragmentColor;

float hash12(vec2 p)
{
    vec3 q = fract(vec3(p.xyx) * 0.1031);
    q += dot(q, q.yzx + 33.33);
    return fract((q.x + q.y) * q.z);
}

// Windows on a wall: returns the glow of lit rooms, and turns the wall's
// albedo and highlight into glass where a window is.
vec3 facadeWindows(inout vec3 albedo, inout vec3 specularMap)
{
    vec2 grid = vTexCoord;
    vec2 cell = floor(grid);
    vec2 inCell = fract(grid);
    vec2 halfSize = 0.5 * uFacade.xy;
    vec2 centre = vec2(0.5, uFacade.z);
    vec2 pixel = max(fwidth(grid), vec2(1e-4));
    // Anti-aliased window rectangle...
    vec2 inside = clamp((halfSize - abs(inCell - centre)) / pixel + 0.5, 0.0, 1.0);
    float window = inside.x * inside.y;
    // A pale frame and sill round each window.
    vec2 framed = clamp((halfSize + vec2(0.035, 0.05) - abs(inCell - centre)) / pixel + 0.5, 0.0, 1.0);
    float frame = framed.x * framed.y - window;
    // Both fade to their average where the grid is finer than a pixel, so
    // distant buildings do not shimmer.
    float blur = smoothstep(0.18, 0.5, max(pixel.x, pixel.y));
    float area = uFacade.x * uFacade.y;
    window = mix(window, area, blur);
    frame *= 1.0 - blur;
    albedo = mix(albedo, vec3(0.62, 0.61, 0.58), frame * 0.8);

    float seed = floor(vSeed * 255.0 + 0.5);
    float pick = hash12(cell + seed);
    float tone = hash12(cell * 1.7 + seed + 11.0);
    // By day the panes differ a little (blinds, curtains, reflections).
    vec3 glass = pow(max(uFacadeGlass, vec3(0.0)), vec3(2.2)) * (0.65 + 0.7 * tone);
    albedo = mix(albedo, glass, window);
    specularMap = mix(specularMap * 0.25, vec3(1.0), window);

    // At night some rooms are lit, mostly warm, a few cool (a television).
    float lit = mix(step(pick, uWindowLight), uWindowLight, blur);
    vec3 room = mix(vec3(1.0, 0.62, 0.30), vec3(0.55, 0.70, 1.0), step(0.84, tone)) * (0.45 + 0.55 * hash12(cell + seed + 5.0));
    room = mix(room, vec3(0.72, 0.52, 0.30), blur);
    // Far away a lit wall is a sprinkle of small lights, not a glowing slab.
    return room * window * lit * mix(0.42, 0.22, blur);
}

vec3 illuminate(vec3 normal, vec3 albedo, vec3 specularMap)
{
    vec3 lightDirection = normalize(-uLightDirection);
    vec3 viewDirection = normalize(uViewPosition - vWorldPosition);
    vec3 reflectionDirection = reflect(-lightDirection, normal);

    float diffuse = max(dot(normal, lightDirection), 0.0);
    float specular = pow(max(dot(viewDirection, reflectionDirection), 0.0), uShininess);
    vec3 ambient = mix(uAmbientGround, uAmbientSky, 0.5 + 0.5 * normal.y);
    vec3 diffuseLighting = ambient + uLightColor * diffuse;
    vec3 specularLighting = uLightColor * specular * 0.35;

    // Street lamps (Lab 3 point lights), per fragment for Phong shading.
    addPointLights(vWorldPosition, normal, viewDirection, uShininess, diffuseLighting, specularLighting);

    // Spot light.
    {
        vec3 toSpot = uSpotPosition - vWorldPosition;
        float spotDistance = length(toSpot);
        vec3 spotDirection = toSpot / max(spotDistance, 0.0001);

        float theta = dot(spotDirection, normalize(-uSpotDirection));
        float cone = clamp(
            (theta - uSpotOuterCutOff) / max(uSpotCutOff - uSpotOuterCutOff, 0.0001), 0.0, 1.0);

        // A floodlight is a long-throw fitting, so it uses much gentler
        // attenuation constants than the short street lamps above.
        float spotAttenuation = cone /
            (1.0 + 0.014 * spotDistance + 0.0007 * spotDistance * spotDistance);

        float spotDiffuse = max(dot(normal, spotDirection), 0.0);
        vec3 spotReflection = reflect(-spotDirection, normal);
        float spotSpecular = pow(max(dot(viewDirection, spotReflection), 0.0), uShininess);

        diffuseLighting += uSpotColor * spotDiffuse * spotAttenuation;
        specularLighting += uSpotColor * spotSpecular * spotAttenuation * 0.5;
    }

    return albedo * diffuseLighting + specularMap * specularLighting;
}

void main()
{
    // Colours in the source were chosen by eye on a monitor, so they are
    // gamma-encoded values. Lighting maths must happen in linear light, so they
    // are decoded here. Textures marked sRGB are decoded by the GPU itself.
    vec4 texel = texture(uDiffuseTexture, vTexCoord);
    if (uAlphaCutoff > 0.0 && texel.a < uAlphaCutoff)
        discard;
    vec3 paint = pow(max(vColor.rgb, vec3(0.0)), vec3(2.2));
    vec3 baseColor = pow(max(uBaseColor, vec3(0.0)), vec3(2.2)) * paint;
    vec3 albedo = texel.rgb * baseColor;
    vec3 specularMap = texture(uSpecularTexture, vTexCoord).rgb;
    vec3 windowGlow = vec3(0.0);
    if (uFacade.w > 0.5)
        windowGlow = facadeWindows(albedo, specularMap);
    vec3 result;

    if (uShadingMode == 1)
    {
        result = albedo * vGouraudDiffuse + specularMap * vGouraudSpecular;
    }
    else
    {
        vec3 normal = normalize(vNormal);
        if (uShadingMode == 0)
        {
            normal = normalize(cross(dFdx(vWorldPosition), dFdy(vWorldPosition)));
            if (dot(normal, vNormal) < 0.0)
                normal = -normal;
        }
        result = illuminate(normal, albedo, specularMap);
    }

    result += uEmissiveColor * uEmissiveStrength * mix(vec3(1.0), texel.rgb * paint, uEmissiveTextured);
    result += windowGlow;
    result += paint * vGlow * uEmissiveStrength;
    result = applyFog(result, vWorldPosition, uViewPosition);
    if (uHaze > 0.0)
    {
        vec3 ray = vWorldPosition - uViewPosition;
        float haze = 1.0 - exp(-uHaze * length(ray) / 420.0);
        result = mix(result, atmosphereColor(normalize(ray)), haze);
    }
    fragmentColor = vec4(result, 1.0);
}
