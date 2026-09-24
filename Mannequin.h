#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <cstddef>
#include <functional>

// A person made of simple solid parts hung on a small skeleton, and how that
// skeleton moves (plan section 5.1). Pure maths with no OpenGL: the renderer,
// the cameras and --walk-test all use it.
//
// The motion is authored here rather than loaded from motion-capture files.
// Each clip (idle, walk, jog, waiting at the kerb) is a set of keyframed
// curves over the body's controls: where each foot is and how it is tilted,
// how high the pelvis rides and how it sways and turns, the lean and twist of
// the spine, the swing of the arms and the turn of the head. The walk and jog
// clips are sampled at the same gait phase and blended by speed, so the feet
// stay in step whatever the mix, and the phase advances by the distance
// walked, so a planted foot stays exactly where it was put down. States change
// through a small animation graph with short cross-fades. The legs are placed
// by two-bone inverse kinematics on the ground under each foot, so people
// step up and down kerbs, and the pelvis drops just enough for both feet to
// reach the ground.
//
// Local frame, as for the vehicles: +z forward, +y up, +x to the LEFT.
// A heading h faces (sin h, cos h) in world (x, z).

// The shapes a person is built from. Arms and legs appear twice.
enum class BodyShape
{
    Pelvis,
    Torso,
    Neck,
    Head,
    Hair,
    UpperArm,
    Forearm,
    Hand,
    Thigh,
    Shin,
    Foot,
    UmbrellaCanopy,
    UmbrellaShaft
};
inline constexpr std::size_t bodyShapeCount = 13;

// Every drawn piece of one person, in a fixed order.
enum BodySlot : int
{
    SlotPelvis,
    SlotTorso,
    SlotNeck,
    SlotHead,
    SlotHair,
    SlotUpperArmLeft,
    SlotUpperArmRight,
    SlotForearmLeft,
    SlotForearmRight,
    SlotHandLeft,
    SlotHandRight,
    SlotThighLeft,
    SlotThighRight,
    SlotShinLeft,
    SlotShinRight,
    SlotFootLeft,
    SlotFootRight,
    SlotUmbrellaCanopy,
    SlotUmbrellaShaft
};
inline constexpr std::size_t bodySlotCount = 19;
BodyShape slotShape(int slot);

// The states of the animation graph.
enum class Motion
{
    Idle,    // standing about
    Walk,    // along the sidewalk (walking or jogging, by speed)
    Wait,    // at the kerb: looking both ways
    Cross    // over the road
};

// How someone looks.
struct WalkerLook
{
    float height = 1.75f;   // metres
    float build = 1.0f;     // girth, about 0.9 to 1.15
    glm::vec3 skin {0.80f, 0.62f, 0.50f};
    glm::vec3 hair {0.12f, 0.08f, 0.05f};
    glm::vec3 top {0.20f, 0.35f, 0.60f};
    glm::vec3 trousers {0.12f, 0.13f, 0.18f};
    glm::vec3 shoes {0.08f, 0.07f, 0.07f};
    glm::vec3 umbrella {0.12f, 0.14f, 0.20f};
    bool longSleeves = true;
    bool shorts = false;
    bool bald = false;

    // The colour of one slot.
    glm::vec3 color(int slot) const;
};

// Where a foot was put down, held there until it lifts off again (foot
// locking): kept by the simulation, which calls Mannequin::plantFeet every
// step, so a planted foot stays put however the pace changes.
struct FootLock
{
    glm::vec2 point {0.0f};   // world (x, z) of the foot's base under the ankle
    bool valid = false;
    bool planted = false;     // on the ground now (else: where it lifted off)
};

// Where someone is and what they are doing, blended between two steps.
struct WalkerMotion
{
    glm::vec3 position {0.0f};   // on the ground, under the pelvis
    float yawDegrees = 0.0f;
    float speed = 0.0f;          // m/s over the ground
    float phase = 0.0f;          // gait cycle, 0..1; 0 = left heel down
    Motion motion = Motion::Walk;
    Motion previousMotion = Motion::Walk;
    float fade = 1.0f;           // cross-fade from the previous state, 0..1
    float stateSeconds = 0.0f;   // time in the current state (the head's looks)
    float clock = 0.0f;          // seconds, for breathing and idle sway
    float umbrella = 0.0f;       // 0 closed and away .. 1 open over the head
    FootLock lock[2];            // 0 left, 1 right
};

// Height of the ground (the top of the sole) at a point in world (x, z).
using GroundHeight = std::function<float(glm::vec2)>;

struct MannequinPose
{
    std::array<glm::mat4, bodySlotCount> parts {};
    glm::vec3 ankle[2] {};        // 0 left, 1 right, world
    glm::vec3 ankleTarget[2] {};  // where the gait wanted them
    glm::vec3 heel[2] {};         // the bottom of each shoe at the heel
    glm::vec3 ball[2] {};         // and under the ball of the foot
    bool planted[2] {};           // in the stance part of the cycle
    glm::vec3 eye {0.0f};
    float lookYawDegrees = 0.0f;  // the head's heading, world
};

namespace Mannequin
{
    // Length of one whole gait cycle (two steps) at a speed, for someone of
    // this height: the phase advances by distance / stride.
    float stride(float speed, float height);
    // Share of the cycle a foot is on the ground: about 0.62 walking, 0.4 jogging.
    float stanceShare(float speed);
    // The head's turn relative to the body, in degrees (left positive).
    float headYaw(const WalkerMotion& motion);

    void pose(const WalkerLook& look, const WalkerMotion& motion, const GroundHeight& ground, MannequinPose& out);

    // Once a simulation step: a foot coming down is locked where it lands,
    // and one lifting off is released (its lift-off point is kept, for the
    // swing to start from).
    void plantFeet(const WalkerLook& look, WalkerMotion& motion);

    // Standard sizes (for a person 1.75 m tall), used to build the meshes.
    inline constexpr float thighLength = 0.455f;
    inline constexpr float shinLength = 0.445f;
    inline constexpr float ankleHeight = 0.075f;
    inline constexpr float upperArmLength = 0.29f;
    inline constexpr float forearmLength = 0.26f;
    inline constexpr float handLength = 0.085f;
    inline constexpr float torsoLength = 0.44f;
    inline constexpr float neckLength = 0.09f;
}
