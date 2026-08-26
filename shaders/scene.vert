#version 330 core

layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat3 uNormalMatrix;
uniform vec2 uUvScale;

uniform vec3 uAmbient;
uniform vec3 uLightDirection;
uniform vec3 uLightColor;
uniform int uPointLightCount;
uniform vec3 uPointLightPositions[4];
uniform vec3 uPointLightColors[4];
uniform vec3 uViewPosition;
uniform float uShininess;

out vec3 vWorldPosition;
out vec3 vNormal;
out vec2 vTexCoord;
out vec3 vGouraudDiffuse;
out vec3 vGouraudSpecular;

void main()
{
    vec4 worldPosition = uModel * vec4(aPosition, 1.0);
    vec3 normal = normalize(uNormalMatrix * aNormal);
    vec3 lightDirection = normalize(-uLightDirection);
    vec3 viewDirection = normalize(uViewPosition - worldPosition.xyz);
    vec3 reflectionDirection = reflect(-lightDirection, normal);

    float diffuse = max(dot(normal, lightDirection), 0.0);
    float specular = pow(max(dot(viewDirection, reflectionDirection), 0.0), uShininess);

    vWorldPosition = worldPosition.xyz;
    vNormal = normal;
    vTexCoord = aTexCoord * uUvScale;
    vec3 totalDiffuse = uAmbient + uLightColor * diffuse;
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

    vGouraudDiffuse = totalDiffuse;
    vGouraudSpecular = totalSpecular;

    gl_Position = uProjection * uView * worldPosition;
}
