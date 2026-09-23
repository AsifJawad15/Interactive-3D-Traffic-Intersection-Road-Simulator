#pragma once

#include <glm/vec3.hpp>

#include <string>

// The clock that drives the sun, the sky, the ambient light and the lamps.
// All colours returned here are LINEAR light values (the renderer tone-maps
// and gamma-encodes at the very end), so they may exceed 1.0.
class DayNight
{
public:
    void update(float dt);
    void reset();

    void toggleAutomatic();
    void setDay();
    void setNight();
    void toggleStreetLamps();

    // Fixes the clock at a given hour and stops the automatic cycle.
    void setTime(float hours);

    float timeOfDay() const { return timeOfDay_; }
    bool automatic() const { return automatic_; }
    bool streetLampsOn() const;
    float daylightAmount() const;
    std::string timeText() const;

    // Direct sunlight used for shading. The direction is the way the light
    // TRAVELS, and is held just above the horizon at night so the maths never
    // degenerates; the colour fades to black instead.
    glm::vec3 sunDirection() const;
    glm::vec3 sunColor() const;

    // Where the sun and moon really are, as unit vectors pointing towards them.
    glm::vec3 sunVector() const;
    glm::vec3 moonVector() const;

    // Hemisphere ambient: light arriving from the sky above and bounced up
    // from the ground below. A surface blends the two by how much it faces up.
    glm::vec3 skyAmbient() const;
    glm::vec3 groundAmbient() const;

    // Sky dome and fog.
    glm::vec3 skyZenithColor() const;
    glm::vec3 skyHorizonColor() const;
    glm::vec3 sunGlowColor() const;
    glm::vec3 sunDiscColor() const;
    glm::vec3 moonColor() const;
    float starVisibility() const;
    float fogDensity() const;
    float fogFalloff() const;

    // Camera exposure. It rises at night, the way eyes adapt to the dark.
    float exposure() const;

    // Plain colour for glClear, kept for code that still wants one.
    glm::vec3 skyColor() const;

private:
    float timeOfDay_ = 10.0f;
    bool automatic_ = true;
    bool lampOverride_ = false;
    bool manualLampsOn_ = false;

    float sunAngle() const;
    float twilightAmount() const;
};
