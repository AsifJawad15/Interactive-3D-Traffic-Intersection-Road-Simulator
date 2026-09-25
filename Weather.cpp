#include "Weather.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>

namespace
{
    // The wind blows from the west-south-west: the clouds drift towards the
    // east-north-east, and the rain leans the same way.
    const glm::vec2 windDirection = glm::normalize(glm::vec2 {0.93f, 0.37f});

    float smooth(float edge0, float edge1, float x)
    {
        const float t = glm::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    // Wet in about 40 s of steady rain, puddles over about 90 s. Once it
    // stops the surfaces dry in about two minutes and the puddles, last, in
    // about four.
    constexpr float wettingSeconds = 40.0f;
    constexpr float dryingSeconds = 120.0f;
    constexpr float puddleFillSeconds = 90.0f;
    constexpr float puddleDrySeconds = 240.0f;
}

const char* Weather::name(WeatherKind kind)
{
    switch (kind)
    {
    case WeatherKind::Clear: return "CLEAR";
    case WeatherKind::Cloudy: return "CLOUDY";
    case WeatherKind::Rain: return "RAIN";
    }
    return "";
}

Weather::Look Weather::lookOf(WeatherKind kind)
{
    // cover, overcast, rain, cloud drift
    switch (kind)
    {
    case WeatherKind::Clear: return {0.16f, 0.0f, 0.0f, 9.0f};
    case WeatherKind::Cloudy: return {0.50f, 0.20f, 0.0f, 13.0f};
    case WeatherKind::Rain: return {1.0f, 0.86f, 1.0f, 17.0f};
    }
    return {};
}

void Weather::update(float seconds)
{
    const float dt = std::max(seconds, 0.0f);
    if (progress_ < 1.0f)
    {
        progress_ = std::min(1.0f, progress_ + dt / blendSeconds);
        applyBlend();
    }

    cloudOffset_ += windDirection * (current_.wind * dt);

    // Surfaces wet as fast as it rains; puddles fill once they are wet.
    const float rain = current_.rain;
    if (rain > 0.02f)
    {
        wetness_ = std::min(1.0f, wetness_ + dt * rain / wettingSeconds);
        if (wetness_ > 0.3f)
            puddles_ = std::min(1.0f, puddles_ + dt * rain / puddleFillSeconds);
    }
    else
    {
        wetness_ = std::max(0.0f, wetness_ - dt / dryingSeconds);
        puddles_ = std::max(0.0f, puddles_ - dt / puddleDrySeconds);
    }
    // A puddle is never there on a dry road.
    puddles_ = std::min(puddles_, wetness_ + 0.35f);
}

void Weather::applyBlend()
{
    // The clouds gather before the rain starts, and the rain stops before
    // they clear.
    float coverFrom = 0.0f;
    float coverTo = 1.0f;
    float rainFrom = 0.0f;
    float rainTo = 1.0f;
    if (to_.rain > from_.rain)
    {
        coverTo = 0.7f;
        rainFrom = 0.35f;
    }
    else if (to_.rain < from_.rain)
    {
        rainTo = 0.6f;
        coverFrom = 0.25f;
    }
    const float cover = smooth(coverFrom, coverTo, progress_);
    const float rain = smooth(rainFrom, rainTo, progress_);
    current_.cover = glm::mix(from_.cover, to_.cover, cover);
    current_.overcast = glm::mix(from_.overcast, to_.overcast, cover);
    current_.wind = glm::mix(from_.wind, to_.wind, cover);
    current_.rain = glm::mix(from_.rain, to_.rain, rain);
}

void Weather::next()
{
    blendTo(static_cast<WeatherKind>((static_cast<int>(target_) + 1) % kindCount));
}

void Weather::blendTo(WeatherKind kind)
{
    if (kind == target_)
        return;
    // From wherever the sky is now, so pressing K mid-blend never jumps.
    target_ = kind;
    from_ = current_;
    to_ = lookOf(kind);
    progress_ = 0.0f;
}

void Weather::set(WeatherKind kind, float wetness)
{
    target_ = kind;
    from_ = to_ = current_ = lookOf(kind);
    progress_ = 1.0f;
    wetness_ = glm::clamp(wetness, 0.0f, 1.0f);
    puddles_ = wetness_;
}

void Weather::reset()
{
    set(WeatherKind::Clear, 0.0f);
    cloudOffset_ = glm::vec2 {0.0f};
}

float Weather::cloudShadow() const
{
    // Patches of shade under broken cloud; none left to see once the whole
    // sky is grey (the overcast dims the light everywhere instead).
    // A clear sky's few wisps cast nothing worth the cost of looking.
    return 0.88f * smooth(0.2f, 0.4f, current_.cover) * (1.0f - smooth(0.25f, 0.85f, current_.overcast));
}

glm::vec2 Weather::groundWind() const
{
    return windDirection * (0.12f * current_.wind);
}
