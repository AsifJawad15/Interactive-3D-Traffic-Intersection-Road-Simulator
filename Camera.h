#pragma once

#include "Simulation.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cstddef>
#include <string>

enum class CameraMode
{
    Free,
    Top,
    Follow,
    Driver
};

struct GLFWwindow;

class Camera
{
public:
    Camera();

    void processKeyboard(GLFWwindow* window, float dt);
    void processMouse(float xOffset, float yOffset);
    void update(float dt, const std::vector<Vehicle>& vehicles);
    void cycleMode();
    void nextFollow(std::size_t vehicleCount);
    void toggleDriverView(std::size_t vehicleCount);
    void reset();

    glm::mat4 viewMatrix() const;
    glm::mat4 projectionMatrix(float aspectRatio) const;

    const glm::vec3& position() const { return position_; }
    float fieldOfView() const { return fieldOfView_; }
    CameraMode mode() const { return mode_; }
    std::string modeName() const;
    std::size_t followedVehicleIndex() const { return followedVehicleIndex_; }

private:
    glm::vec3 position_ { 20.0f, 17.0f, 24.0f };
    glm::vec3 front_ { -0.6f, -0.35f, -0.7f };
    glm::vec3 up_ { 0.0f, 1.0f, 0.0f };

    float yaw_ = -130.0f;
    float pitch_ = -22.0f;
    float speed_ = 14.0f;
    float sensitivity_ = 0.09f;
    float fieldOfView_ = 55.0f;
    CameraMode mode_ = CameraMode::Free;
    std::size_t followedVehicleIndex_ = 0;
    glm::vec3 savedFreePosition_ {20.0f, 17.0f, 24.0f};
    float savedFreeYaw_ = -130.0f;
    float savedFreePitch_ = -22.0f;

    void updateVectors();
    void saveFreeCamera();
    void restoreFreeCamera();
};
