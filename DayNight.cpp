#include "DayNight.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
    glm::vec3 mixColor(const glm::vec3& first, const glm::vec3& second, float amount)
    {
        return first * (1.0f - amount) + second * amount;
    }

    // The sun is brighter than any lamp; in linear light a clear-sky sun is
    // several times stronger than the ambient sky.
    constexpr float sunIntensity = 1.6f;

    // The sky of a city at 40 degrees north in late spring: the sun's
    // declination is +10 degrees, so at its highest it stands
    // 90 - 40 + 10 = 60 degrees above the southern horizon. Clocks run half an
    // hour ahead of the sun (summer time), so it sets at about 19:00.
    const float latitude = glm::radians(40.0f);
    const float declination = glm::radians(10.0f);
    constexpr float solarNoon = 12.5f;

    // Where a body of the given declination stands at a clock time, as a
    // unit vector towards it: the hour angle turns 15 degrees an hour,
    // negative in the morning (east) and positive in the afternoon (west).
    glm::vec3 skyPosition(float hours, float bodyDeclination)
    {
        const float hourAngle = glm::radians((hours - solarNoon) * 15.0f);
        const float east = -std::cos(bodyDeclination) * std::sin(hourAngle);
        const float north = std::cos(latitude) * std::sin(bodyDeclination) -
                            std::sin(latitude) * std::cos(bodyDeclination) * std::cos(hourAngle);
        const float up = std::sin(latitude) * std::sin(bodyDeclination) +
                         std::cos(latitude) * std::cos(bodyDeclination) * std::cos(hourAngle);
        return glm::normalize(glm::vec3 {east, up, north});
    }

    // The moon is full: it runs opposite the sun, half an hour ahead, so it
    // is up all night and its shadows fall the other way.
    constexpr float moonLead = 11.5f;

    // Below this sine of the sun's elevation the moon takes over as the
    // light that casts shadows. The sun's light is already black here and the
    // moon's has not started, so the switch cannot be seen.
    constexpr float moonTakesOver = -0.03f;

    constexpr float presetGlideSeconds = 3.0f;
    constexpr float hourGlideSeconds = 1.2f;

    float wrapHours(float hours)
    {
        return std::fmod(std::fmod(hours, 24.0f) + 24.0f, 24.0f);
    }

    // A glide does not move the clock evenly: it spends longer where the
    // light changes fastest, round sunrise and sunset, so even a jump from
    // night to noon shows the sun coming up instead of a flash. Each clock
    // minute weighs its share of an hour (the sun's movement) plus how much
    // the light's colour changes in it; a glide moves evenly (eased)
    // through the summed weight, so neither the sun nor the light ever jumps.
    class GlideScale
    {
    public:
        // Weight per unit of change in the light's colour, against 1 per
        // hour of the sun's movement.
        static constexpr float colourWeight = 8.0f;

        static const GlideScale& get()
        {
            static const GlideScale scale;
            return scale;
        }

        // The summed weight from midnight of day 0 to an unwrapped time.
        float weightAt(float hours) const
        {
            const float days = std::floor(hours / 24.0f);
            const float minute = (hours - days * 24.0f) * 60.0f;
            const int index = std::min(static_cast<int>(minute), minutesPerDay - 1);
            const float within = minute - static_cast<float>(index);
            return days * total_ + sum_[static_cast<std::size_t>(index)] +
                   within * (sum_[static_cast<std::size_t>(index) + 1] - sum_[static_cast<std::size_t>(index)]);
        }

        // The time at which the summed weight reaches `weight`.
        float timeAt(float weight) const
        {
            const float days = std::floor(weight / total_);
            const float rest = weight - days * total_;
            const auto above = std::upper_bound(sum_.begin(), sum_.end(), rest);
            const int index = std::clamp(static_cast<int>(above - sum_.begin()) - 1, 0, minutesPerDay - 1);
            const float low = sum_[static_cast<std::size_t>(index)];
            const float high = sum_[static_cast<std::size_t>(index) + 1];
            const float within = high > low ? (rest - low) / (high - low) : 0.0f;
            return days * 24.0f + (static_cast<float>(index) + within) / 60.0f;
        }

    private:
        static constexpr int minutesPerDay = 24 * 60;
        std::array<float, minutesPerDay + 1> sum_ {};
        float total_ = 0.0f;

        GlideScale()
        {
            DayNight probe;
            probe.setTime(0.0f);
            glm::vec3 last = probe.lightColor();
            for (int minute = 0; minute < minutesPerDay; ++minute)
            {
                probe.setTime(static_cast<float>(minute + 1) / 60.0f);
                const glm::vec3 color = probe.lightColor();
                sum_[static_cast<std::size_t>(minute) + 1] =
                    sum_[static_cast<std::size_t>(minute)] + 1.0f / 60.0f + colourWeight * glm::length(color - last);
                last = color;
            }
            total_ = sum_.back();
        }
    };
}

const std::array<TimePreset, DayNight::presetCount>& DayNight::presets()
{
    static const std::array<TimePreset, presetCount> list = {{
        {"MORNING", 7.0f}, {"NOON", 12.0f}, {"AFTERNOON", 15.5f}, {"EVENING", 18.5f}, {"NIGHT", 22.0f}
    }};
    return list;
}

void DayNight::update(float dt)
{
    if (!automatic_ || gliding())
        return;
    timeOfDay_ = std::fmod(timeOfDay_ + glm::clamp(dt, 0.0f, 0.05f) * 0.22f, 24.0f);
}

void DayNight::animate(float seconds)
{
    if (!gliding())
        return;
    glideClock_ = std::min(glideClock_ + std::max(seconds, 0.0f), glideSeconds_);
    const float progress = glideClock_ / glideSeconds_;
    // Eased in and out: the sun sets off gently and settles gently, and
    // takes its time over sunrise and sunset.
    const float eased = progress * progress * (3.0f - 2.0f * progress);
    const GlideScale& scale = GlideScale::get();
    const float from = scale.weightAt(glideFrom_);
    const float to = scale.weightAt(glideTo_);
    timeOfDay_ = wrapHours(progress >= 1.0f ? glideTo_ : scale.timeAt(from + (to - from) * eased));
    if (glideClock_ >= glideSeconds_)
        glideSeconds_ = 0.0f;
}

void DayNight::reset()
{
    timeOfDay_ = 10.0f;
    automatic_ = true;
    lampOverride_ = false;
    manualLampsOn_ = false;
    glideSeconds_ = 0.0f;
}

void DayNight::toggleAutomatic()
{
    automatic_ = !automatic_;
}

void DayNight::setDay()
{
    setTime(12.0f);
}

void DayNight::setNight()
{
    setTime(22.0f);
}

void DayNight::setTime(float hours)
{
    timeOfDay_ = wrapHours(hours);
    automatic_ = false;
    lampOverride_ = false;
    glideSeconds_ = 0.0f;
}

float DayNight::targetTime() const
{
    return gliding() ? wrapHours(glideTo_) : timeOfDay_;
}

void DayNight::glideTo(float target, float seconds)
{
    // `target` is unwrapped relative to the current clock: above it runs
    // forwards, below it backwards. A very long glide (night to noon) takes
    // up to half as long again, so the sun still never jumps.
    glideFrom_ = timeOfDay_;
    glideTo_ = target;
    glideClock_ = 0.0f;
    const GlideScale& scale = GlideScale::get();
    const float span = std::abs(scale.weightAt(glideTo_) - scale.weightAt(glideFrom_));
    glideSeconds_ = seconds * glm::clamp(span / 20.0f, 1.0f, 1.5f);
    automatic_ = false;
    lampOverride_ = false;
    if (std::abs(glideTo_ - glideFrom_) < 1.0e-4f)
        glideSeconds_ = 0.0f;
}

void DayNight::glideToPreset(int index)
{
    glideToTime(presets()[static_cast<std::size_t>(std::clamp(index, 0, presetCount - 1))].hours);
}

void DayNight::glideToTime(float hours)
{
    // Always forwards, the way the day goes, but never more than a day.
    float ahead = wrapHours(hours - timeOfDay_);
    if (ahead > 23.99f)
        ahead = 0.0f;
    glideTo(timeOfDay_ + ahead, presetGlideSeconds);
}

void DayNight::nextPreset()
{
    // The next preset after where the clock is going (so pressing O again
    // mid-glide moves on to the one after).
    const float from = targetTime();
    float best = 25.0f;
    for (const TimePreset& preset : presets())
    {
        float ahead = wrapHours(preset.hours - from);
        if (ahead < 0.02f)
            ahead += 24.0f;
        best = std::min(best, ahead);
    }
    const float ahead = wrapHours(targetTime() - timeOfDay_) + best;
    glideTo(timeOfDay_ + ahead, presetGlideSeconds);
}

void DayNight::stepHours(float hours)
{
    // From where the clock is going, so pressing ] three times moves three hours.
    float pending = gliding() ? glideTo_ - timeOfDay_ : 0.0f;
    glideTo(timeOfDay_ + pending + hours, hourGlideSeconds);
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

int DayNight::period() const
{
    const float hours = timeOfDay_;
    if (hours >= 5.0f && hours < 10.5f)
        return 0;
    if (hours >= 10.5f && hours < 13.5f)
        return 1;
    if (hours >= 13.5f && hours < 17.5f)
        return 2;
    if (hours >= 17.5f && hours < 20.5f)
        return 3;
    return 4;
}

bool DayNight::onPreset() const
{
    const float hours = targetTime();
    for (const TimePreset& preset : presets())
    {
        if (std::abs(preset.hours - hours) < 0.02f)
            return true;
    }
    return false;
}

glm::vec3 DayNight::sunVector() const
{
    return skyPosition(timeOfDay_, declination);
}

glm::vec3 DayNight::moonVector() const
{
    return skyPosition(timeOfDay_ + moonLead, -declination);
}

float DayNight::sunHeight() const
{
    return sunVector().y;
}

float DayNight::daylightAmount() const
{
    return glm::smoothstep(-0.16f, 0.30f, sunHeight());
}

// 1 at the middle of dusk or dawn, 0 in full day or full night.
float DayNight::twilightAmount() const
{
    return 1.0f - std::abs(daylightAmount() * 2.0f - 1.0f);
}

bool DayNight::streetLampsOn() const
{
    // On when the sun is lower than about 7 degrees: the Evening preset
    // (18:30) already has its lamps and neon lit.
    return lampOverride_ ? manualLampsOn_ : daylightAmount() < 0.68f;
}

glm::vec3 DayNight::sunLight() const
{
    const float height = sunHeight();
    // It rises out of nothing over the first six degrees, is dimmed and
    // reddened by the long path through the air while it is low, and is
    // white when it is high.
    const float visible = glm::smoothstep(0.0f, 0.10f, height);
    const float warmth = 1.0f - glm::smoothstep(0.05f, 0.50f, height);
    const float strength = glm::mix(0.55f, 1.0f, glm::smoothstep(0.05f, 0.40f, height));
    const glm::vec3 dayColor {0.96f, 0.94f, 0.86f};
    const glm::vec3 sunsetColor {1.0f, 0.45f, 0.18f};
    return mixColor(dayColor, sunsetColor, warmth * 0.75f) * (visible * strength * sunIntensity);
}

glm::vec3 DayNight::moonLight() const
{
    // Faint and blue. It only starts once the sun has gone well down, and
    // grows as the moon climbs.
    const float afterDusk = 1.0f - glm::smoothstep(-0.14f, moonTakesOver - 0.02f, sunHeight());
    const float risen = glm::smoothstep(0.0f, 0.20f, moonVector().y);
    return glm::vec3 {0.075f, 0.095f, 0.16f} * (afterDusk * risen);
}

bool DayNight::moonlit() const
{
    return sunHeight() < moonTakesOver;
}

glm::vec3 DayNight::lightDirection() const
{
    // Never quite horizontal, so the lighting and shadow maths stay sound
    // while the light is black at the horizon.
    glm::vec3 towards = moonlit() ? moonVector() : sunVector();
    towards.y = std::max(towards.y, 0.02f);
    return -glm::normalize(towards);
}

glm::vec3 DayNight::lightColor() const
{
    return moonlit() ? moonLight() : sunLight();
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
    const float visible = glm::smoothstep(-0.05f, 0.05f, sunHeight());
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
    // Rounded to the minute, so 15.5 shows as 15:30 and not 15:29.
    const int minutes = static_cast<int>(std::lround(timeOfDay_ * 60.0f)) % (24 * 60);
    char buffer[16] {};
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d", minutes / 60, minutes % 60);
    return buffer;
}
