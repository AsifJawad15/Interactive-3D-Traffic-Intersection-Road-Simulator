// Shared by the sky and by every lit surface, so that fog always fades into
// exactly the colour of the sky behind the object. That is what hides the
// edge of the world: distant ground melts into the horizon instead of ending.

uniform vec3 uSunVector;    // unit vector pointing TOWARDS the sun (can be below the horizon)
uniform vec3 uSkyZenith;    // linear colour straight up
uniform vec3 uSkyHorizon;   // linear colour at the horizon
uniform vec3 uSunGlow;      // haze colour around the sun
uniform float uFogDensity;  // extinction per metre at ground level
uniform float uFogFalloff;  // how fast the fog thins with height, per metre

vec3 atmosphereColor(vec3 direction)
{
    float up = direction.y;
    vec3 sky = mix(uSkyHorizon, uSkyZenith, pow(clamp(up, 0.0, 1.0), 0.5));

    // Below the horizon the eye meets distant hazy ground, slightly darker
    // than the horizon itself.
    float below = 1.0 - smoothstep(-0.30, 0.0, up);
    sky = mix(sky, uSkyHorizon * 0.62, below);

    // Forward scattering: the air near the sun glows.
    float towardsSun = max(dot(direction, uSunVector), 0.0);
    sky += uSunGlow * (0.18 * pow(towardsSun, 6.0) + 0.55 * pow(towardsSun, 48.0));
    return sky;
}

// Exponential height fog. The density is  a * exp(-b * y),  and integrating it
// along the view ray from the camera to the surface gives the optical depth
// in closed form:
//
//     depth = a * exp(-b * y0) * (1 - exp(-b * dy * D)) / (b * dy)
//
// The fraction of the surface colour that survives is exp(-depth).
vec3 applyFog(vec3 color, vec3 worldPosition, vec3 viewPosition)
{
    vec3 ray = worldPosition - viewPosition;
    float distanceToSurface = length(ray);
    if (distanceToSurface < 1e-3)
        return color;

    vec3 direction = ray / distanceToSurface;
    float groundDensity = uFogDensity * exp(-uFogFalloff * max(viewPosition.y, 0.0));
    float k = uFogFalloff * direction.y * distanceToSurface;

    float opticalDepth;
    if (abs(k) < 1e-3)
        opticalDepth = groundDensity * distanceToSurface;
    else
        opticalDepth = groundDensity * (1.0 - exp(-k)) / (uFogFalloff * direction.y);

    float fog = clamp(1.0 - exp(-opticalDepth), 0.0, 1.0);
    return mix(color, atmosphereColor(direction), fog);
}
