#version 330 core

// Only depth is written. Leaf cards cut out the same texels here as they do
// on screen, so a tree's shadow is dappled like its crown.

in vec2 vTexCoord;

uniform sampler2D uDiffuseTexture;
uniform float uAlphaCutoff;

void main()
{
    if (uAlphaCutoff > 0.0 && texture(uDiffuseTexture, vTexCoord).a < uAlphaCutoff)
        discard;
}
