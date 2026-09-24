#include "Camera.h"

#include <GLFW/glfw3.h>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

Camera::Camera()
{
    updateVectors();
}

void Camera::processKeyboard(GLFWwindow* window, float dt)
{
    if (mode_ != CameraMode::Free)
        return;

    // Shift moves four times faster: the city is 400 m across.
    const bool boost = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;
    const float distance = speed_ * dt * (boost ? 4.0f : 1.0f);
    const glm::vec3 right = glm::normalize(glm::cross(front_, up_));

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        position_ += front_ * distance;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        position_ -= front_ * distance;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        position_ -= right * distance;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        position_ += right * distance;
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
        position_ -= up_ * distance;
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)
        position_ += up_ * distance;

    position_.y = glm::clamp(position_.y, 1.0f, 400.0f);
    saveFreeCamera();
}

void Camera::processMouse(float xOffset, float yOffset)
{
    if (mode_ != CameraMode::Free)
        return;

    yaw_ += xOffset * sensitivity_;
    pitch_ += yOffset * sensitivity_;
    pitch_ = glm::clamp(pitch_, -85.0f, 85.0f);
    updateVectors();
    saveFreeCamera();
}

namespace
{
    // Critically damped spring towards `target`, integrated exactly for any
    // frame time (the "SmoothDamp" form, Game Programming Gems 4, 1.10).
    // `smoothTime` is roughly the time it takes to close most of the gap.
    glm::vec3 smoothDamp(const glm::vec3& current, const glm::vec3& target,
                         glm::vec3& velocity, float smoothTime, float dt)
    {
        const float omega = 2.0f / smoothTime;
        const float x = omega * dt;
        const float decay = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
        const glm::vec3 change = current - target;
        const glm::vec3 temp = (velocity + omega * change) * dt;
        velocity = (velocity - omega * temp) * decay;
        return target + (change + temp) * decay;
    }
}

void Camera::update(float dt, const std::vector<VehiclePose>& vehicles)
{
    // A stalled frame (window dragged, breakpoint) must not fling the camera.
    dt = glm::clamp(dt, 0.0f, 0.25f);

    if (mode_ == CameraMode::Top)
    {
        // High enough to see the whole city, out to the sidewalk round the
        // ring road (about 215 m either way at a 55 degree field of view).
        position_ = {0.0f, 430.0f, 0.01f};
        front_ = glm::normalize(glm::vec3{0.0f, 0.0f, 0.0f} - position_);
        return;
    }

    if ((mode_ != CameraMode::Follow && mode_ != CameraMode::Driver) || vehicles.empty())
    {
        followSettled_ = false;
        return;
    }

    followedVehicleIndex_ %= vehicles.size();
    const VehiclePose& vehicle = vehicles[followedVehicleIndex_];
    const float yaw = glm::radians(vehicle.yawDegrees);
    const glm::vec3 direction {std::sin(yaw), 0.0f, std::cos(yaw)};

    if (mode_ == CameraMode::Driver)
    {
        // The driver's eye is rigidly part of the car: no smoothing, or the
        // view would swim against the dashboard.
        position_ = vehicle.position + direction * 0.08f + glm::vec3{0.0f, 1.30f, 0.0f};
        const glm::vec3 target = vehicle.position + direction * 14.0f + glm::vec3{0.0f, 1.12f, 0.0f};
        front_ = glm::normalize(target - position_);
        followSettled_ = false;
        return;
    }

    const glm::vec3 desiredPosition = vehicle.position - direction * 8.5f + glm::vec3{0.0f, 4.2f, 0.0f};
    const glm::vec3 target = vehicle.position + direction * 2.2f + glm::vec3{0.0f, 0.9f, 0.0f};

    // Entering follow mode (or switching car) starts the spring from where
    // the camera already is, at rest.
    if (!followSettled_)
    {
        followVelocity_ = glm::vec3 {0.0f};
        followSettled_ = true;
    }
    position_ = smoothDamp(position_, desiredPosition, followVelocity_, 0.45f, dt);
    front_ = glm::normalize(target - position_);
}

void Camera::cycleMode()
{
    if (mode_ == CameraMode::Free)
    {
        saveFreeCamera();
        mode_ = CameraMode::Top;
    }
    else if (mode_ == CameraMode::Top)
    {
        mode_ = CameraMode::Follow;
    }
    else if (mode_ == CameraMode::Follow)
    {
        mode_ = CameraMode::Driver;
    }
    else
    {
        mode_ = CameraMode::Free;
        restoreFreeCamera();
    }
}

void Camera::nextFollow(std::size_t vehicleCount)
{
    if (vehicleCount == 0)
        return;
    if (mode_ == CameraMode::Free)
        saveFreeCamera();
    if (mode_ == CameraMode::Follow || mode_ == CameraMode::Driver)
        followedVehicleIndex_ = (followedVehicleIndex_ + 1) % vehicleCount;
    else
    {
        mode_ = CameraMode::Follow;
        followedVehicleIndex_ %= vehicleCount;
    }
}

void Camera::toggleDriverView(std::size_t vehicleCount)
{
    if (vehicleCount == 0)
        return;

    if (mode_ == CameraMode::Driver)
    {
        mode_ = CameraMode::Free;
        restoreFreeCamera();
        return;
    }

    if (mode_ == CameraMode::Free)
        saveFreeCamera();
    followedVehicleIndex_ %= vehicleCount;
    mode_ = CameraMode::Driver;
}

void Camera::reset()
{
    position_ = {20.0f, 17.0f, 24.0f};
    yaw_ = -130.0f;
    pitch_ = -22.0f;
    mode_ = CameraMode::Free;
    followedVehicleIndex_ = 0;
    updateVectors();
    saveFreeCamera();
}

void Camera::setFreePose(const glm::vec3& position, float yawDegrees, float pitchDegrees)
{
    mode_ = CameraMode::Free;
    position_ = position;
    yaw_ = yawDegrees;
    pitch_ = glm::clamp(pitchDegrees, -85.0f, 85.0f);
    updateVectors();
    saveFreeCamera();
}

glm::mat4 Camera::viewMatrix() const
{
    const glm::vec3 viewUp = mode_ == CameraMode::Top ? glm::vec3{0.0f, 0.0f, -1.0f} : up_;
    return glm::lookAt(position_, position_ + front_, viewUp);
}

glm::mat4 Camera::projectionMatrix(float aspectRatio) const
{
    const float fov = mode_ == CameraMode::Driver ? 68.0f : fieldOfView_;
    // The far plane reaches the fogged horizon; fog hides everything beyond it.
    // The near plane moves out as the camera climbs. With a 24-bit depth
    // buffer the depth step grows as distance^2 / near: at 0.1 m it would be
    // 11 cm at 430 m, enough for the ground to show through the asphalt from
    // the top view. Nothing is ever within a few metres of a high camera, so a
    // near plane of about 1/80 of the height costs nothing and keeps the step
    // to millimetres; at street level and in the driver's seat it stays 0.1 m.
    const float nearPlane = glm::clamp(0.1f + 0.012f * position_.y, 0.1f, 6.0f);
    return glm::perspective(glm::radians(fov), aspectRatio, nearPlane, 1200.0f);
}

void Camera::updateVectors()
{
    glm::vec3 direction;
    direction.x = std::cos(glm::radians(yaw_)) * std::cos(glm::radians(pitch_));
    direction.y = std::sin(glm::radians(pitch_));
    direction.z = std::sin(glm::radians(yaw_)) * std::cos(glm::radians(pitch_));
    front_ = glm::normalize(direction);
}

std::string Camera::modeName() const
{
    switch (mode_)
    {
    case CameraMode::Free: return "FREE";
    case CameraMode::Top: return "TOP";
    case CameraMode::Follow: return "FOLLOW CAR " + std::to_string(followedVehicleIndex_ + 1);
    case CameraMode::Driver: return "DRIVER CAR " + std::to_string(followedVehicleIndex_ + 1);
    }
    return "UNKNOWN";
}

void Camera::saveFreeCamera()
{
    if (mode_ != CameraMode::Free)
        return;
    savedFreePosition_ = position_;
    savedFreeYaw_ = yaw_;
    savedFreePitch_ = pitch_;
}

void Camera::restoreFreeCamera()
{
    position_ = savedFreePosition_;
    yaw_ = savedFreeYaw_;
    pitch_ = savedFreePitch_;
    updateVectors();
}
