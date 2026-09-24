#version 330 core

layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;
// Per-vertex paint (a building's colour, an awning's stripe). Its alpha is a
// per-object number: a building's lit-window seed, or how far a leaf sways.
layout (location = 3) in vec4 aColor;
// Instanced draws (the people, the walkers' signal lenses): each copy brings
// its own model matrix and colour; the colour's alpha is how much it glows.
layout (location = 4) in vec4 aInstanceModel0;
layout (location = 5) in vec4 aInstanceModel1;
layout (location = 6) in vec4 aInstanceModel2;
layout (location = 7) in vec4 aInstanceModel3;
layout (location = 8) in vec4 aInstanceColor;
uniform float uInstanced;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat3 uNormalMatrix;
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

// Ripple animation for the fountain water. Zero for every other object, so a
// single extra uniform buys the whole effect without a second shader.
uniform float uWaveAmplitude;
uniform float uTime;

// Wind in the trees: 0 for everything else. Each vertex bends by its sway
// weight (vertex alpha), trees a little out of step with each other, and the
// leaves flutter on top of the slow sway.
uniform float uSway;

out vec3 vWorldPosition;
out vec3 vNormal;
out vec2 vTexCoord;
out vec3 vGouraudDiffuse;
out vec3 vGouraudSpecular;
out vec4 vColor;
// The same alpha, not interpolated: a building's window seed must be exactly
// the same all over its wall, or the hash that picks lit rooms turns to noise.
flat out float vSeed;
flat out float vGlow;

void main()
{
    vec3 localPosition = aPosition;
    if (uWaveAmplitude > 0.0)
    {
        float ringDistance = length(localPosition.xz);
        localPosition.y += uWaveAmplitude * sin(22.0 * ringDistance - 3.4 * uTime);
    }

    mat4 model = uModel;
    mat3 normalMatrix = uNormalMatrix;
    vec4 paint = aColor;
    float glow = 0.0;
    if (uInstanced > 0.5)
    {
        // A copy may be stretched along one axis (an arm, a leg), so its
        // normals need the inverse transpose, worked out here per copy.
        model = mat4(aInstanceModel0, aInstanceModel1, aInstanceModel2, aInstanceModel3);
        normalMatrix = transpose(inverse(mat3(model)));
        paint = vec4(aColor.rgb * aInstanceColor.rgb, aColor.a);
        glow = aInstanceColor.a;
    }

    vec4 worldPosition = model * vec4(localPosition, 1.0);
    if (uSway > 0.0)
    {
        float weight = aColor.a;
        float phase = dot(worldPosition.xz, vec2(0.061, 0.047));
        vec2 bend = vec2(sin(uTime * 1.13 + phase) + 0.35 * sin(uTime * 2.37 + 1.7 * phase),
                         0.6 * cos(uTime * 0.91 + 1.3 * phase));
        float flutter = sin(uTime * 6.3 + dot(worldPosition.xyz, vec3(1.37, 1.71, 1.13)));
        worldPosition.xz += uSway * weight * (0.13 * bend + 0.025 * vec2(flutter, -flutter));
        worldPosition.y += uSway * weight * 0.02 * flutter;
    }
    vec3 normal = normalize(normalMatrix * aNormal);
    vec3 lightDirection = normalize(-uLightDirection);
    vec3 viewDirection = normalize(uViewPosition - worldPosition.xyz);
    vec3 reflectionDirection = reflect(-lightDirection, normal);

    float diffuse = max(dot(normal, lightDirection), 0.0);
    float specular = pow(max(dot(viewDirection, reflectionDirection), 0.0), uShininess);

    vWorldPosition = worldPosition.xyz;
    vNormal = normal;
    vTexCoord = aTexCoord * uUvScale;
    vec3 ambient = mix(uAmbientGround, uAmbientSky, 0.5 + 0.5 * normal.y);
    vec3 totalDiffuse = ambient + uLightColor * diffuse;
    vec3 totalSpecular = uLightColor * specular * 0.35;

    // Street lamps (Lab 3 point lights), per vertex for Gouraud shading.
    addPointLights(worldPosition.xyz, normal, viewDirection, uShininess, totalDiffuse, totalSpecular);

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

    vColor = paint;
    vSeed = paint.a;
    vGlow = glow;
    vGouraudDiffuse = totalDiffuse;
    vGouraudSpecular = totalSpecular;

    gl_Position = uProjection * uView * worldPosition;
}
