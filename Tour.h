#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstddef>
#include <string>
#include <vector>

class Camera;
class DayNight;
class PedestrianSystem;
class TrafficSystem;
class Weather;
class World;
struct PlayerView;

// What the demo video sets in the application for one frame.
struct TourControls
{
    int shadingMode = 2;
    bool enhanced = false;
    bool confirmOpen = false;
    int confirmHover = -1;
    bool hud = false;
    bool cursorShown = false;
    glm::vec2 cursor {0.0f};    // framebuffer pixels, y down
    float cursorPress = 0.0f;   // 0..1 through a click's ring, 0 for none
    float brightness = 1.0f;    // 0 in the dip to black between two parts
    bool playerPanel = false;   // the speed panel, while the camera watches your car
};

// A caption for the finished video (drawn on by delivery/video/make_video.py).
struct TourCaption
{
    float start = 0.0f;
    float end = 0.0f;
    std::string title;
    std::string subtitle;
};

// The demo video (--tour): a fixed two-minute script of camera moves, clicks
// and captions. Every frame advances the city by exactly two 1/60 s steps,
// so the video runs at an even 30 frames a second of city time however long
// a frame takes to draw.
//
// Your car is driven by the autopilot: it takes the place of one AI car
// (Player::followVehicle), which is then not drawn. Before the first frame
// the tour runs the same city once on its own (the simulation is
// deterministic) to choose that car and how long the city runs first, so the
// car stops at a red light at the central crossroads a few seconds into its
// part and then drives through the roundabout east of it.
class Tour
{
public:
    static constexpr float framesPerSecond = 30.0f;
    static constexpr int stepsPerFrame = 2;
    static constexpr float simulationStep = 1.0f / 60.0f;
    static constexpr float length = 120.0f;

    Tour(const World& world, std::size_t vehicleCount, std::size_t pedestrianCount);

    // One step of the city as the video runs it: no guests, because your
    // car is the AI car it replaces.
    static void step(TrafficSystem& traffic, PedestrianSystem& pedestrians);

    float warmSeconds() const { return warmSeconds_; }
    std::size_t drivenVehicle() const { return drivenVehicle_; }
    int frameCount() const { return static_cast<int>(length * framesPerSecond); }
    const std::string& report() const { return report_; }

    // Before the camera update and the drawing of frame `frame`.
    void direct(int frame, Camera& camera, const PlayerView& player, const TrafficSystem& traffic,
                DayNight& dayNight, Weather& weather, int width, int height, TourControls& controls);

    const std::vector<TourCaption>& captions() const { return captions_; }
    bool writeCaptions(const std::string& path) const;

private:
    const World& world_;
    float warmSeconds_ = 20.0f;
    std::size_t drivenVehicle_ = 0;
    std::size_t crossing_ = 0;       // where the people cross in their part
    glm::vec2 roundabout_ {100.0f, 0.0f};   // the one your car drives through
    std::string report_;

    std::vector<TourCaption> captions_;
    float driverViewAt_ = -1.0f;     // when the drive switched to the driver's seat
    float chaseYaw_ = 0.0f;          // the chase camera's heading, easing after the car's
    bool driveFound_ = false;

    void scout(std::size_t vehicleCount, std::size_t pedestrianCount);
    void caption(float start, float end, const std::string& title, const std::string& subtitle);
};
