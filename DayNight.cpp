#include "DayNight.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace
{
    glm::vec3 mixColor(const glm::vec3& first, const glm::vec3& second, float amount)
    {
        return first * (1.0f - amount) + second * amount;
    }

    // The sun is brighter than any lamp; in linear light a clear-sky sun is
    // several times stronger than the ambient sky.
    constexpr float sunIntensity = 1.6f;
}

void DayNight::update(float dt)
{
    if (!automatic_)
        return;
    timeOfDay_ = std::fmod(timeOfDay_ + glm::clamp(dt, 0.0f, 0.05f) * 0.22f, 24.0f);
}

void DayNight::reset()
{
    timeOfDay_ = 10.0f;
    automatic_ = true;
    lampOverride_ = false;
    manualLampsOn_ = false;
}

void DayNight::toggleAutomatic()
{
    automatic_ = !automatic_;
}

void DayNight::setDay()
{
    timeOfDay_ = 12.0f;
    automatic_ = false;
    lampOverride_ = false;
}

void DayNight::setNight()
{
    timeOfDay_ = 22.0f;
    automatic_ = false;
    lampOverride_ = false;
}

void DayNight::setTime(float hours)
{
    timeOfDay_ = std::fmod(std::fmod(hours, 24.0f) + 24.0f, 24.0f);
    automatic_ = false;
    lampOverride_ = false;
}

void DayNight::toggleStreetLamps()
{
    if (!lampOverride_)
    {
        manualLampsOn_ = !streetLampsOn();
        lampOverride_ = true;
    }
    else
    {
        manualLampsOn_ = !manualLampsOn_;
    }
}

float DayNight::sunAngle() const
{
    return (timeOfDay_ - 6.0f) / 24.0f * 2.0f * std::numbers::pi_v<float>;
}

float DayNight::daylightAmount() const
{
    return glm::smoothstep(-0.16f, 0.30f, std::sin(sunAngle()));
}

// 1 at the middle of dusk or dawn, 0 in full day or full night.
float DayNight::twilightAmount() const
{
    return 1.0f - std::abs(daylightAmount() * 2.0f - 1.0f);
}

bool DayNight::streetLampsOn() const
{
    return lampOverride_ ? manualLampsOn_ : daylightAmount() < 0.34f;
}

glm::vec3 DayNight::sunDirection() const
{
    const float angle = sunAngle();
    const float elevation = std::sin(angle);
    return glm::normalize(glm::vec3{
        std::cos(angle) * 0.65f,
        -std::max(elevation, 0.10f),
        std::sin(angle) * 0.35f});
}

glm::vec3 DayNight::sunColor() const
{
    const float daylight = daylightAmount();
    const float horizonWarmth = 1.0f - glm::smoothstep(0.20f, 0.72f, daylight);
    const glm::vec3 dayColor {0.96f, 0.94f, 0.86f};
    const glm::vec3 sunsetColor {1.0f, 0.45f, 0.18f};
    return mixColor(dayColor, sunsetColor, horizonWarmth * 0.65f) * daylight * sunIntensity;
}

glm::vec3 DayNight::sunVector() const
{
    // The same path as sunDirection(), but not clamped above the horizon.
    const float angle = sunAngle();
    return glm::normalize(glm::vec3{
        -std::cos(angle) * 0.65f,
        std::sin(angle),
        -std::sin(angle) * 0.35f});
}

glm::vec3 DayNight::moonVector() const
{
    // Roughly opposite the sun, nudged sideways so it is not a mirror image.
    return glm::normalize(-sunVector() + glm::vec3{0.25f, 0.0f, 0.20f});
}

glm::vec3 DayNight::skyAmbient() const
{
    const glm::vec3 night {0.020f, 0.028f, 0.055f};
    const glm::vec3 day {0.28f, 0.34f, 0.44f};
    const glm::vec3 dusk {0.06f, 0.03f, 0.02f};
    return mixColor(night, day, daylightAmount()) + dusk * twilightAmount();
}

glm::vec3 DayNight::groundAmbient() const
{
    const glm::vec3 night {0.008f, 0.008f, 0.012f};
    const glm::vec3 day {0.15f, 0.13f, 0.10f};
    return mixColor(night, day, daylightAmount());
}

glm::vec3 DayNight::skyZenithColor() const
{
    const glm::vec3 night {0.0015f, 0.0025f, 0.008f};
    const glm::vec3 day {0.045f, 0.13f, 0.46f};
    const glm::vec3 dusk {0.09f, 0.07f, 0.16f};
    return mixColor(night, day, daylightAmount()) + dusk * (twilightAmount() * 0.5f);
}

glm::vec3 DayNight::skyHorizonColor() const
{
    const glm::vec3 night {0.010f, 0.014f, 0.028f};
    const glm::vec3 day {0.34f, 0.47f, 0.66f};
    const glm::vec3 sunset {0.85f, 0.36f, 0.12f};
    return mixColor(mixColor(night, day, daylightAmount()), sunset, twilightAmount() * 0.55f);
}

glm::vec3 DayNight::sunGlowColor() const
{
    const float daylight = daylightAmount();
    const float warmth = 1.0f - glm::smoothstep(0.20f, 0.72f, daylight);
    return mixColor({1.0f, 0.85f, 0.60f}, {1.2f, 0.45f, 0.15f}, warmth) * (daylight * 0.9f);
}

glm::vec3 DayNight::sunDiscColor() const
{
    const float daylight = daylightAmount();
    const float warmth = 1.0f - glm::smoothstep(0.20f, 0.72f, daylight);
    const float visible = glm::smoothstep(-0.05f, 0.05f, std::sin(sunAngle()));
    return mixColor({60.0f, 52.0f, 40.0f}, {40.0f, 18.0f, 6.0f}, warmth) * visible;
}

glm::vec3 DayNight::moonColor() const
{
    return glm::vec3{1.35f, 1.42f, 1.65f} * (1.0f - daylightAmount());
}

float DayNight::starVisibility() const
{
    return 1.0f - glm::smoothstep(0.05f, 0.35f, daylightAmount());
}

float DayNight::fogDensity() const
{
    // A little extra haze at dawn and dusk.
    return 0.0032f + 0.0010f * twilightAmount();
}

float DayNight::fogFalloff() const
{
    return 0.030f;
}

float DayNight::exposure() const
{
    return glm::mix(1.8f, 1.0f, daylightAmount());
}

glm::vec3 DayNight::skyColor() const
{
    const float daylight = daylightAmount();
    const glm::vec3 night {0.012f, 0.020f, 0.065f};
    const glm::vec3 day {0.38f, 0.64f, 0.88f};
    const glm::vec3 sunset {0.82f, 0.32f, 0.18f};
    return mixColor(mixColor(night, day, daylight), sunset, twilightAmount() * 0.22f);
}

std::string DayNight::timeText() const
{
    const int hour = static_cast<int>(timeOfDay_) % 24;
    const int minute = static_cast<int>((timeOfDay_ - static_cast<float>(hour)) * 60.0f) % 60;
    char buffer[16] {};
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d", hour, minute);
    return buffer;
}
