#version 330 core

#include "placement.glsl"

uniform mat4 uView;
uniform mat4 uProjection;
uniform vec2 uUvScale;

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

out vec3 vWorldPosition;
out vec3 vNormal;
out vec2 vTexCoord;
// Gouraud: the lighting worked out at the vertices. The sun (or moon) is kept
// apart from the rest, so the fragment shader can still put it in shadow.
out vec3 vGouraudDiffuse;
out vec3 vGouraudSpecular;
out vec3 vGouraudSun;
out vec3 vGouraudSunSpecular;
out vec4 vColor;
// The same alpha, not interpolated: a building's window seed must be exactly
// the same all over its wall, or the hash that picks lit rooms turns to noise.
flat out float vSeed;
flat out float vGlow;

void main()
{
    Placed placed = placeVertex();
    vec4 worldPosition = placed.world;
    vec3 normal = placed.normal;
    vec3 lightDirection = normalize(-uLightDirection);
    vec3 viewDirection = normalize(uViewPosition - worldPosition.xyz);
    vec3 reflectionDirection = reflect(-lightDirection, normal);

    float diffuse = max(dot(normal, lightDirection), 0.0);
    float specular = pow(max(dot(viewDirection, reflectionDirection), 0.0), uShininess);

    vWorldPosition = worldPosition.xyz;
    vNormal = normal;
    vTexCoord = aTexCoord * uUvScale;
    vec3 ambient = mix(uAmbientGround, uAmbientSky, 0.5 + 0.5 * normal.y);
    vec3 totalDiffuse = ambient;
    vec3 totalSpecular = vec3(0.0);

    // Street lamps (Lab 3 point lights) and headlights, per vertex for
    // Gouraud shading.
    addPointLights(worldPosition.xyz, normal, viewDirection, uShininess, totalDiffuse, totalSpecular);
    addSpotLights(worldPosition.xyz, normal, viewDirection, uShininess, totalDiffuse, totalSpecular);

    // Spot light.
    {
        vec3 toSpot = uSpotPosition - worldPosition.xyz;
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

        totalDiffuse += uSpotColor * spotDiffuse * spotAttenuation;
        totalSpecular += uSpotColor * spotSpecular * spotAttenuation * 0.5;
    }

    vColor = placed.paint;
    vSeed = placed.paint.a;
    vGlow = placed.glow;
    vGouraudDiffuse = totalDiffuse;
    vGouraudSpecular = totalSpecular;
    vGouraudSun = uLightColor * diffuse;
    vGouraudSunSpecular = uLightColor * specular * 0.35;

    gl_Position = uProjection * uView * worldPosition;
}
