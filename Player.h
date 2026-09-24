#pragma once

#include "Collision.h"
#include "Mannequin.h"
#include "World.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <string>
#include <vector>

// The player: one car you can drive, and yourself on foot. Both move in the
// same fixed 1/60 s steps as the traffic and are drawn blended between the
// last two steps, exactly like the AI cars, so they are as smooth.
//
// Headings use the vehicles' convention: forward = (sin yaw, cos yaw) in world
// (x, z), and a larger yaw is a turn to the left.

struct PlayerInput
{
    // Driving: each 0..1, steer -1 (right) .. +1 (left).
    float throttle = 0.0f;
    float brake = 0.0f;    // brakes; once stopped, reverses
    float steer = 0.0f;
    bool handbrake = false;
    bool boost = false;

    // Walking: x = to the right, y = forward, relative to where you look.
    glm::vec2 walk {0.0f};
    bool run = false;
    glm::vec2 lookForward {0.0f, 1.0f};   // on the ground, unit length
};

// What the camera, the renderer and the HUD need, blended for this frame.
struct PlayerView
{
    bool walking = false;

    glm::vec3 carPosition {0.0f};
    float carYawDegrees = 0.0f;
    float carSteerDegrees = 0.0f;
    float carWheelDegrees = 0.0f;
    float carSpeed = 0.0f;               // m/s, negative in reverse
    float lateralAcceleration = 0.0f;    // m/s^2, positive to the left
    float longitudinalAcceleration = 0.0f;

    glm::vec3 walkerPosition {0.0f};     // feet
    float walkerYawDegrees = 0.0f;
    float walkerSpeed = 0.0f;            // m/s over the ground
    float walkerPhase = 0.0f;            // gait cycle (Mannequin), from the distance walked
    FootLock walkerFeet[2];              // where the feet are planted
};

class Player
{
public:
    explicit Player(const World& world);

    void reset();

    // Put the car, or you on foot, somewhere at rest (used by --player-test).
    void placeCar(glm::vec2 position, float yawDegrees);
    void placeWalker(glm::vec2 position, float yawDegrees);

    // One fixed step. `traffic` holds the AI bodies as they stand after this
    // step's traffic update; the player is pushed out of any it runs into.
    void step(float dt, const PlayerInput& input, const std::vector<OrientedBox>& traffic);

    // Blended pose for drawing (alpha as in TrafficSystem::interpolatePoses).
    PlayerView view(float alpha) const;

    // F: get out of the car (only when it has nearly stopped), or back in
    // (only when standing next to it). Returns false, with a message, if not.
    bool toggleOnFoot();
    bool walking() const { return walking_; }

    // The shapes the AI must keep clear of: the car always (parked or driven)
    // and the person when on foot.
    void guests(std::vector<Guest>& out) const;

    OrientedBox carBody() const;
    Circle walkerBody() const;
    float speed() const { return car_.speed; }

    // A short line for the HUD, and how long ago it was set.
    const std::string& message() const { return message_; }
    float messageAge() const { return messageAge_; }

    // Contacts since the last reset: with the world, and with AI cars.
    int worldContacts() const { return worldContacts_; }
    int trafficContacts() const { return trafficContacts_; }

    static constexpr float carHalfWidth = 0.94f;
    static constexpr float carHalfLength = 2.03f;
    static constexpr float walkerRadius = 0.3f;
    static constexpr float eyeHeight = 1.65f;
    static constexpr float playerHeight = 1.78f;

private:
    struct CarState
    {
        glm::vec2 position {0.0f};
        float height = 0.0f;
        float yawDegrees = 0.0f;
        float steerDegrees = 0.0f;
        float wheelDegrees = 0.0f;
        float speed = 0.0f;
        float lateralAcceleration = 0.0f;
        float longitudinalAcceleration = 0.0f;
    };
    struct WalkerState
    {
        glm::vec2 position {0.0f};
        float height = 0.0f;
        float yawDegrees = 0.0f;
        glm::vec2 velocity {0.0f};
        float speed = 0.0f;
        float phase = 0.0f;
        FootLock lock[2];
    };

    const World& world_;
    CarState car_;
    CarState previousCar_;
    WalkerState walker_;
    WalkerState previousWalker_;
    bool walking_ = false;

    std::string message_;
    float messageAge_ = 100.0f;
    int worldContacts_ = 0;
    int trafficContacts_ = 0;

    void stepCar(float dt, const PlayerInput& input, const std::vector<OrientedBox>& traffic);
    void stepWalker(float dt, const PlayerInput& input, const std::vector<OrientedBox>& traffic);
    void say(const std::string& text);
};
