#pragma once

#include <glm/vec3.hpp>

#include <array>
#include <string>

// A named time of day, one of the corner buttons (and O).
struct TimePreset
{
    const char* name;
    float hours;
};

// The clock that drives the sun, the moon, the sky, the ambient light and the
// lamps. All colours returned here are LINEAR light values (the renderer
// tone-maps and gamma-encodes at the very end), so they may exceed 1.0.
//
// The sun follows a real mid-latitude path: it rises in the east, climbs to
// about 60 degrees in the south at midday and sets in the west, so even noon
// has short shadows. The moon runs the opposite arc through the night.
// World axes: +x is east, +z is north, +y is up.
class DayNight
{
public:
    static constexpr int presetCount = 5;
    static const std::array<TimePreset, presetCount>& presets();

    // One simulation step: the automatic cycle.
    void update(float dt);
    // Every frame, paused or not: the glide to a new time.
    void animate(float seconds);
    void reset();

    void toggleAutomatic();
    void setDay();
    void setNight();
    void toggleStreetLamps();

    // Fixes the clock at a given hour at once and stops the automatic cycle.
    void setTime(float hours);

    // The sun and moon glide to a new time instead of jumping: three seconds
    // to a preset, a little over one for an hour. These stop the automatic
    // cycle. O runs forwards through the presets; [ and ] move an hour.
    void glideToPreset(int index);
    void glideToTime(float hours);
    void nextPreset();
    void stepHours(float hours);
    bool gliding() const { return glideSeconds_ > 0.0f; }

    float timeOfDay() const { return timeOfDay_; }
    bool automatic() const { return automatic_; }
    bool streetLampsOn() const;
    float daylightAmount() const;
    std::string timeText() const;

    // Which part of the day it is (0 Morning .. 4 Night), named like the
    // presets, and whether the clock stands exactly on that preset (or is
    // gliding to it).
    int period() const;
    const char* periodName() const { return presets()[static_cast<std::size_t>(period())].name; }
    bool onPreset() const;

    // Where the sun and moon are, as unit vectors pointing towards them
    // (below the horizon too).
    glm::vec3 sunVector() const;
    glm::vec3 moonVector() const;

    // The one directional light that shades and casts shadows: the sun by
    // day, the moon by night. The direction is the way the light TRAVELS.
    // Between the two (dusk and dawn) its colour passes through black, so
    // switching from one to the other never shows.
    glm::vec3 lightDirection() const;
    glm::vec3 lightColor() const;
    bool moonlit() const;

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

    // The weather's say in the light (Weather.h): how overcast the sky is
    // (0..1: the sun dims, the ambient lifts and greys, the sky greys) and
    // how hard it rains (the haze thickens a little).
    void setWeather(float overcast, float rain);
    float overcast() const { return overcast_; }

    // The light on the clouds: the sun's, which reaches them at 1.5 km a
    // little before sunrise and after sunset, or the moon's; and the unit
    // vector towards whichever lights them.
    glm::vec3 cloudLight() const;
    glm::vec3 cloudLightVector() const;

    // Plain colour for glClear, kept for code that still wants one.
    glm::vec3 skyColor() const;

private:
    float timeOfDay_ = 10.0f;
    bool automatic_ = true;
    bool lampOverride_ = false;
    bool manualLampsOn_ = false;

    // The glide: from one clock time to another (unwrapped, so it may run
    // past midnight either way), eased in and out.
    float glideFrom_ = 0.0f;
    float glideTo_ = 0.0f;
    float glideClock_ = 0.0f;
    float glideSeconds_ = 0.0f;

    float overcast_ = 0.0f;
    float rain_ = 0.0f;

    void glideTo(float target, float seconds);
    float targetTime() const;
    float sunHeight() const;   // sine of the sun's elevation
    float twilightAmount() const;
    glm::vec3 sunLight() const;
    glm::vec3 moonLight() const;
};
