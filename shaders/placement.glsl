// Where a vertex ends up in the world. Shared by the scene shader and the
// shadow-map shader, so a shadow moves exactly like what casts it: the
// people and the walkers' lamps (instanced), trees swaying in the wind, and
// the ripples on the fountain.

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
uniform mat3 uNormalMatrix;

// Ripple animation for the fountain water. Zero for every other object, so a
// single extra uniform buys the whole effect without a second shader.
uniform float uWaveAmplitude;
uniform float uTime;

// Wind in the trees: 0 for everything else. Each vertex bends by its sway
// weight (vertex alpha), trees a little out of step with each other, and the
// leaves flutter on top of the slow sway.
uniform float uSway;

struct Placed
{
    vec4 world;
    vec3 normal;
    vec4 paint;
    float glow;
};

Placed placeVertex()
{
    vec3 localPosition = aPosition;
    if (uWaveAmplitude > 0.0)
    {
        float ringDistance = length(localPosition.xz);
        localPosition.y += uWaveAmplitude * sin(22.0 * ringDistance - 3.4 * uTime);
    }

    mat4 model = uModel;
    mat3 normalMatrix = uNormalMatrix;
    Placed placed;
    placed.paint = aColor;
    placed.glow = 0.0;
    if (uInstanced > 0.5)
    {
        // A copy may be stretched along one axis (an arm, a leg), so its
        // normals need the inverse transpose, worked out here per copy.
        model = mat4(aInstanceModel0, aInstanceModel1, aInstanceModel2, aInstanceModel3);
        normalMatrix = transpose(inverse(mat3(model)));
        placed.paint = vec4(aColor.rgb * aInstanceColor.rgb, aColor.a);
        placed.glow = aInstanceColor.a;
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
    placed.world = worldPosition;
    placed.normal = normalize(normalMatrix * aNormal);
    return placed;
}
