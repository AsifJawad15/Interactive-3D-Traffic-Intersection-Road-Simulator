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

float DayNight::daylightAmount() const
{
    const float sunElevation = std::sin((timeOfDay_ - 6.0f) / 24.0f * 2.0f * std::numbers::pi_v<float>);
    return glm::smoothstep(-0.16f, 0.30f, sunElevation);
}

bool DayNight::streetLampsOn() const
{
    return lampOverride_ ? manualLampsOn_ : daylightAmount() < 0.34f;
}

glm::vec3 DayNight::ambientLight() const
{
    return mixColor({0.035f, 0.045f, 0.075f}, {0.24f, 0.26f, 0.30f}, daylightAmount());
}

glm::vec3 DayNight::sunDirection() const
{
    const float angle = (timeOfDay_ - 6.0f) / 24.0f * 2.0f * std::numbers::pi_v<float>;
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
    return mixColor(dayColor, sunsetColor, horizonWarmth * 0.65f) * daylight;
}

glm::vec3 DayNight::skyColor() const
{
    const float daylight = daylightAmount();
    const glm::vec3 night {0.012f, 0.020f, 0.065f};
    const glm::vec3 day {0.38f, 0.64f, 0.88f};
    const glm::vec3 sunset {0.82f, 0.32f, 0.18f};
    const float twilight = 1.0f - std::abs(daylight * 2.0f - 1.0f);
    return mixColor(mixColor(night, day, daylight), sunset, twilight * 0.22f);
}

std::string DayNight::timeText() const
{
    const int hour = static_cast<int>(timeOfDay_) % 24;
    const int minute = static_cast<int>((timeOfDay_ - static_cast<float>(hour)) * 60.0f) % 60;
    char buffer[16] {};
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d", hour, minute);
    return buffer;
}
