#version 330 core

// Enhanced mode, at half resolution: smooths the occlusion and contact
// shadows of ssao.frag. Each pixel there took a few rays staggered by a
// per-pixel noise; averaging 5 x 5 neighbours turns that grain into an
// even shade. Only neighbours at the pixel's own depth count, so the shade
// under a car does not bleed onto the road seen past it.

in vec2 vUv;

uniform sampler2D uOcclusion;   // r: ambient kept, g: sunlight kept, b: distance

out vec4 fragmentColor;

void main()
{
    ivec2 texel = ivec2(gl_FragCoord.xy);
    ivec2 last = textureSize(uOcclusion, 0) - 1;
    vec4 centre = texelFetch(uOcclusion, texel, 0);
    vec2 sum = vec2(0.0);
    float total = 0.0;
    for (int y = -2; y <= 2; ++y)
    {
        for (int x = -2; x <= 2; ++x)
        {
            vec4 other = texelFetch(uOcclusion, clamp(texel + ivec2(x, y), ivec2(0), last), 0);
            float weight = exp(-abs(other.b - centre.b) / (0.02 * centre.b + 0.03));
            sum += other.rg * weight;
            total += weight;
        }
    }
    fragmentColor = vec4(sum / total, centre.b, 1.0);
}
