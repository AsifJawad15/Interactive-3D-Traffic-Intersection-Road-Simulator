#version 330 core

in vec3 vWorldPosition;
in vec3 vNormal;
in vec2 vTexCoord;
in vec3 vGouraudDiffuse;
in vec3 vGouraudSpecular;

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

// Hemisphere ambient: sky light from above, bounce light from the ground.
uniform vec3 uAmbientSky;
uniform vec3 uAmbientGround;
uniform vec3 uLightDirection;
uniform vec3 uLightColor;
uniform int uPointLightCount;
uniform vec3 uPointLightPositions[4];
uniform vec3 uPointLightColors[4];

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
uniform int uShadingMode;

#include "atmosphere.glsl"

out vec4 fragmentColor;

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

    for (int index = 0; index < uPointLightCount; ++index)
    {
        vec3 toLight = uPointLightPositions[index] - vWorldPosition;
        float distanceToLight = length(toLight);
        vec3 pointDirection = normalize(toLight);
        float attenuation = 1.0 / (1.0 + 0.09 * distanceToLight + 0.032 * distanceToLight * distanceToLight);
        float pointDiffuse = max(dot(normal, pointDirection), 0.0);
        vec3 pointReflection = reflect(-pointDirection, normal);
        float pointSpecular = pow(max(dot(viewDirection, pointReflection), 0.0), uShininess);
        diffuseLighting += uPointLightColors[index] * pointDiffuse * attenuation;
        specularLighting += uPointLightColors[index] * pointSpecular * attenuation * 0.45;
    }


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
    vec3 baseColor = pow(max(uBaseColor, vec3(0.0)), vec3(2.2));
    vec3 albedo = texture(uDiffuseTexture, vTexCoord).rgb * baseColor;
    vec3 specularMap = texture(uSpecularTexture, vTexCoord).rgb;
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

    result += uEmissiveColor * uEmissiveStrength;
    result = applyFog(result, vWorldPosition, uViewPosition);
    fragmentColor = vec4(result, 1.0);
}
