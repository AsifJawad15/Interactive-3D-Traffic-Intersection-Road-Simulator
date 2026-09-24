#pragma once

#include "Player.h"
#include "Simulation.h"

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstddef>
#include <string>

enum class CameraMode
{
    Free,
    Top,
    Follow,        // behind an AI car
    Driver,        // in an AI car
    PlayerChase,   // above and behind the player's car
    PlayerSeat,    // the player's driver view, over the bonnet
    OnFoot         // the player's own eyes
};

struct GLFWwindow;

class Camera
{
public:
    Camera();

    void processKeyboard(GLFWwindow* window, float dt);
    void processMouse(float xOffset, float yOffset);
    // `vehicles` and `player` are the interpolated poses for this frame, so a
    // camera that rides on a car moves exactly as smoothly as the car is drawn.
    void update(float dt, const std::vector<VehiclePose>& vehicles, const PlayerView& player);

    // C: lock the camera onto the player (the car, or yourself on foot), or
    // let go again and return to the free camera where it was.
    void togglePlayer(const PlayerView& player);
    bool onPlayer() const;
    // V while on the player's car: chase view <-> driver view.
    void togglePlayerView();
    // After F: follow the player out of the car, or back into it.
    void playerChangedFoot(const PlayerView& player);

    // M: the top view over the whole city, and back.
    void toggleTop();

    void nextFollow(std::size_t vehicleCount);
    void toggleDriverView(std::size_t vehicleCount);
    void reset();

    // Places the free camera at an exact pose. Used by --capture so that the
    // same view can be rendered before and after a change.
    void setFreePose(const glm::vec3& position, float yawDegrees, float pitchDegrees);

    glm::mat4 viewMatrix() const;
    glm::mat4 projectionMatrix(float aspectRatio) const;

    const glm::vec3& position() const { return position_; }
    float fieldOfView() const { return fieldOfView_; }
    CameraMode mode() const { return mode_; }
    std::string modeName() const;
    std::size_t followedVehicleIndex() const { return followedVehicleIndex_; }

    // Where you look, flattened onto the ground (for walking).
    glm::vec2 groundForward() const;

private:
    glm::vec3 position_ { 20.0f, 17.0f, 24.0f };
    glm::vec3 front_ { -0.6f, -0.35f, -0.7f };
    glm::vec3 up_ { 0.0f, 1.0f, 0.0f };
    glm::vec3 viewUp_ { 0.0f, 1.0f, 0.0f };   // tilted by the lean in the driver view

    float yaw_ = -130.0f;
    float pitch_ = -22.0f;
    float speed_ = 14.0f;
    float sensitivity_ = 0.09f;
    float fieldOfView_ = 55.0f;
    CameraMode mode_ = CameraMode::Free;
    CameraMode modeBeforeTop_ = CameraMode::Free;
    CameraMode playerCarView_ = CameraMode::PlayerChase;
    std::size_t followedVehicleIndex_ = 0;
    glm::vec3 savedFreePosition_ {20.0f, 17.0f, 24.0f};
    float savedFreeYaw_ = -130.0f;
    float savedFreePitch_ = -22.0f;

    // Follow and chase cameras: a critically damped spring pulls the camera
    // towards its spot behind the car. It settles as fast as possible without
    // overshoot, and its velocity carries over from frame to frame, so a jerky
    // frame time never makes the camera jerk.
    glm::vec3 followVelocity_ {0.0f};
    bool followSettled_ = false;

    // Driver view: head turned by the mouse (limited like a real neck), B to
    // look back, and a gentle lean under braking and cornering.
    float headYaw_ = 0.0f;
    float headPitch_ = 0.0f;
    bool lookBackHeld_ = false;
    float lookBack_ = 0.0f;
    float lookBackVelocity_ = 0.0f;
    glm::vec2 lean_ {0.0f};           // x = roll, y = pitch, degrees
    glm::vec2 leanVelocity_ {0.0f};

    void updateVectors();
    void saveFreeCamera();
    void restoreFreeCamera();
    void leaveFree();
};
