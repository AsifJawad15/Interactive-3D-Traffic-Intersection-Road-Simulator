// Everything the city's surfaces are drawn with, shared by two programs:
// scene.frag (dry, clear or nearly clear sky) and scene_wet.frag, which
// defines WET_WORLD and adds the weather: cloud shadows, wet surfaces,
// puddles and the rings where drops land. The weather's code makes the
// shader bigger, and a bigger shader runs slower even where its branches
// are skipped, so the dry program leaves it out (Scene.cpp picks one).
// Enhanced mode's soft shadows (SOFT_SHADOWS, shadows.glsl) make two more:
// scene_soft.frag and scene_wet_soft.frag.

in vec3 vWorldPosition;
in vec3 vNormal;
in vec2 vTexCoord;
in vec3 vGouraudDiffuse;
in vec3 vGouraudSpecular;
in vec3 vGouraudSun;
in vec3 vGouraudSunSpecular;
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
#include "shadows.glsl"
#ifdef WET_WORLD
#include "clouds.glsl"

// The weather (Weather.cpp). uCloudShadow: how much a cloud overhead takes
// off the sunlight (patches under broken cloud, 0 when overcast all over).
// uWetness and uPuddles build up in the rain and dry after it; uRain is how
// hard it rains now, and uRainClock the clock of the rings where drops land.
// uPuddleSurface is 1 for the roads and paving, the only places puddles lie,
// and uCloudTone the colour of the cloud sheet that puddles reflect.
uniform float uCloudShadow;
uniform float uWetness;
uniform float uPuddles;
uniform float uRain;
uniform float uRainClock;
uniform float uPuddleSurface;
uniform vec3 uCloudTone;
#endif

// The colour, and in alpha how much of this point is a mirror: Enhanced
// mode's reflections (ssr.frag) read it, and nothing else does.
out vec4 fragmentColor;

// How much of the pixel is window glass, set by facadeWindows().
float windowGlass = 0.0;

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
    windowGlass = window;

    // At night some rooms are lit, mostly warm, a few cool (a television).
    float lit = mix(step(pick, uWindowLight), uWindowLight, blur);
    vec3 room = mix(vec3(1.0, 0.62, 0.30), vec3(0.55, 0.70, 1.0), step(0.84, tone)) * (0.45 + 0.55 * hash12(cell + seed + 5.0));
    room = mix(room, vec3(0.72, 0.52, 0.30), blur);
    // Far away a lit wall is a sprinkle of small lights, not a glowing slab.
    return room * window * lit * mix(0.42, 0.22, blur);
}

#ifdef WET_WORLD
// How much of the sun (or moon) the clouds let through to this point: the
// shadow of the cloud that stands between it and the light.
float cloudShade(vec3 towardsLight)
{
    if (uCloudShadow <= 0.0)
        return 1.0;
    // A cloud's shadow has a firmer edge than the cloud's soft outline:
    // only its thicker part keeps the sun off.
    float density = cloudDensity(cloudSheetPoint(vWorldPosition, towardsLight), 3);
    return 1.0 - uCloudShadow * smoothstep(0.25, 0.7, density);
}

// Rings spreading where raindrops land: the ground is cut into cells about
// 40 cm across (two such grids, offset), and in each cycle some cells, more
// in heavier rain, catch one drop at a random spot and moment. Returns how
// the ring tilts the surface (world x and z).
vec2 rippleSlope(vec2 xz)
{
    vec2 slope = vec2(0.0);
    for (int layer = 0; layer < 2; ++layer)
    {
        vec2 p = xz * 2.6 + float(layer) * vec2(0.37, 0.71);
        vec2 cell = floor(p);
        float seed = cloudHash(cell + float(layer) * 13.1);
        float clock = uRainClock * 1.25 + seed * 5.3;
        float cycle = floor(clock);
        float age = fract(clock);
        if (cloudHash(cell + cycle * 1.37) > 0.08 + 0.3 * uRain)
            continue;
        vec2 centre = 0.3 + 0.4 * vec2(seed, cloudHash(cell + 7.7));
        vec2 offset = fract(p) - centre;
        float distanceFromDrop = length(offset);
        float x = distanceFromDrop - age * 0.3;
        const float width = 0.045;
        float height = exp(-(x * x) / (width * width)) * (1.0 - age) * (1.0 - age);
        slope += offset / max(distanceFromDrop, 1e-4) * (-2.0 * x / (width * width)) * height;
    }
    return slope;
}

// The sky seen in a puddle or on a wet road: the sky's own colour, turned
// towards the cloud sheet's under a cloudy sky.
vec3 skyReflection(vec3 direction)
{
    direction.y = max(direction.y, 0.02);
    return mix(atmosphereColor(normalize(direction)), uCloudTone, 0.85 * smoothstep(0.35, 0.9, uCloudCover));
}
#else
float cloudShade(vec3 towardsLight)
{
    return 1.0;
}
#endif

// Returns the lit colour; `highlights` is the part of it that is specular.
vec3 illuminate(vec3 normal, vec3 albedo, vec3 specularMap, float shininess, out vec3 highlights)
{
    vec3 lightDirection = normalize(-uLightDirection);
    vec3 viewDirection = normalize(uViewPosition - vWorldPosition);
    vec3 reflectionDirection = reflect(-lightDirection, normal);

    float diffuse = max(dot(normal, lightDirection), 0.0);
    float specular = pow(max(dot(viewDirection, reflectionDirection), 0.0), shininess);
    // The sun (or the moon) reaches this point only where nothing stands
    // between them: the shadow map says how much of it does, and the clouds
    // take their share on top.
    float visible = lightVisibility(vWorldPosition, normal, lightDirection) * cloudShade(lightDirection);
    vec3 ambient = mix(uAmbientGround, uAmbientSky, 0.5 + 0.5 * normal.y);
    vec3 diffuseLighting = ambient + uLightColor * diffuse * visible;
    vec3 specularLighting = uLightColor * specular * 0.35 * visible;

    // Street lamps (Lab 3 point lights) and headlights, per fragment for
    // Phong shading.
    addPointLights(vWorldPosition, normal, viewDirection, shininess, diffuseLighting, specularLighting);
    addSpotLights(vWorldPosition, normal, viewDirection, shininess, diffuseLighting, specularLighting);

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
        float spotSpecular = pow(max(dot(viewDirection, spotReflection), 0.0), shininess);

        diffuseLighting += uSpotColor * spotDiffuse * spotAttenuation;
        specularLighting += uSpotColor * spotSpecular * spotAttenuation * 0.5;
    }

    highlights = specularMap * specularLighting;
    return albedo * diffuseLighting + highlights;
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
    vec3 highlights;

    // Rain: wet surfaces are darker (water fills the pores) and glossier,
    // the more so the more they face the sky; walls only a little. Puddles
    // lie in the dips of the roads and paving: nearly black, flat and
    // mirror-like. Near the camera, rings spread where drops land.
    vec3 surfaceNormal = normalize(vNormal);
    vec3 viewDirection = normalize(uViewPosition - vWorldPosition);
    float wet = 0.0;
    float puddle = 0.0;
    vec2 ripple = vec2(0.0);
    float shininess = uShininess;
    // Wet highlights are sharper and stronger. Only per-pixel lighting can
    // sharpen a highlight; Gouraud's are worked out at the vertices with
    // the dry shininess, so they keep their dry strength too.
    float specularGain = 1.0;
    // How glossy the material can get when wet: the specular map says so
    // (grass and leaves carry a matte one and stay dull).
    float gloss = clamp(dot(specularMap, vec3(0.47)), 0.1, 1.0);
#ifdef WET_WORLD
    if (uWetness > 0.0 && uHaze <= 0.0)
    {
        float upward = smoothstep(0.2, 0.9, surfaceNormal.y);
        wet = uWetness * mix(0.3, 1.0, upward);
        if (uPuddleSurface > 0.5 && uPuddles > 0.0 && surfaceNormal.y > 0.9)
        {
            float dip = cloudFbm(vWorldPosition.xz * 0.16 + vec2(3.1, 7.9), 3);
            float level = mix(0.80, 0.47, uPuddles);
            puddle = smoothstep(level, level + 0.05, dip);
        }
        float distanceToEye = length(uViewPosition - vWorldPosition);
        if (uRain > 0.0 && surfaceNormal.y > 0.75 && distanceToEye < 20.0)
            ripple = rippleSlope(vWorldPosition.xz) * (1.0 - smoothstep(7.0, 20.0, distanceToEye)) * uRain;
        albedo *= mix(1.0, 0.55, wet) * mix(1.0, 0.3, puddle);
        shininess = mix(uShininess, max(uShininess, 110.0), wet * gloss);
        shininess = mix(shininess, 600.0, puddle);
        specularGain = 1.0 + 2.0 * wet * gloss + 3.0 * puddle;
    }
#endif

    if (uShadingMode == 1)
    {
        // Lit at the vertices, but shadowed per pixel: a shadow edge
        // across a large triangle is still sharp.
        vec3 towardsLight = normalize(-uLightDirection);
        float visible = lightVisibility(vWorldPosition, normalize(vNormal), towardsLight) * cloudShade(towardsLight);
        highlights = specularMap * (vGouraudSpecular + vGouraudSunSpecular * visible);
        result = albedo * (vGouraudDiffuse + vGouraudSun * visible) + highlights;
    }
    else
    {
        vec3 normal = normalize(vNormal);
        if (uShadingMode == 0)
        {
            // A sliver of a triangle (a leaf seen edge-on) can have no
            // measurable face at all; it keeps its smooth normal instead of
            // a normal of NaN, which would light it NaN and bloom white.
            vec3 face = cross(dFdx(vWorldPosition), dFdy(vWorldPosition));
            if (dot(face, face) > 1.0e-14)
            {
                normal = normalize(face);
                if (dot(normal, vNormal) < 0.0)
                    normal = -normal;
            }
        }
        if (puddle > 0.0)
            normal = normalize(mix(normal, vec3(0.0, 1.0, 0.0), puddle));
        if (ripple != vec2(0.0))
            normal = normalize(normal - vec3(ripple.x, 0.0, ripple.y) * 0.012 * mix(0.2, 1.0, puddle));
        result = illuminate(normal, albedo, specularMap * specularGain, shininess, highlights);
    }

    // The sky in the wet: a sheen on wet ground seen at a low angle, and a
    // near mirror in the puddles, rippled where drops land. The lamps' and
    // the sun's highlights stay on top: they are what a puddle mirrors most.
    float mirror = 0.0;
#ifdef WET_WORLD
    if (wet > 0.0)
    {
        // Rings show clearly on a puddle, faintly on the rest of the road.
        vec3 reflectNormal = normalize(mix(surfaceNormal, vec3(0.0, 1.0, 0.0), puddle) -
                                       vec3(ripple.x, 0.0, ripple.y) * 0.025 * mix(0.15, 1.0, puddle));
        float facing = max(dot(reflectNormal, viewDirection), 0.0);
        // Water reflects 2 % looking straight down, nearly all of it at a
        // grazing angle (Schlick's approximation of Fresnel). A wet road
        // is rough water, so it mirrors much less than a puddle.
        float fresnel = 0.02 + 0.98 * pow(1.0 - facing, 5.0);
        vec3 reflection = skyReflection(reflect(-viewDirection, reflectNormal)) * 0.8;
        mirror = puddle * fresnel * 0.9 + (1.0 - puddle) * wet * gloss * fresnel * 0.2 *
                 smoothstep(0.5, 0.95, surfaceNormal.y);
        mirror = clamp(mirror, 0.0, 0.9);
        result = mix(result, reflection, mirror) + highlights * mirror;
    }
#endif

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

    // The mirror: polished paint and glass a little, more at a grazing
    // angle (Fresnel again), window panes more, and wet ground as much as it
    // mirrors the sky above. The far skyline, lost in the haze, none.
    float grazing = pow(1.0 - max(dot(surfaceNormal, viewDirection), 0.0), 5.0);
    float polish = smoothstep(40.0, 120.0, uShininess) * gloss;
    float reflectivity = max(polish * (0.06 + 0.5 * grazing), windowGlass * (0.10 + 0.6 * grazing));
    reflectivity = uHaze > 0.0 ? 0.0 : max(reflectivity, mirror);
    fragmentColor = vec4(result, reflectivity);
}
