#include "Tour.h"

#include "Camera.h"
#include "DayNight.h"
#include "Overlay.h"
#include "Pedestrians.h"
#include "Player.h"
#include "Simulation.h"
#include "VehicleTypes.h"
#include "Weather.h"
#include "World.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>

namespace
{
    // Where each part of the video starts, in seconds. The picture dips to
    // black for a moment at each of these cuts.
    constexpr float cityStart = 11.0f;
    constexpr float driveStart = 22.0f;
    constexpr float peopleStart = 50.0f;
    constexpr float shadingStart = 58.0f;
    constexpr float dayStart = 68.0f;
    constexpr float weatherStart = 84.0f;
    constexpr float rayStart = 100.0f;
    constexpr std::array<float, 6> cuts = {driveStart, peopleStart, shadingStart, dayStart, weatherStart, rayStart};
    constexpr float dipSeconds = 0.35f;

    // Your car is already waiting at a red light when the chase camera
    // reaches it, gets green at this moment, and must be out of the next
    // roundabout before the people's part begins.
    constexpr float chaseFrom = 25.5f;
    constexpr float greenAt = 29.5f;


    // The lit billboard ("GRAPHICS LAB / CSE 4102") on the lawn west of the
    // crossroads, and where the camera stands in front of it.
    const glm::vec3 billboardMiddle {-50.0f, World::billboardBottom + 0.5f * World::billboardHeight, -15.0f};
    const glm::vec3 billboardCamera {-50.0f, billboardMiddle.y, -9.3f};

    constexpr float afternoon = 15.5f;
    constexpr float evening = 18.6f;
    constexpr float night = 22.0f;

    float clamp01(float x)
    {
        return glm::clamp(x, 0.0f, 1.0f);
    }

    float progress(float t, float from, float to)
    {
        return clamp01((t - from) / (to - from));
    }

    // Eased 0..1: no speed at either end, so every move starts and stops
    // smoothly.
    float smooth(float x)
    {
        x = clamp01(x);
        return x * x * (3.0f - 2.0f * x);
    }

    float smoother(float x)
    {
        x = clamp01(x);
        return x * x * x * (x * (6.0f * x - 15.0f) + 10.0f);
    }

    glm::vec3 bezier(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3, float s)
    {
        const float r = 1.0f - s;
        return r * r * r * p0 + 3.0f * r * r * s * p1 + 3.0f * r * s * s * p2 + s * s * s * p3;
    }

    glm::vec3 headingVector(float degrees)
    {
        const float h = glm::radians(degrees);
        return {std::sin(h), 0.0f, std::cos(h)};
    }

    // A point on a circle round `centre`, `degrees` from +x towards +z.
    glm::vec3 orbit(const glm::vec3& centre, float radius, float height, float degrees)
    {
        const float a = glm::radians(degrees);
        return centre + glm::vec3 {radius * std::cos(a), height, radius * std::sin(a)};
    }

    bool crossed(float previous, float now, float at)
    {
        return previous < at && now >= at;
    }

    // The pointer's path: it glides between these points, resting at each.
    struct CursorKey
    {
        float time;
        glm::vec2 position;
    };

    glm::vec2 cursorAt(float t, const std::vector<CursorKey>& keys)
    {
        if (t <= keys.front().time)
            return keys.front().position;
        for (std::size_t index = 1; index < keys.size(); ++index)
        {
            if (t <= keys[index].time)
            {
                const float s = smoother(progress(t, keys[index - 1].time, keys[index].time));
                return glm::mix(keys[index - 1].position, keys[index].position, s);
            }
        }
        return keys.back().position;
    }

    // 0..1 through the ring of a click at `at`, 0 outside it.
    float pressAt(float t, float at)
    {
        return t >= at && t < at + 0.45f ? (t - at) / 0.45f : 0.0f;
    }

    glm::vec2 timeButton(int index, int width)
    {
        const float x = TimeButtons::left(width, DayNight::presetCount) +
                        static_cast<float>(index) * (TimeButtons::buttonWidth + TimeButtons::gap) + 0.5f * TimeButtons::buttonWidth;
        return {x, TimeButtons::top + TimeButtons::header + 0.5f * TimeButtons::buttonHeight};
    }

    glm::vec2 weatherButton(int index, int width)
    {
        const float x = WeatherButtons::left(width, Weather::kindCount) +
                        static_cast<float>(index) * (TimeButtons::buttonWidth + TimeButtons::gap) + 0.5f * TimeButtons::buttonWidth;
        return {x, WeatherButtons::top + TimeButtons::header + 0.5f * TimeButtons::buttonHeight};
    }

    glm::vec2 rayButton(int width)
    {
        return {EnhancedButton::left(width) + 0.5f * EnhancedButton::width,
                EnhancedButton::top + TimeButtons::header + 0.5f * TimeButtons::buttonHeight};
    }

    glm::vec2 yesButton(int width, int height)
    {
        return {ConfirmDialog::buttonLeft(width, 0) + 0.5f * ConfirmDialog::buttonWidth,
                ConfirmDialog::buttonTop(height) + 0.5f * ConfirmDialog::buttonHeight};
    }

    bool debugging()
    {
        char* value = nullptr;
        std::size_t size = 0;
        const bool set = _dupenv_s(&value, &size, "TOUR_DEBUG") == 0 && value != nullptr;
        std::free(value);
        return set;
    }

    bool drivable(const Vehicle& vehicle)
    {
        return vehicle.active && vehicle.sizeClass == SizeClass::Car && vehicle.kind != VehicleKind::Motorbike &&
               !vehicle.lineBus && !vehicleSpec(vehicle.kind).emergency;
    }
}

Tour::Tour(const World& world, std::size_t vehicleCount, std::size_t pedestrianCount) : world_(world)
{
    scout(vehicleCount, pedestrianCount);

    caption(cityStart + 0.6f, driveStart - 0.6f, "A Smart 3D City",
            "Roads, buildings, vehicles and people, all built from 3D transformed shapes");
    caption(driveStart + 0.6f, 36.0f, "Driving My Car",
            "Digital traffic signals: red, amber, green and turn arrows");
    caption(peopleStart + 0.5f, shadingStart - 0.5f, "People at the Crossings",
            "Walk signals: waiting at the kerb, crossing on green");
    caption(shadingStart + 0.4f, 61.3f, "Flat Shading", "One normal per face");
    caption(61.4f, 64.6f, "Gouraud Shading", "Lighting at each vertex, colours interpolated");
    caption(64.7f, dayStart - 0.5f, "Phong Shading", "Normals interpolated, lighting at each pixel");
    caption(dayStart + 0.5f, weatherStart - 0.5f, "Day and Night",
            "Sun and moon are directional lights; street lamps switch on at dusk");
    caption(weatherStart + 0.5f, rayStart - 0.5f, "Weather", "Clouds gather, rain falls and the roads get wet");
    caption(rayStart + 0.5f, 104.4f, "Ray Tracing", "Switched on from the panel, with a confirmation");
    caption(105.0f, length - 1.4f, "Ray-Traced Reflections",
            "Street lamps, signals and headlights mirrored in the wet road");
}

void Tour::caption(float start, float end, const std::string& title, const std::string& subtitle)
{
    captions_.push_back({start, end, title, subtitle});
}

void Tour::step(TrafficSystem& traffic, PedestrianSystem& pedestrians)
{
    static const std::vector<Guest> noGuests;
    traffic.setGuests(noGuests);
    pedestrians.setGuests(noGuests);
    traffic.update(simulationStep);
    pedestrians.update(simulationStep, traffic);
}

void Tour::scout(std::size_t vehicleCount, std::size_t pedestrianCount)
{
    // The same city as the video's, run on its own: every 0.1 s, where each
    // vehicle is and how fast it goes, and how many people are on each
    // crossing.
    TrafficSystem traffic(vehicleCount);
    PedestrianSystem people(world_, traffic, pedestrianCount);
    constexpr int stepsPerSample = 6;
    constexpr float sampleSeconds = stepsPerSample * simulationStep;
    constexpr float maximumWarm = 900.0f;
    const int samples = static_cast<int>((maximumWarm + length) / sampleSeconds);

    const std::size_t vehicles = traffic.vehicles().size();
    const std::size_t crossings = traffic.crossings().size();
    std::vector<std::vector<glm::vec3>> tracks(vehicles);   // x, z, speed
    std::vector<std::vector<int>> onBand(crossings);
    for (int sample = 0; sample < samples; ++sample)
    {
        for (int index = 0; index < stepsPerSample; ++index)
            step(traffic, people);
        for (std::size_t index = 0; index < vehicles; ++index)
        {
            const Vehicle& vehicle = traffic.vehicles()[index];
            tracks[index].push_back({vehicle.position.x, vehicle.position.z, vehicle.currentSpeed});
        }
        const std::vector<CrossingState>& states = traffic.crossingStates();
        for (std::size_t index = 0; index < crossings; ++index)
            onBand[index].push_back(index < states.size() ? states[index].onBand + states[index].waiting : 0);
    }
    const auto timeOf = [](int sample) { return static_cast<float>(sample + 1) * sampleSeconds; };
    const auto sampleOf = [](float seconds) { return std::max(0, static_cast<int>(seconds / sampleSeconds) - 1); };

    // The drive: waiting at a red light at a signalised junction when the
    // camera reaches the car, green a few seconds later, then on through
    // the next roundabout and out of it before the people's part.
    std::vector<glm::vec2> signalised;
    std::vector<glm::vec2> roundabouts;
    for (const Junction& junction : traffic.network().junctions())
    {
        if (junction.isSignalised())
            signalised.push_back(junction.centre);
        if (junction.type == JunctionType::Roundabout)
            roundabouts.push_back(junction.centre);
    }
    float bestScore = 1.0e9f;
    for (std::size_t index = 0; index < vehicles; ++index)
    {
        if (!drivable(traffic.vehicles()[index]))
            continue;
        const std::vector<glm::vec3>& track = tracks[index];
        const auto distance = [&track](int sample, glm::vec2 to)
        {
            return glm::length(glm::vec2 {track[static_cast<std::size_t>(sample)].x, track[static_cast<std::size_t>(sample)].y} - to);
        };
        const int count = static_cast<int>(track.size());
        for (const glm::vec2& junction : signalised)
        {
            const auto waiting = [&](int sample)
            {
                const float away = distance(sample, junction);
                return track[static_cast<std::size_t>(sample)].z < 0.3f && away > 12.0f && away < 40.0f;
            };
            for (int first = 1; first < count; ++first)
            {
                if (!waiting(first) || waiting(first - 1))
                    continue;
                int last = first;
                while (last + 1 < count && waiting(last + 1))
                    ++last;
                const float stopped = static_cast<float>(last - first + 1) * sampleSeconds;
                const float warm = timeOf(last) - greenAt;
                if (stopped < 2.5f || timeOf(first) - warm > chaseFrom - 0.5f || warm < 3.0f || warm > maximumWarm)
                    continue;

                int through = -1;
                for (int sample = last; sample < std::min(count, last + sampleOf(7.0f)) && through < 0; ++sample)
                    through = distance(sample, junction) < 9.0f ? sample : -1;
                if (through < 0)
                    continue;
                int reached = -1;
                glm::vec2 roundaboutCentre {0.0f};
                for (int sample = through; sample < std::min(count, through + sampleOf(16.0f)) && reached < 0; ++sample)
                {
                    for (const glm::vec2& centre : roundabouts)
                    {
                        if (reached < 0 && distance(sample, centre) < 15.0f)
                        {
                            reached = sample;
                            roundaboutCentre = centre;
                        }
                    }
                }
                if (reached < 0)
                    continue;
                int left = -1;
                for (int sample = reached; sample < count && left < 0; ++sample)
                    left = distance(sample, roundaboutCentre) > 30.0f ? sample : -1;
                const float leftAt = left < 0 ? 1.0e9f : timeOf(left) - warm;
                const float reachedAt = timeOf(reached) - warm;
                if (debugging())
                    std::printf("  vehicle %zu waits %.1f s at (%.0f, %.0f), green at %.1f, out of the roundabout at video %.1f s\n",
                                index, stopped, junction.x, junction.y, timeOf(last), leftAt);
                if (reachedAt > peopleStart - 3.5f)
                    continue;

                // The central crossroads first (its signals are the showpiece),
                // then the earliest.
                const bool central = glm::length(junction) < 1.0f;
                const float score = leftAt + (central ? 0.0f : 120.0f);
                if (score < bestScore)
                {
                    bestScore = score;
                    warmSeconds_ = warm;
                    drivenVehicle_ = index;
                    roundabout_ = roundaboutCentre;
                    driveFound_ = true;
                    char line[256];
                    std::snprintf(line, sizeof line,
                                  "Tour: your car replaces vehicle %zu (%s). The city runs %.1f s first. It waits %.1f s at the "
                                  "signals at (%.0f, %.0f), and leaves the roundabout at (%.0f, %.0f) at %.1f s.\n",
                                  index, vehicleKindName(traffic.vehicles()[index].kind), warm, stopped, junction.x,
                                  junction.y, roundaboutCentre.x, roundaboutCentre.y, leftAt);
                    report_ = line;
                }
            }
        }
    }

    if (!driveFound_)
    {
        for (std::size_t index = 0; index < vehicles; ++index)
        {
            if (drivable(traffic.vehicles()[index]))
            {
                drivenVehicle_ = index;
                break;
            }
        }
        report_ = "Tour: no car stops at the crossroads and then takes the roundabout in time; driving vehicle " +
                  std::to_string(drivenVehicle_) + " anyway.\n";
    }

    // The people's part: the signalised crossing with the most people on it.
    const int from = sampleOf(warmSeconds_ + peopleStart + 0.5f);
    const int to = std::min(samples - 1, sampleOf(warmSeconds_ + shadingStart - 0.5f));
    int mostPeople = -1;
    for (std::size_t index = 0; index < crossings; ++index)
    {
        if (!traffic.crossings()[index].signalised)
            continue;
        int total = 0;
        for (int sample = from; sample <= to; ++sample)
            total += onBand[index][static_cast<std::size_t>(sample)];
        if (total > mostPeople)
        {
            mostPeople = total;
            crossing_ = index;
        }
    }
    char line[160];
    const Crossing& chosen = traffic.crossings()[crossing_];
    std::snprintf(line, sizeof line, "Tour: people cross at crossing %zu (junction %zu, arm %d), %.1f people at or on it on average.\n",
                  crossing_, chosen.junction, chosen.arm,
                  static_cast<float>(mostPeople) / static_cast<float>(std::max(1, to - from + 1)));
    report_ += line;
}

void Tour::direct(int frame, Camera& camera, const PlayerView& player, const TrafficSystem& traffic,
                  DayNight& dayNight, Weather& weather, int width, int height, TourControls& controls)
{
    const float t = static_cast<float>(frame) / framesPerSecond;
    const float previous = static_cast<float>(frame - 1) / framesPerSecond;

    // Fade in, dip to black at every cut, fade out.
    float brightness = smooth(progress(t, 0.0f, 0.8f)) * (1.0f - smooth(progress(t, length - 1.4f, length - 0.1f)));
    for (float cut : cuts)
        brightness *= smooth(std::abs(t - cut) / dipSeconds);
    controls.brightness = brightness;
    controls.hud = t >= cityStart;
    controls.cursorShown = false;
    controls.cursorPress = 0.0f;
    controls.shadingMode = 2;

    if (frame == 0)
    {
        dayNight.setTime(afternoon);
        weather.set(WeatherKind::Clear, 0.0f);
    }

    const float w = static_cast<float>(width);
    const float h = static_cast<float>(height);

    if (t < cityStart)
    {
        // Title: high over the north-west of the city, then down onto the
        // course billboard.
        const float s = 0.05f * progress(t, 0.0f, 10.2f) + 0.95f * smoother(progress(t, 0.0f, 10.2f));
        const glm::vec3 position = bezier({-190.0f, 95.0f, 150.0f}, {-150.0f, 70.0f, 95.0f}, {-62.0f, 11.0f, 12.0f},
                                          billboardCamera, s);
        const glm::vec3 target = glm::mix(glm::vec3 {-20.0f, 0.0f, -10.0f}, billboardMiddle, smooth(progress(s, 0.45f, 0.97f)));
        camera.lookFrom(position, target);
        return;
    }

    if (t < driveStart)
    {
        // Crane up from the billboard and swing round over the city: out
        // over the road (the pine behind the billboard stays behind), and
        // up before out.
        const float s = smoother(progress(t, cityStart, driveStart - 0.2f));
        const glm::vec2 start {billboardCamera.x, billboardCamera.z};
        const float startRadius = glm::length(start);
        const float startAngle = glm::degrees(std::atan2(start.y, start.x));
        const glm::vec3 position = orbit(glm::vec3 {0.0f}, glm::mix(startRadius, 140.0f, s),
                                         glm::mix(billboardCamera.y, 85.0f, smooth(progress(s, 0.0f, 0.75f))),
                                         glm::mix(startAngle, startAngle - 60.0f, s));
        const glm::vec3 target = glm::mix(billboardMiddle, glm::vec3 {0.0f}, smooth(progress(s, 0.0f, 0.6f)));
        camera.lookFrom(position, target);
        return;
    }

    if (t < peopleStart)
    {
        // Your car: swoop down in behind it, then a chase camera of the
        // video's own (higher than the game's, and out over the road, clear
        // of the roadside trees; it swings round behind the car smoothly),
        // then the driver's seat on the way into the roundabout.
        const glm::vec3 car = player.carPosition;
        if (t < driveStart + 0.5f / framesPerSecond)
            chaseYaw_ = player.carYawDegrees;
        const float turn = std::fmod(player.carYawDegrees - chaseYaw_ + 540.0f, 360.0f) - 180.0f;
        chaseYaw_ += turn * std::min(1.0f, 2.5f / framesPerSecond);
        const glm::vec3 forward = headingVector(chaseYaw_);
        const glm::vec3 left {forward.z, 0.0f, -forward.x};
        const glm::vec3 chasePosition = car - forward * 9.5f + left * 2.2f + glm::vec3 {0.0f, 6.0f, 0.0f};
        const glm::vec3 chaseTarget = car + forward * 6.0f + glm::vec3 {0.0f, 0.6f, 0.0f};
        controls.playerPanel = t >= chaseFrom;
        const float toRoundabout = glm::length(glm::vec2 {car.x, car.z} - roundabout_);
        if (driverViewAt_ < 0.0f && ((t > greenAt + 1.0f && toRoundabout < 55.0f) || t > peopleStart - 9.0f))
        {
            camera.togglePlayer(player);
            if (camera.mode() == CameraMode::PlayerChase)
                camera.togglePlayerView();
            driverViewAt_ = t;
            captions_[1].end = t - 0.2f;
            caption(t + 0.3f, peopleStart - 0.6f, "Driver View", "From the driver's seat, over the bonnet");
        }
        if (driverViewAt_ >= 0.0f)
            return;
        const float s = smoother(progress(t, driveStart, chaseFrom));
        const glm::vec3 high = car - forward * 22.0f + left * 16.0f + glm::vec3 {0.0f, 18.0f, 0.0f};
        camera.lookFrom(glm::mix(high, chasePosition, s), glm::mix(car + glm::vec3 {0.0f, 1.0f, 0.0f}, chaseTarget, s));
        return;
    }

    if (t < shadingStart)
    {
        // People crossing: from the sidewalk beside the crossing, looking
        // across the band, easing in a little.
        controls.playerPanel = false;
        if (camera.onPlayer())
            camera.togglePlayer(player);
        const Crossing& crossing = traffic.crossings()[crossing_];
        const float s = smooth(progress(t, peopleStart, shadingStart));
        const glm::vec2 middle = crossing.point(0.5f * (crossing.from + crossing.to), 0.0f);
        const glm::vec2 stand = crossing.point(crossing.to + 1.2f, glm::mix(13.0f, 11.0f, s));
        camera.lookFrom({stand.x, 4.6f, stand.y}, {middle.x, 0.6f, middle.y});
        return;
    }

    if (t < dayStart)
    {
        // Round the fountain in the roundabout: flat, Gouraud, Phong.
        controls.shadingMode = t < 61.33f ? 0 : (t < 64.67f ? 1 : 2);
        const float s = progress(t, shadingStart, dayStart);
        const glm::vec2 fountain = world_.fountains().empty() ? glm::vec2 {100.0f, 0.0f} : world_.fountains().front().centre;
        const glm::vec3 centre {fountain.x, 0.0f, fountain.y};
        camera.lookFrom(orbit(centre, glm::mix(9.0f, 7.6f, s), glm::mix(3.4f, 2.8f, s), glm::mix(200.0f, 290.0f, s)),
                        centre + glm::vec3 {0.0f, 1.2f, 0.0f});
        return;
    }

    if (t < weatherStart)
    {
        // Afternoon to night from high over the city: the pointer clicks
        // EVENING, then NIGHT.
        const float s = progress(t, dayStart, weatherStart);
        camera.lookFrom(orbit(glm::vec3 {0.0f}, 150.0f, 80.0f, glm::mix(230.0f, 262.0f, smooth(s))), {0.0f, 5.0f, 20.0f});
        const glm::vec2 rest {0.62f * w, 0.42f * h};
        const std::vector<CursorKey> keys = {{dayStart + 0.6f, rest},
                                             {dayStart + 1.8f, timeButton(3, width)},
                                             {dayStart + 7.4f, timeButton(3, width)},
                                             {dayStart + 8.3f, timeButton(4, width)},
                                             {dayStart + 9.4f, timeButton(4, width)},
                                             {dayStart + 10.6f, rest + glm::vec2 {0.0f, 60.0f}}};
        controls.cursorShown = t > dayStart + 0.6f && t < dayStart + 10.6f;
        controls.cursor = cursorAt(t, keys);
        const float eveningClick = dayStart + 2.0f;
        const float nightClick = dayStart + 8.5f;
        controls.cursorPress = std::max(pressAt(t, eveningClick), pressAt(t, nightClick));
        if (t >= eveningClick && t < nightClick)
            dayNight.setTime(glm::mix(afternoon, evening, smooth(progress(t, eveningClick, eveningClick + 5.5f))));
        else if (t >= nightClick)
            dayNight.setTime(glm::mix(evening, night, smooth(progress(t, nightClick, nightClick + 5.5f))));
        return;
    }

    if (t < rayStart)
    {
        // Night on a shopping street: the pointer clicks CLOUDY, then RAIN.
        if (crossed(previous, t, weatherStart))
            dayNight.setTime(night);
        const float s = smooth(progress(t, weatherStart, rayStart));
        camera.lookFrom({glm::mix(-22.0f, -29.0f, s), 2.2f, -9.6f}, {-72.0f, 1.6f, -1.0f});
        const glm::vec2 rest {0.60f * w, 0.40f * h};
        const std::vector<CursorKey> keys = {{weatherStart + 0.8f, rest},
                                             {weatherStart + 1.8f, weatherButton(1, width)},
                                             {weatherStart + 4.8f, weatherButton(1, width)},
                                             {weatherStart + 5.4f, weatherButton(2, width)},
                                             {weatherStart + 6.3f, weatherButton(2, width)},
                                             {weatherStart + 7.3f, rest}};
        controls.cursorShown = t > weatherStart + 0.8f && t < weatherStart + 7.3f;
        controls.cursor = cursorAt(t, keys);
        const float cloudyClick = weatherStart + 2.0f;
        const float rainClick = weatherStart + 5.5f;
        controls.cursorPress = std::max(pressAt(t, cloudyClick), pressAt(t, rainClick));
        if (crossed(previous, t, cloudyClick))
            weather.blendTo(WeatherKind::Cloudy);
        if (crossed(previous, t, rainClick))
            weather.blendTo(WeatherKind::Rain);
        return;
    }

    // Ray tracing, at night in the rain, the road soaked: the pointer clicks
    // the RAY TRACING button, then YES; then a slow move along the wet road.
    if (crossed(previous, t, rayStart))
        weather.set(WeatherKind::Rain, 1.0f);
    const float s = smooth(progress(t, rayStart, length));
    camera.lookFrom({glm::mix(-28.0f, -21.0f, s), 1.1f, -9.6f}, {0.0f, 1.3f, glm::mix(-3.0f, -1.0f, s)});
    const glm::vec2 rest {0.58f * w, 0.55f * h};
    const float rayClick = rayStart + 2.7f;
    const float yesClick = rayStart + 4.4f;
    const std::vector<CursorKey> keys = {{rayStart + 0.8f, rest},
                                         {rayStart + 2.4f, rayButton(width)},
                                         {rayClick + 0.3f, rayButton(width)},
                                         {yesClick - 0.4f, yesButton(width, height)},
                                         {yesClick + 0.3f, yesButton(width, height)},
                                         {yesClick + 1.1f, yesButton(width, height) + glm::vec2 {260.0f, 140.0f}}};
    controls.cursorShown = t > rayStart + 0.8f && t < yesClick + 1.1f;
    controls.cursor = cursorAt(t, keys);
    controls.cursorPress = std::max(pressAt(t, rayClick), pressAt(t, yesClick));
    if (crossed(previous, t, rayClick))
        controls.confirmOpen = true;
    if (controls.confirmOpen)
    {
        const int over = ConfirmDialog::at(controls.cursor.x, controls.cursor.y, width, height);
        controls.confirmHover = over == 0 || over == 1 ? over : -1;
    }
    if (crossed(previous, t, yesClick))
    {
        controls.enhanced = true;
        controls.confirmOpen = false;
        controls.confirmHover = -1;
    }
}

bool Tour::writeCaptions(const std::string& path) const
{
    std::ofstream out(path);
    if (!out)
        return false;
    for (const TourCaption& entry : captions_)
        out << entry.start << '\t' << entry.end << '\t' << entry.title << '\t' << entry.subtitle << '\n';
    return static_cast<bool>(out);
}
