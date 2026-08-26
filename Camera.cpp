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

    const float distance = speed_ * dt;
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

    position_.y = glm::clamp(position_.y, 1.0f, 55.0f);
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

void Camera::update(float dt, const std::vector<Vehicle>& vehicles)
{
    if (mode_ == CameraMode::Top)
    {
        position_ = {0.0f, 48.0f, 0.01f};
        front_ = glm::normalize(glm::vec3{0.0f, 0.0f, 0.0f} - position_);
        return;
    }

    if ((mode_ != CameraMode::Follow && mode_ != CameraMode::Driver) || vehicles.empty())
        return;

    followedVehicleIndex_ %= vehicles.size();
    const Vehicle& vehicle = vehicles[followedVehicleIndex_];
    const float yaw = glm::radians(vehicle.yawDegrees);
    const glm::vec3 direction {std::sin(yaw), 0.0f, std::cos(yaw)};

    if (mode_ == CameraMode::Driver)
    {
        position_ = vehicle.position + direction * 0.08f + glm::vec3{0.0f, 1.30f, 0.0f};
        const glm::vec3 target = vehicle.position + direction * 14.0f + glm::vec3{0.0f, 1.12f, 0.0f};
        front_ = glm::normalize(target - position_);
        return;
    }

    const glm::vec3 desiredPosition = vehicle.position - direction * 8.5f + glm::vec3{0.0f, 4.2f, 0.0f};
    const glm::vec3 target = vehicle.position + direction * 2.2f + glm::vec3{0.0f, 0.9f, 0.0f};
    const float blend = 1.0f - std::exp(-5.0f * glm::clamp(dt, 0.0f, 0.05f));
    position_ = glm::mix(position_, desiredPosition, blend);
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

glm::mat4 Camera::viewMatrix() const
{
    const glm::vec3 viewUp = mode_ == CameraMode::Top ? glm::vec3{0.0f, 0.0f, -1.0f} : up_;
    return glm::lookAt(position_, position_ + front_, viewUp);
}

glm::mat4 Camera::projectionMatrix(float aspectRatio) const
{
    const float fov = mode_ == CameraMode::Driver ? 68.0f : fieldOfView_;
    return glm::perspective(glm::radians(fov), aspectRatio, 0.1f, 180.0f);
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
