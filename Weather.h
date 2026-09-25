#pragma once

#include <glm/vec2.hpp>

// The weather: Clear, Cloudy or Rain, cycled with K or chosen with the corner
// buttons. A change never snaps: every value below blends to the new state
// over about ten seconds. Going towards rain the clouds thicken first and the
// rain starts once they have; going away from it the rain stops first.
//
// Wetness and puddles are not part of the state: they build up while it
// rains (the roads are wet after about 40 s, puddles fill over about a
// minute and a half) and dry slowly once it stops, the puddles last.
enum class WeatherKind
{
    Clear,
    Cloudy,
    Rain
};

class Weather
{
public:
    static constexpr int kindCount = 3;
    static constexpr float blendSeconds = 10.0f;
    static const char* name(WeatherKind kind);

    // Every frame, on real time (the sky keeps moving while paused).
    void update(float seconds);

    // K: Clear -> Cloudy -> Rain -> Clear.
    void next();
    void blendTo(WeatherKind kind);
    // At once, for captures and tests: the state, and how soaked the city is.
    void set(WeatherKind kind, float wetness);
    void reset();

    WeatherKind kind() const { return target_; }
    const char* kindName() const { return name(target_); }
    bool blending() const { return progress_ < 1.0f; }
    float blendProgress() const { return progress_; }

    // How much of the sky the cloud layer covers (0 a clear blue sky, 1
    // overcast), and how much it shuts out the sun everywhere at once
    // (broken cloud dims it only where a cloud's shadow falls).
    float cloudCover() const { return current_.cover; }
    float overcast() const { return current_.overcast; }
    // How hard it rains, 0..1.
    float rain() const { return current_.rain; }
    // How strongly clouds shadow the ground in patches: strong for broken
    // cloud, none under a sky that is overcast all over.
    float cloudShadow() const;

    float wetness() const { return wetness_; }
    float puddles() const { return puddles_; }

    // How far the cloud layer has drifted with the wind, in metres, and the
    // wind at street level (m/s, x east, y north) that slants the rain.
    glm::vec2 cloudOffset() const { return cloudOffset_; }
    glm::vec2 groundWind() const;

private:
    struct Look
    {
        float cover = 0.0f;
        float overcast = 0.0f;
        float rain = 0.0f;
        float wind = 0.0f;   // m/s of cloud drift
    };
    static Look lookOf(WeatherKind kind);

    WeatherKind target_ = WeatherKind::Clear;
    Look from_ = lookOf(WeatherKind::Clear);
    Look to_ = lookOf(WeatherKind::Clear);
    Look current_ = lookOf(WeatherKind::Clear);
    float progress_ = 1.0f;

    float wetness_ = 0.0f;
    float puddles_ = 0.0f;
    glm::vec2 cloudOffset_ {0.0f};

    void applyBlend();
};
