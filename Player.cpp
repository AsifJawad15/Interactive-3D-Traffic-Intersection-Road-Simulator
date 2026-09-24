#include "Player.h"

#include "Mannequin.h"
#include "Route.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>

namespace
{
    // Kinematic bicycle model (the car turns about its rear axle).
    constexpr float wheelBase = 2.5f;
    constexpr float wheelRadius = 0.34f;

    constexpr float topSpeed = 14.0f;        // 50 km/h
    constexpr float boostTopSpeed = 22.0f;   // 80 km/h
    constexpr float reverseTopSpeed = 5.0f;
    constexpr float driveAcceleration = 3.2f;
    constexpr float boostAcceleration = 5.0f;
    constexpr float brakeDeceleration = 8.0f;
    constexpr float handbrakeDeceleration = 9.5f;
    constexpr float reverseAcceleration = 2.5f;

    // The steering lock shrinks with speed, as on a real car.
    constexpr float lowSpeedLock = 34.0f;    // degrees
    constexpr float highSpeedLock = 7.0f;
    constexpr float steerRate = 110.0f;      // degrees per second towards the wheel input
    constexpr float centreRate = 160.0f;     // and back to straight when let go

    // Collision: the car never moves more than this in one sub-step, so
    // nothing thinner than a lamp post can be skipped over.
    constexpr float maximumSubStep = 0.2f;
    constexpr float restitution = 0.2f;

    constexpr float walkSpeed = 1.6f;
    constexpr float runSpeed = 4.5f;
    constexpr float walkerAcceleration = 12.0f;

    // Parked on the sidewalk beside the road south of the central crossroads,
    // facing north: out of the traffic's way.
    constexpr glm::vec2 carStart {9.3f, -58.0f};
    constexpr float carStartYaw = 0.0f;

    glm::vec2 forwardOf(float yawDegrees)
    {
        const float yaw = glm::radians(yawDegrees);
        return {std::sin(yaw), std::cos(yaw)};
    }

    // Left of a heading (a larger yaw turns this way).
    glm::vec2 leftOf(glm::vec2 forward)
    {
        return {forward.y, -forward.x};
    }

    float blendAngle(float from, float to, float alpha)
    {
        const float difference = std::fmod(to - from + 540.0f, 360.0f) - 180.0f;
        return from + difference * alpha;
    }

    // Keeps a shape inside the square everyone must stay in.
    glm::vec2 boundaryPush(glm::vec2 centre, float reach)
    {
        const float limit = World::boundary - reach;
        return glm::clamp(centre, glm::vec2 {-limit}, glm::vec2 {limit}) - centre;
    }
}

Player::Player(const World& world) : world_(world)
{
    reset();
}

void Player::reset()
{
    car_ = CarState {};
    car_.position = carStart;
    car_.yawDegrees = carStartYaw;
    car_.height = world_.surfaceHeight(carStart);
    previousCar_ = car_;
    walker_ = WalkerState {};
    walker_.position = carStart + leftOf(forwardOf(carStartYaw)) * 1.6f;
    walker_.height = world_.surfaceHeight(walker_.position);
    previousWalker_ = walker_;
    walking_ = false;
    worldContacts_ = 0;
    trafficContacts_ = 0;
    message_.clear();
    messageAge_ = 100.0f;
}

void Player::placeCar(glm::vec2 position, float yawDegrees)
{
    car_ = CarState {};
    car_.position = position;
    car_.yawDegrees = yawDegrees;
    car_.height = world_.surfaceHeight(position);
    previousCar_ = car_;
}

void Player::placeWalker(glm::vec2 position, float yawDegrees)
{
    walker_ = WalkerState {};
    walker_.position = position;
    walker_.yawDegrees = yawDegrees;
    walker_.height = world_.surfaceHeight(position);
    previousWalker_ = walker_;
    walking_ = true;
}

void Player::say(const std::string& text)
{
    message_ = text;
    messageAge_ = 0.0f;
}

OrientedBox Player::carBody() const
{
    return makeOrientedBox(car_.position, car_.yawDegrees, {carHalfWidth, carHalfLength});
}

Circle Player::walkerBody() const
{
    return {walker_.position, walkerRadius};
}

void Player::guests(std::vector<Guest>& out) const
{
    out.clear();
    out.push_back({carBody(), forwardOf(car_.yawDegrees) * car_.speed});
    if (walking_)
    {
        // The person, as a small square the AI can test its body against.
        out.push_back({makeOrientedBox(walker_.position, 0.0f, {walkerRadius, walkerRadius}), walker_.velocity});
    }
}

bool Player::toggleOnFoot()
{
    if (!walking_)
    {
        if (std::abs(car_.speed) > 2.0f)
        {
            say("SLOW DOWN TO GET OUT");
            return false;
        }
        // Out through the driver's door (on the left), or the other side if
        // something is in the way there.
        const glm::vec2 forward = forwardOf(car_.yawDegrees);
        glm::vec2 door = car_.position + leftOf(forward) * 1.6f;
        for (const OrientedBox& box : world_.solidBoxes())
        {
            if (glm::length(pushOut(Circle {door, walkerRadius}, box)) > 0.0f)
                door = car_.position - leftOf(forward) * 1.6f;
        }
        walker_ = WalkerState {};
        walker_.position = door;
        walker_.height = world_.surfaceHeight(door);
        walker_.yawDegrees = car_.yawDegrees;
        previousWalker_ = walker_;
        car_.speed = 0.0f;
        walking_ = true;
        say("ON FOOT - F NEAR THE CAR TO DRIVE");
        return true;
    }

    const float distance = glm::length(walker_.position - car_.position);
    if (distance > 4.5f)
    {
        say("WALK TO YOUR CAR (" + std::to_string(static_cast<int>(distance)) + " M) TO GET IN");
        return false;
    }
    walking_ = false;
    say("DRIVING");
    return true;
}

void Player::step(float dt, const PlayerInput& input, const std::vector<OrientedBox>& traffic)
{
    previousCar_ = car_;
    previousWalker_ = walker_;
    messageAge_ += dt;

    // The car always moves (a parked car just stands); when you are on foot
    // it gets no input, so it rolls to a stop.
    stepCar(dt, walking_ ? PlayerInput {} : input, traffic);
    if (walking_)
        stepWalker(dt, input, traffic);
}

void Player::stepCar(float dt, const PlayerInput& input, const std::vector<OrientedBox>& traffic)
{
    CarState& car = car_;
    const float speed = car.speed;

    // ---- Speed ----------------------------------------------------------------
    const float maximum = input.boost ? boostTopSpeed : topSpeed;
    float acceleration = 0.0f;
    if (input.handbrake)
    {
        acceleration = -glm::sign(speed) * handbrakeDeceleration;
    }
    else if (input.throttle > 0.0f && speed < -0.3f)
    {
        acceleration = brakeDeceleration * input.throttle;   // braking out of reverse
    }
    else if (input.throttle > 0.0f)
    {
        const float push = input.boost ? boostAcceleration : driveAcceleration;
        const float ratio = std::max(speed, 0.0f) / maximum;
        acceleration = push * input.throttle * (1.0f - ratio * ratio);
        if (speed > maximum)
            acceleration = -1.5f;   // boost let go: ease back down
    }
    else if (input.brake > 0.0f && speed > 0.3f)
    {
        acceleration = -brakeDeceleration * input.brake;
    }
    else if (input.brake > 0.0f)
    {
        acceleration = speed > -reverseTopSpeed ? -reverseAcceleration * input.brake : 0.0f;
    }
    else
    {
        // Rolling resistance and air drag.
        acceleration = -glm::sign(speed) * (0.5f + 0.012f * speed * speed);
    }

    float newSpeed = speed + acceleration * dt;
    // Braking and drag stop the car; they never push it backwards.
    const bool reversing = input.brake > 0.0f && input.throttle <= 0.0f && !input.handbrake && speed <= 0.3f;
    if (!reversing && speed > 0.0f && newSpeed < 0.0f)
        newSpeed = 0.0f;
    if (speed < 0.0f && newSpeed > 0.0f && input.throttle <= 0.0f)
        newSpeed = 0.0f;
    if (!reversing && input.throttle <= 0.0f && std::abs(newSpeed) < 0.05f)
        newSpeed = 0.0f;
    car.speed = glm::clamp(newSpeed, -reverseTopSpeed, boostTopSpeed);
    car.longitudinalAcceleration = (car.speed - speed) / dt;

    // ---- Steering -------------------------------------------------------------
    const float lock = glm::mix(lowSpeedLock, highSpeedLock, glm::clamp(std::abs(car.speed) / boostTopSpeed, 0.0f, 1.0f));
    const float target = input.steer * lock;
    const float rate = std::abs(target) > std::abs(car.steerDegrees) ? steerRate : centreRate;
    car.steerDegrees += glm::clamp(target - car.steerDegrees, -rate * dt, rate * dt);

    float yawRate = car.speed / wheelBase * std::tan(glm::radians(car.steerDegrees));   // rad/s
    if (input.handbrake && std::abs(car.speed) > 3.0f)
        yawRate *= 1.35f;   // the rear steps out a little
    car.lateralAcceleration = car.speed * yawRate;

    // ---- Move, in sub-steps, pushing out of anything solid ----------------
    const float travel = std::abs(car.speed) * dt;
    const int subSteps = std::max(1, static_cast<int>(std::ceil(travel / maximumSubStep)));
    const float subDt = dt / static_cast<float>(subSteps);
    bool hitWorld = false;
    bool hitTraffic = false;
    for (int sub = 0; sub < subSteps; ++sub)
    {
        car.yawDegrees += glm::degrees(yawRate) * subDt;
        car.position += forwardOf(car.yawDegrees) * car.speed * subDt;

        for (int iteration = 0; iteration < 4; ++iteration)
        {
            const OrientedBox body = carBody();
            glm::vec2 deepest {0.0f};
            bool fromTraffic = false;
            const auto consider = [&deepest, &fromTraffic](glm::vec2 push, bool isTraffic)
            {
                if (glm::dot(push, push) > glm::dot(deepest, deepest))
                {
                    deepest = push;
                    fromTraffic = isTraffic;
                }
            };

            for (const OrientedBox& box : world_.solidBoxes())
            {
                if (glm::length(box.centre - body.centre) < 30.0f)
                    consider(pushOut(body, box), false);
            }
            for (const Circle& post : world_.solidPosts())
            {
                if (glm::length(post.centre - body.centre) < 4.0f)
                    consider(pushOut(body, post), false);
            }
            for (const Circle& island : world_.islands())
                consider(pushOut(body, island), false);
            for (const OrientedBox& other : traffic)
            {
                if (glm::length(other.centre - body.centre) < 6.0f)
                    consider(pushOut(body, other), true);
            }
            consider(boundaryPush(body.centre, carHalfLength), false);

            const float depth = glm::length(deepest);
            if (depth < 1.0e-5f)
                break;

            // Out of the obstacle, and the part of the velocity that drove
            // into it is taken away (with a little bounce). The car can only
            // move along its heading, so what is left becomes its new speed:
            // hitting a wall at a shallow angle slides along it.
            car.position += deepest;
            const glm::vec2 normal = deepest / depth;
            const glm::vec2 forward = forwardOf(car.yawDegrees);
            glm::vec2 velocity = forward * car.speed;
            const float into = glm::dot(velocity, normal);
            if (into < 0.0f)
            {
                // A glancing blow on something solid swings the nose round
                // along it, so the car scrapes along a wall instead of
                // sticking to it. A head-on hit (steeper than 65 degrees to
                // the wall) just stops it, and bumping a car never turns you.
                const glm::vec2 along = forward - normal * glm::dot(forward, normal);
                if (!fromTraffic && glm::length(along) > 0.42f)
                {
                    const glm::vec2 tangent = glm::normalize(along);
                    float turn = glm::degrees(std::atan2(tangent.x, tangent.y) - std::atan2(forward.x, forward.y));
                    turn = std::fmod(turn + 540.0f, 360.0f) - 180.0f;
                    car.yawDegrees += turn * 0.35f;
                }
                const glm::vec2 turnedForward = forwardOf(car.yawDegrees);
                velocity -= normal * into * (1.0f + restitution);
                car.speed = glm::dot(velocity, turnedForward);
            }
            (fromTraffic ? hitTraffic : hitWorld) = true;
        }
    }
    if (hitWorld && !(previousCar_.speed == 0.0f && car.speed == 0.0f))
        ++worldContacts_;
    if (hitTraffic)
        ++trafficContacts_;

    // Up and down the kerbs smoothly.
    const float ground = world_.surfaceHeight(car.position);
    car.height += (ground - car.height) * std::min(1.0f, dt * 14.0f);
    car.wheelDegrees = std::fmod(car.wheelDegrees + glm::degrees(car.speed * dt / wheelRadius), 360.0f);
}

void Player::stepWalker(float dt, const PlayerInput& input, const std::vector<OrientedBox>& traffic)
{
    WalkerState& walker = walker_;

    // Where you want to go, relative to where you look.
    const glm::vec2 forward = glm::length(input.lookForward) > 1.0e-4f ? glm::normalize(input.lookForward)
                                                                       : forwardOf(walker.yawDegrees);
    const glm::vec2 right = -leftOf(forward);
    glm::vec2 wish = forward * input.walk.y + right * input.walk.x;
    if (glm::length(wish) > 1.0f)
        wish = glm::normalize(wish);
    const glm::vec2 targetVelocity = wish * (input.run ? runSpeed : walkSpeed);
    const glm::vec2 change = targetVelocity - walker.velocity;
    const float maximumChange = walkerAcceleration * dt;
    walker.velocity += glm::length(change) > maximumChange ? glm::normalize(change) * maximumChange : change;
    walker.yawDegrees = glm::degrees(std::atan2(forward.x, forward.y));

    walker.position += walker.velocity * dt;

    // Solid things push you out; you slide along them because only the part
    // of your velocity going into them is removed.
    const OrientedBox parkedCar = carBody();
    for (int iteration = 0; iteration < 4; ++iteration)
    {
        const Circle body = walkerBody();
        glm::vec2 deepest {0.0f};
        const auto consider = [&deepest](glm::vec2 push)
        {
            if (glm::dot(push, push) > glm::dot(deepest, deepest))
                deepest = push;
        };
        for (const OrientedBox& box : world_.solidBoxes())
        {
            if (glm::length(box.centre - body.centre) < 30.0f)
                consider(pushOut(body, box));
        }
        for (const Circle& post : world_.solidPosts())
        {
            if (glm::length(post.centre - body.centre) < 2.0f)
                consider(pushOut(body, post));
        }
        for (const Circle& fountain : world_.fountains())
            consider(pushOut(body, fountain));
        for (const OrientedBox& other : traffic)
        {
            if (glm::length(other.centre - body.centre) < 5.0f)
                consider(pushOut(body, other));
        }
        consider(pushOut(body, parkedCar));
        consider(boundaryPush(body.centre, walkerRadius));

        const float depth = glm::length(deepest);
        if (depth < 1.0e-5f)
            break;
        walker.position += deepest;
        const glm::vec2 normal = deepest / depth;
        const float into = glm::dot(walker.velocity, normal);
        if (into < 0.0f)
            walker.velocity -= normal * into;
    }

    // Step up onto the kerb and down again smoothly.
    const float ground = world_.surfaceHeight(walker.position);
    walker.height += (ground - walker.height) * std::min(1.0f, dt * 16.0f);

    // The legs keep time with the distance actually covered.
    const float moved = glm::length(walker.position - previousWalker_.position);
    walker.speed = moved / dt;
    walker.phase = std::fmod(walker.phase + moved / Mannequin::stride(walker.speed, playerHeight), 1.0f);

    WalkerLook you;
    you.height = playerHeight;
    WalkerMotion motion;
    motion.position = {walker.position.x, 0.0f, walker.position.y};
    motion.yawDegrees = walker.yawDegrees;
    motion.speed = walker.speed;
    motion.phase = walker.phase;
    motion.lock[0] = walker.lock[0];
    motion.lock[1] = walker.lock[1];
    Mannequin::plantFeet(you, motion);
    walker.lock[0] = motion.lock[0];
    walker.lock[1] = motion.lock[1];
}

PlayerView Player::view(float alpha) const
{
    alpha = glm::clamp(alpha, 0.0f, 1.0f);
    PlayerView result;
    result.walking = walking_;

    const glm::vec2 carPosition = glm::mix(previousCar_.position, car_.position, alpha);
    const float carHeight = glm::mix(previousCar_.height, car_.height, alpha);
    // The car's origin rides at the same height above the ground as the AI
    // cars' does above the asphalt.
    result.carPosition = {carPosition.x, carHeight - RoadNetwork::roadY + Route::rideHeight, carPosition.y};
    result.carYawDegrees = blendAngle(previousCar_.yawDegrees, car_.yawDegrees, alpha);
    result.carSteerDegrees = glm::mix(previousCar_.steerDegrees, car_.steerDegrees, alpha);
    result.carWheelDegrees = blendAngle(previousCar_.wheelDegrees, car_.wheelDegrees, alpha);
    result.carSpeed = glm::mix(previousCar_.speed, car_.speed, alpha);
    result.lateralAcceleration = glm::mix(previousCar_.lateralAcceleration, car_.lateralAcceleration, alpha);
    result.longitudinalAcceleration = glm::mix(previousCar_.longitudinalAcceleration, car_.longitudinalAcceleration, alpha);

    const glm::vec2 walkerPosition = glm::mix(previousWalker_.position, walker_.position, alpha);
    result.walkerPosition = {walkerPosition.x, glm::mix(previousWalker_.height, walker_.height, alpha), walkerPosition.y};
    result.walkerYawDegrees = blendAngle(previousWalker_.yawDegrees, walker_.yawDegrees, alpha);
    result.walkerSpeed = glm::mix(previousWalker_.speed, walker_.speed, alpha);
    float phaseStep = walker_.phase - previousWalker_.phase;
    if (phaseStep < -0.5f)
        phaseStep += 1.0f;
    result.walkerPhase = std::fmod(previousWalker_.phase + phaseStep * alpha + 1.0f, 1.0f);
    result.walkerFeet[0] = walker_.lock[0];
    result.walkerFeet[1] = walker_.lock[1];
    return result;
}
