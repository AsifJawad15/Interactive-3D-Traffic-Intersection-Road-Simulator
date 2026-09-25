#version 330 core

// The sky is drawn as one full-screen triangle behind everything else. Each
// pixel turns its screen position back into a world-space view direction and
// asks the shared atmosphere function for the colour of the sky that way,
// then adds the sun disc, the moon and, at night, the stars, and last the
// clouds in front of them all.

in vec2 vUv;

uniform mat4 uInverseViewProjection;
uniform vec3 uViewPosition;
uniform vec3 uSunDiscColor;
uniform vec3 uMoonVector;
uniform vec3 uMoonColor;
uniform float uStarVisibility;
uniform float uTime;

// The clouds' light: the sun's or the moon's colour where it reaches them
// and the unit vector towards it, the soft light of the sky round them, and
// how dark the undersides of rain clouds are (0..1).
uniform vec3 uCloudLight;
uniform vec3 uCloudLightVector;
uniform vec3 uCloudAmbient;
uniform float uCloudDarkness;

#include "atmosphere.glsl"
#include "clouds.glsl"

out vec4 fragmentColor;

float hash(vec3 cell)
{
    return fract(sin(dot(cell, vec3(12.9898, 78.233, 37.719))) * 43758.5453);
}

void main()
{
    vec4 farPoint = uInverseViewProjection * vec4(vUv * 2.0 - 1.0, 1.0, 1.0);
    vec3 direction = normalize(farPoint.xyz / farPoint.w - uViewPosition);

    vec3 color = atmosphereColor(direction);
    vec3 bodies = vec3(0.0);
    float aboveHorizon = smoothstep(-0.02, 0.02, direction.y);

    // Sun disc, about half a degree across, with a thin soft edge.
    float sunDisc = smoothstep(0.99985, 0.99993, dot(direction, uSunVector));
    bodies += uSunDiscColor * sunDisc * aboveHorizon;

    // Moon disc plus a faint halo.
    float towardsMoon = dot(direction, uMoonVector);
    float moonDisc = smoothstep(0.99955, 0.99965, towardsMoon);
    bodies += uMoonColor * (moonDisc + 0.04 * pow(max(towardsMoon, 0.0), 180.0)) * aboveHorizon;

    // Stars: the direction is quantised into small cells and a few cells,
    // chosen by a hash, hold a point of light that twinkles slowly.
    if (uStarVisibility > 0.0 && direction.y > 0.0)
    {
        const float cellsPerUnit = 220.0;
        vec3 scaled = direction * cellsPerUnit;
        vec3 cell = floor(scaled);
        float random = hash(cell);
        if (random > 0.9965)
        {
            float distanceToCentre = length(fract(scaled) - 0.5);
            float point = smoothstep(0.30, 0.0, distanceToCentre);
            float twinkle = 0.65 + 0.35 * sin(uTime * (1.5 + 3.0 * random) + random * 40.0);
            float brightness = 0.6 + 2.4 * fract(random * 173.0);
            bodies += vec3(0.85, 0.90, 1.0) * point * twinkle * brightness * uStarVisibility *
                      smoothstep(0.0, 0.25, direction.y);
        }
    }

    // The clouds: where this pixel's ray meets the sheet 1.5 km up. Towards
    // the horizon the sheet is far away and seen edge-on, so it thins into
    // the haze instead of ending in a line.
    float cloud = 0.0;
    vec3 cloudColor = vec3(0.0);
    if (direction.y > 0.004)
    {
        float along = (cloudBase - uViewPosition.y) / direction.y;
        vec2 point = uViewPosition.xz + direction.xz * along;
        // Fewer octaves far away, where the finest detail would only flicker.
        int octaves = along < 6000.0 ? 6 : (along < 15000.0 ? 5 : 4);
        float density = cloudDensity(point, octaves);
        if (density > 0.0)
        {
            // Light comes through where the cloud towards the light is thin:
            // the edges facing the sun are bright, the thick middles dark
            // underneath. Thin edges seen near the sun glow (a silver lining).
            vec3 towards = uCloudLightVector;
            vec2 lightStep = normalize(towards.xz + vec2(1e-4)) * 220.0;
            float shade = cloudDensity(point + lightStep, 4);
            float lit = exp(-2.4 * shade);
            float lining = pow(max(dot(direction, towards), 0.0), 7.0) * (1.0 - density) * 2.2;
            cloudColor = uCloudAmbient * mix(1.15, 0.65, density) +
                         uCloudLight * (0.25 + 0.55 * lit + lining) * 0.8;
            cloudColor *= mix(1.0, 0.5, uCloudDarkness * density);

            float haze = 1.0 - exp(-along / 16000.0);
            cloudColor = mix(cloudColor, atmosphereColor(direction), haze);
            cloud = density * smoothstep(0.004, 0.09, direction.y);
        }
    }

    // Thick cloud hides the sun, the moon and the stars behind it.
    float behind = exp(-7.0 * cloud);
    fragmentColor = vec4(mix(color, cloudColor, cloud) + bodies * behind, 1.0);
}
