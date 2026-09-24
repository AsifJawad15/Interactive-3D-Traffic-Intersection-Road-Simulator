#include "Camera.h"

#include <GLFW/glfw3.h>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace
{
    // Critically damped spring towards `target`, integrated exactly for any
    // frame time (the "SmoothDamp" form, Game Programming Gems 4, 1.10).
    // `smoothTime` is roughly the time it takes to close most of the gap.
    template <typename T>
    T smoothDamp(const T& current, const T& target, T& velocity, float smoothTime, float dt)
    {
        const float omega = 2.0f / smoothTime;
        const float x = omega * dt;
        const float decay = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
        const T change = current - target;
        const T temp = (velocity + omega * change) * dt;
        velocity = (velocity - omega * temp) * decay;
        return target + (change + temp) * decay;
    }

    // A vehicle heading (forward = (sin h, cos h)) as a 3D direction.
    glm::vec3 headingVector(float degrees)
    {
        const float h = glm::radians(degrees);
        return {std::sin(h), 0.0f, std::cos(h)};
    }

    bool isPlayerMode(CameraMode mode)
    {
        return mode == CameraMode::PlayerChase || mode == CameraMode::PlayerSeat || mode == CameraMode::OnFoot;
    }
}

Camera::Camera()
{
    updateVectors();
}

void Camera::processKeyboard(GLFWwindow* window, float dt)
{
    lookBackHeld_ = mode_ == CameraMode::PlayerSeat && glfwGetKey(window, GLFW_KEY_B) == GLFW_PRESS;
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
    if (mode_ == CameraMode::PlayerSeat)
    {
        // Turning the head: a larger head yaw looks to the left, the way a
        // larger heading turns. Limited to what a neck can do.
        headYaw_ = glm::clamp(headYaw_ - xOffset * sensitivity_, -70.0f, 70.0f);
        headPitch_ = glm::clamp(headPitch_ + yOffset * sensitivity_, -30.0f, 20.0f);
        return;
    }
    if (mode_ != CameraMode::Free && mode_ != CameraMode::OnFoot)
        return;

    yaw_ += xOffset * sensitivity_;
    pitch_ += yOffset * sensitivity_;
    pitch_ = glm::clamp(pitch_, -85.0f, 85.0f);
    updateVectors();
    saveFreeCamera();
}

void Camera::update(float dt, const std::vector<VehiclePose>& vehicles, const PlayerView& player)
{
    // A stalled frame (window dragged, breakpoint) must not fling the camera.
    dt = glm::clamp(dt, 0.0f, 0.25f);
    viewUp_ = up_;

    if (mode_ == CameraMode::Top)
    {
        // High enough to see the whole city, out to the sidewalk round the
        // ring road (about 215 m either way at a 55 degree field of view).
        position_ = {0.0f, 430.0f, 0.01f};
        front_ = glm::normalize(glm::vec3{0.0f, 0.0f, 0.0f} - position_);
        viewUp_ = {0.0f, 0.0f, -1.0f};
        return;
    }

    if (mode_ == CameraMode::OnFoot)
    {
        // Your own eyes: rigidly where you stand, looking where the mouse says.
        position_ = player.walkerPosition + glm::vec3{0.0f, Player::eyeHeight, 0.0f};
        updateVectors();
        return;
    }

    if (mode_ == CameraMode::PlayerSeat)
    {
        const glm::vec3 forward = headingVector(player.carYawDegrees);

        // The body is pushed out of the corner and nods under braking, eased
        // by a spring so the view never jerks. In a left turn the head tips
        // to the right, and so does the view's up direction.
        const glm::vec2 leanTarget {
            glm::clamp(player.lateralAcceleration * 0.55f, -3.0f, 3.0f),
            glm::clamp(player.longitudinalAcceleration * 0.30f, -2.5f, 1.5f)};
        lean_ = smoothDamp(lean_, leanTarget, leanVelocity_, 0.28f, dt);
        lookBack_ = smoothDamp(lookBack_, lookBackHeld_ ? 1.0f : 0.0f, lookBackVelocity_, 0.12f, dt);

        // Looking out over the bonnet from the middle of the windscreen.
        // Rigidly part of the car: no smoothing, or the view would swim
        // against the bonnet.
        position_ = player.carPosition + glm::vec3{0.0f, 1.42f, 0.0f} + forward * 0.35f;

        const float lookYaw = player.carYawDegrees + headYaw_ + 180.0f * lookBack_;
        const float lookPitch = glm::radians(headPitch_ * (1.0f - lookBack_) - 4.0f + lean_.y);
        const glm::vec3 look = headingVector(lookYaw);
        front_ = glm::normalize(glm::vec3{look.x * std::cos(lookPitch), std::sin(lookPitch), look.z * std::cos(lookPitch)});

        const glm::vec3 right = glm::normalize(glm::cross(front_, up_));
        const float roll = glm::radians(lean_.x);
        viewUp_ = glm::normalize(up_ * std::cos(roll) + right * std::sin(roll));
        followSettled_ = false;
        return;
    }

    if (mode_ == CameraMode::PlayerChase)
    {
        // Above and behind the car, looking down past it at the road ahead.
        const glm::vec3 forward = headingVector(player.carYawDegrees);
        const glm::vec3 desiredPosition = player.carPosition - forward * 8.5f + glm::vec3{0.0f, 4.8f, 0.0f};
        const glm::vec3 target = player.carPosition + forward * 4.0f + glm::vec3{0.0f, 0.8f, 0.0f};
        if (!followSettled_)
        {
            position_ = desiredPosition;
            followVelocity_ = glm::vec3 {0.0f};
            followSettled_ = true;
        }
        position_ = smoothDamp(position_, desiredPosition, followVelocity_, 0.18f, dt);
        front_ = glm::normalize(target - position_);
        return;
    }

    if ((mode_ != CameraMode::Follow && mode_ != CameraMode::Driver) || vehicles.empty())
    {
        followSettled_ = false;
        return;
    }

    followedVehicleIndex_ %= vehicles.size();
    const VehiclePose& vehicle = vehicles[followedVehicleIndex_];
    const glm::vec3 direction = headingVector(vehicle.yawDegrees);

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

bool Camera::onPlayer() const
{
    return isPlayerMode(mode_);
}

void Camera::leaveFree()
{
    if (mode_ == CameraMode::Free)
        saveFreeCamera();
}

void Camera::togglePlayer(const PlayerView& player)
{
    if (onPlayer())
    {
        mode_ = CameraMode::Free;
        restoreFreeCamera();
        return;
    }
    leaveFree();
    followSettled_ = false;
    headYaw_ = 0.0f;
    headPitch_ = 0.0f;
    playerChangedFoot(player);
}

void Camera::playerChangedFoot(const PlayerView& player)
{
    if (player.walking)
    {
        // Look the way you were facing: the camera's yaw counts from +x
        // towards +z, a heading from +z towards +x.
        mode_ = CameraMode::OnFoot;
        yaw_ = 90.0f - player.walkerYawDegrees;
        pitch_ = -5.0f;
        updateVectors();
    }
    else
    {
        mode_ = playerCarView_;
        followSettled_ = false;
    }
}

void Camera::togglePlayerView()
{
    if (mode_ != CameraMode::PlayerChase && mode_ != CameraMode::PlayerSeat)
        return;
    mode_ = mode_ == CameraMode::PlayerChase ? CameraMode::PlayerSeat : CameraMode::PlayerChase;
    playerCarView_ = mode_;
    headYaw_ = 0.0f;
    headPitch_ = 0.0f;
    followSettled_ = false;
}

void Camera::toggleTop()
{
    if (mode_ == CameraMode::Top)
    {
        mode_ = modeBeforeTop_;
        if (mode_ == CameraMode::Free)
            restoreFreeCamera();
        followSettled_ = false;
        return;
    }
    leaveFree();
    modeBeforeTop_ = mode_ == CameraMode::OnFoot ? CameraMode::OnFoot : mode_;
    mode_ = CameraMode::Top;
}

void Camera::nextFollow(std::size_t vehicleCount)
{
    if (vehicleCount == 0)
        return;
    leaveFree();
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

    leaveFree();
    followedVehicleIndex_ %= vehicleCount;
    mode_ = CameraMode::Driver;
}

void Camera::reset()
{
    position_ = {20.0f, 17.0f, 24.0f};
    yaw_ = -130.0f;
    pitch_ = -22.0f;
    mode_ = CameraMode::Free;
    playerCarView_ = CameraMode::PlayerChase;
    followedVehicleIndex_ = 0;
    followSettled_ = false;
    headYaw_ = 0.0f;
    headPitch_ = 0.0f;
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
    return glm::lookAt(position_, position_ + front_, viewUp_);
}

glm::mat4 Camera::projectionMatrix(float aspectRatio) const
{
    const bool inCar = mode_ == CameraMode::Driver || mode_ == CameraMode::PlayerSeat;
    const float fov = inCar ? 68.0f : (mode_ == CameraMode::OnFoot ? 62.0f : fieldOfView_);
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

glm::vec2 Camera::groundForward() const
{
    const glm::vec2 flat {front_.x, front_.z};
    const float length = glm::length(flat);
    return length > 1.0e-4f ? flat / length : glm::vec2 {0.0f, 1.0f};
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
    case CameraMode::PlayerChase: return "YOUR CAR - CHASE";
    case CameraMode::PlayerSeat: return "YOUR CAR - DRIVER VIEW";
    case CameraMode::OnFoot: return "ON FOOT";
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
