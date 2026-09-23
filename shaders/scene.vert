#version 330 core

layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;

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

// Ripple animation for the fountain water. Zero for every other object, so a
// single extra uniform buys the whole effect without a second shader.
uniform float uWaveAmplitude;
uniform float uTime;

out vec3 vWorldPosition;
out vec3 vNormal;
out vec2 vTexCoord;
out vec3 vGouraudDiffuse;
out vec3 vGouraudSpecular;

void main()
{
    vec3 localPosition = aPosition;
    if (uWaveAmplitude > 0.0)
    {
        float ringDistance = length(localPosition.xz);
        localPosition.y += uWaveAmplitude * sin(22.0 * ringDistance - 3.4 * uTime);
    }

    vec4 worldPosition = uModel * vec4(localPosition, 1.0);
    vec3 normal = normalize(uNormalMatrix * aNormal);
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

    for (int index = 0; index < uPointLightCount; ++index)
    {
        vec3 toLight = uPointLightPositions[index] - worldPosition.xyz;
        float distanceToLight = length(toLight);
        vec3 pointDirection = normalize(toLight);
        float attenuation = 1.0 / (1.0 + 0.09 * distanceToLight + 0.032 * distanceToLight * distanceToLight);
        float pointDiffuse = max(dot(normal, pointDirection), 0.0);
        vec3 pointReflection = reflect(-pointDirection, normal);
        float pointSpecular = pow(max(dot(viewDirection, pointReflection), 0.0), uShininess);
        totalDiffuse += uPointLightColors[index] * pointDiffuse * attenuation;
        totalSpecular += uPointLightColors[index] * pointSpecular * attenuation * 0.45;
    }


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

    vGouraudDiffuse = totalDiffuse;
    vGouraudSpecular = totalSpecular;

    gl_Position = uProjection * uView * worldPosition;
}
