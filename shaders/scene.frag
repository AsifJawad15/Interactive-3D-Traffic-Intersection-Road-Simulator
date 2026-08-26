#version 330 core

in vec3 vWorldPosition;
in vec3 vNormal;
in vec2 vTexCoord;
in vec3 vGouraudDiffuse;
in vec3 vGouraudSpecular;

uniform sampler2D uDiffuseTexture;
uniform vec3 uBaseColor;
uniform vec3 uEmissiveColor;
uniform vec3 uAmbient;
uniform vec3 uLightDirection;
uniform vec3 uLightColor;
uniform int uPointLightCount;
uniform vec3 uPointLightPositions[4];
uniform vec3 uPointLightColors[4];
uniform vec3 uViewPosition;
uniform float uShininess;
uniform int uShadingMode;

out vec4 fragmentColor;

vec3 illuminate(vec3 normal, vec3 albedo)
{
    vec3 lightDirection = normalize(-uLightDirection);
    vec3 viewDirection = normalize(uViewPosition - vWorldPosition);
    vec3 reflectionDirection = reflect(-lightDirection, normal);

    float diffuse = max(dot(normal, lightDirection), 0.0);
    float specular = pow(max(dot(viewDirection, reflectionDirection), 0.0), uShininess);
    vec3 diffuseLighting = uAmbient + uLightColor * diffuse;
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

    return albedo * diffuseLighting + specularLighting;
}

void main()
{
    vec3 albedo = texture(uDiffuseTexture, vTexCoord).rgb * uBaseColor;
    vec3 result;

    if (uShadingMode == 1)
    {
        result = albedo * vGouraudDiffuse + vGouraudSpecular;
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
        result = illuminate(normal, albedo);
    }

    result += uEmissiveColor;
    fragmentColor = vec4(result, 1.0);
}
