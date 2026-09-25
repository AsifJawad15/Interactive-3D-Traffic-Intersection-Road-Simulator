#version 330 core

// Enhanced mode's sun shafts, step 1, at a quarter of the resolution: the
// light the shafts are made of. Only open sky shines (buildings, trees and
// everything else in front of it are dark), only near the sun, and only
// as brightly as the sky there really is, so a cloud over the sun takes
// the shafts away with it.

in vec2 vUv;

uniform sampler2D uScene;
uniform sampler2D uDepth;
uniform vec2 uSunUv;          // where the sun is on the screen (may be off it)
uniform float uAspect;        // width / height
uniform float uExposure;

out vec4 fragmentColor;

void main()
{
    if (textureLod(uDepth, vUv, 0.0).r < 1.0)
    {
        fragmentColor = vec4(0.0);
        return;
    }
    vec3 sky = textureLod(uScene, vUv, 0.0).rgb * uExposure;
    float brightness = max(sky.r, max(sky.g, sky.b));
    vec2 fromSun = (vUv - uSunUv) * vec2(uAspect, 1.0);
    float nearSun = exp(-dot(fromSun, fromSun) * 9.0);
    fragmentColor = vec4(vec3(clamp(brightness - 0.8, 0.0, 3.0) * nearSun), 1.0);
}
