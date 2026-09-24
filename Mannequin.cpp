#include "Mannequin.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>

namespace
{
    constexpr float pi = 3.14159265358979f;
    constexpr float twoPi = 2.0f * pi;

    // Standard proportions for someone 1.75 m tall; everything scales with
    // height (and girth with build).
    constexpr float standardHeight = 1.75f;
    constexpr float hipSpacing = 0.09f;       // hip joint either side of the middle
    constexpr float hipBelowPelvis = 0.035f;
    constexpr float pelvisToWaist = 0.055f;
    constexpr float shoulderSpacing = 0.175f;
    constexpr float shoulderHeight = 0.385f;  // above the waist, along the spine
    constexpr float heelBehind = 0.065f;      // heel behind the ankle
    constexpr float ballAhead = 0.15f;        // ball of the foot ahead of it

    float smoothstep(float edge0, float edge1, float x)
    {
        const float t = glm::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    float smootherstep(float t)
    {
        t = glm::clamp(t, 0.0f, 1.0f);
        return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
    }

    // A keyframe of a looping curve: its time as a share of the loop, and
    // its value.
    struct Key
    {
        float t;
        float value;
    };

    // Catmull-Rom spline through looping keys (times in [0, 1), in order):
    // smooth through every key, with no corner where the loop closes.
    template <std::size_t N>
    float sampleLoop(const std::array<Key, N>& keys, float t)
    {
        t -= std::floor(t);
        const int count = static_cast<int>(N);
        int index = count - 1;
        for (int k = 0; k < count; ++k)
        {
            if (keys[static_cast<std::size_t>(k)].t <= t)
                index = k;
        }
        const auto at = [&keys, count](int k) { return keys[static_cast<std::size_t>(((k % count) + count) % count)]; };
        const Key k0 = at(index - 1);
        const Key k1 = at(index);
        const Key k2 = at(index + 1);
        const Key k3 = at(index + 2);
        float start = k1.t;
        float end = k2.t;
        if (end <= start)
            end += 1.0f;
        float time = t;
        if (time < start)
            time += 1.0f;
        const float u = glm::clamp((time - start) / std::max(end - start, 1.0e-4f), 0.0f, 1.0f);
        const float u2 = u * u;
        const float u3 = u2 * u;
        return 0.5f * (2.0f * k1.value + (-k0.value + k2.value) * u +
                       (2.0f * k0.value - 5.0f * k1.value + 4.0f * k2.value - k3.value) * u2 +
                       (-k0.value + 3.0f * k1.value - 3.0f * k2.value + k3.value) * u3);
    }

    // The body's controls: what every clip is made of, and what is blended.
    // Lengths are for the standard height; angles are in degrees.
    struct Controls
    {
        glm::vec3 foot[2] {};     // ankle target: x left, y lift of the sole, z forward
        float footPitch[2] {};    // toes up positive
        float stance[2] {};       // 1 while the foot is on the ground
        float swing[2] {};        // how far through its swing, 0..1 (1 on the ground)
        float landing[2] {};      // z where the foot will next be on the ground
        float pelvisHeight = 0.965f;
        float pelvisSway = 0.0f;
        float pelvisYaw = 0.0f;
        float lean = 0.0f;        // spine forward
        float twist = 0.0f;       // shoulders against the hips
        float headYaw = 0.0f;
        float headPitch = 0.0f;
        float armSwing[2] {};     // forward positive
        float elbow[2] {};
        float armOut[2] {};
    };

    Controls blend(const Controls& a, const Controls& b, float t)
    {
        Controls result;
        for (int side = 0; side < 2; ++side)
        {
            result.foot[side] = glm::mix(a.foot[side], b.foot[side], t);
            result.footPitch[side] = glm::mix(a.footPitch[side], b.footPitch[side], t);
            result.stance[side] = glm::mix(a.stance[side], b.stance[side], t);
            result.swing[side] = glm::mix(a.swing[side], b.swing[side], t);
            result.landing[side] = glm::mix(a.landing[side], b.landing[side], t);
            result.armSwing[side] = glm::mix(a.armSwing[side], b.armSwing[side], t);
            result.elbow[side] = glm::mix(a.elbow[side], b.elbow[side], t);
            result.armOut[side] = glm::mix(a.armOut[side], b.armOut[side], t);
        }
        result.pelvisHeight = glm::mix(a.pelvisHeight, b.pelvisHeight, t);
        result.pelvisSway = glm::mix(a.pelvisSway, b.pelvisSway, t);
        result.pelvisYaw = glm::mix(a.pelvisYaw, b.pelvisYaw, t);
        result.lean = glm::mix(a.lean, b.lean, t);
        result.twist = glm::mix(a.twist, b.twist, t);
        result.headYaw = glm::mix(a.headYaw, b.headYaw, t);
        result.headPitch = glm::mix(a.headPitch, b.headPitch, t);
        return result;
    }

    // ---- The clips ------------------------------------------------------------

    // Standing about: feet a little apart, breathing, the weight drifting from
    // one foot to the other, and the odd look around.
    Controls idleClip(float clock)
    {
        Controls c;
        c.foot[0] = {0.11f, 0.0f, 0.035f};
        c.foot[1] = {-0.11f, 0.0f, -0.02f};
        c.stance[0] = c.stance[1] = 1.0f;
        c.swing[0] = c.swing[1] = 1.0f;
        c.landing[0] = c.foot[0].z;
        c.landing[1] = c.foot[1].z;
        c.pelvisHeight = 0.962f + 0.004f * std::sin(twoPi * clock / 4.2f);
        c.pelvisSway = 0.018f * std::sin(twoPi * clock / 7.3f);
        c.pelvisYaw = 2.5f * std::sin(twoPi * clock / 9.1f);
        c.lean = 1.0f + 0.6f * std::sin(twoPi * clock / 4.2f);
        c.headYaw = 14.0f * std::sin(twoPi * clock / 8.7f) + 6.0f * std::sin(twoPi * clock / 3.9f);
        c.headPitch = 2.0f;
        for (int side = 0; side < 2; ++side)
        {
            c.armSwing[side] = 2.0f + 1.5f * std::sin(twoPi * clock / 4.2f + static_cast<float>(side));
            c.elbow[side] = 10.0f;
            c.armOut[side] = 6.0f;
        }
        return c;
    }

    // Waiting at the kerb: standing, and looking both ways (left, right,
    // left again), round and round while the wait lasts.
    Controls waitClip(float stateSeconds, float clock)
    {
        Controls c = idleClip(clock);
        static constexpr std::array<Key, 8> look = {{
            {0.00f, 0.0f}, {0.11f, 50.0f}, {0.27f, 52.0f}, {0.42f, -50.0f},
            {0.58f, -52.0f}, {0.70f, 18.0f}, {0.82f, 22.0f}, {0.93f, 4.0f}
        }};
        constexpr float cycleSeconds = 7.0f;
        c.headYaw = sampleLoop(look, stateSeconds / cycleSeconds);
        c.twist = 0.22f * c.headYaw;
        c.lean = 2.0f;
        return c;
    }

    // Walking and jogging, sampled at one gait phase and blended by speed.
    // The foot on the ground moves back under the body at exactly the speed
    // the body moves forward, so it stays put on the ground.
    Controls locomotionClip(float phase, float speed, float height)
    {
        const float k = height / standardHeight;
        const float jog = smoothstep(1.9f, 2.7f, speed);
        const float stride = Mannequin::stride(speed, height) / k;   // in standard units
        const float stanceShare = Mannequin::stanceShare(speed);
        const float stanceLength = stanceShare * stride;

        // Foot tilt through the stance (heel strike, flat, toe-off) and the
        // swing (toes down after push-off, up again for the next heel strike).
        static constexpr std::array<Key, 5> walkSwingPitch = {{
            {0.0f, -30.0f}, {0.3f, -12.0f}, {0.65f, 2.0f}, {0.9f, 12.0f}, {0.99f, 14.0f}
        }};
        static constexpr std::array<Key, 5> jogSwingPitch = {{
            {0.0f, -38.0f}, {0.3f, -26.0f}, {0.7f, -4.0f}, {0.9f, 3.0f}, {0.99f, 4.0f}
        }};

        Controls c;
        for (int side = 0; side < 2; ++side)
        {
            const float footPhase = std::fmod(phase + 0.5f * static_cast<float>(side), 1.0f);
            const float x = (side == 0 ? 1.0f : -1.0f) * glm::mix(0.10f, 0.075f, jog);
            float z = 0.0f;
            float lift = 0.0f;
            float pitch = 0.0f;
            if (footPhase < stanceShare)
            {
                const float t = footPhase / stanceShare;
                z = stanceLength * (0.5f - t);
                c.stance[side] = 1.0f;
                c.swing[side] = 1.0f;
                c.landing[side] = z;
                const float walkPitch = t < 0.12f ? glm::mix(14.0f, 0.0f, t / 0.12f)
                                      : t < 0.78f ? 0.0f : glm::mix(0.0f, -30.0f, (t - 0.78f) / 0.22f);
                const float jogPitch = t < 0.15f ? glm::mix(4.0f, 0.0f, t / 0.15f)
                                     : t < 0.6f ? 0.0f : glm::mix(0.0f, -38.0f, (t - 0.6f) / 0.4f);
                pitch = glm::mix(walkPitch, jogPitch, jog);
            }
            else
            {
                const float u = (footPhase - stanceShare) / (1.0f - stanceShare);
                // A Hermite curve from where the foot lifted off to where it
                // will land, leaving and arriving at the same speed backwards
                // under the body as in the stance: at rest on the ground at
                // both ends, so it neither scuffs off nor skids in.
                const float swingLength = (1.0f - stanceShare) * stride;
                const float u2 = u * u;
                const float u3 = u2 * u;
                z = (2.0f * u3 - 3.0f * u2 + 1.0f) * (-0.5f * stanceLength) + (-2.0f * u3 + 3.0f * u2) * (0.5f * stanceLength) -
                    swingLength * ((u3 - 2.0f * u2 + u) + (u3 - u2));
                c.swing[side] = u;
                c.landing[side] = 0.5f * stanceLength + (1.0f - u) * swingLength;
                lift = glm::mix(0.10f, 0.21f, jog) * std::sin(pi * u) * (1.15f - 0.3f * u);
                pitch = glm::mix(sampleLoop(walkSwingPitch, u * 0.99f), sampleLoop(jogSwingPitch, u * 0.99f), jog);
            }
            c.foot[side] = {x, lift, z};
            c.footPitch[side] = pitch;
        }

        // The pelvis is lowest as each heel comes down walking, and at
        // mid-stance jogging (knees bent), highest in between.
        const float walkBob = -0.018f * std::cos(2.0f * twoPi * phase);
        const float jogBob = -0.035f * std::cos(2.0f * twoPi * (phase - 0.2f));
        c.pelvisHeight = glm::mix(0.958f, 0.93f, jog) + glm::mix(walkBob, jogBob, jog);
        c.pelvisSway = glm::mix(0.022f, 0.010f, jog) * std::sin(twoPi * phase);
        c.pelvisYaw = -glm::mix(5.0f, 8.0f, jog) * std::cos(twoPi * phase);
        c.twist = -1.5f * c.pelvisYaw;
        c.lean = glm::mix(3.0f, 10.0f, jog);
        c.headYaw = -0.25f * c.pelvisYaw;
        c.headPitch = glm::mix(2.0f, 4.0f, jog);   // eyes a few metres ahead
        const float swing = glm::mix(17.0f, 38.0f, jog) * std::cos(twoPi * phase);
        c.armSwing[0] = 3.0f - swing;   // the left arm goes back as the left leg goes forward
        c.armSwing[1] = 3.0f + swing;
        for (int side = 0; side < 2; ++side)
        {
            c.elbow[side] = glm::mix(14.0f, 88.0f, jog) + 0.35f * std::max(0.0f, c.armSwing[side]);
            c.armOut[side] = glm::mix(5.0f, 9.0f, jog);
        }
        return c;
    }

    // One state of the animation graph.
    Controls stateClip(Motion motion, const WalkerMotion& state, float height)
    {
        switch (motion)
        {
        case Motion::Idle:
            return idleClip(state.clock);
        case Motion::Wait:
            return waitClip(state.stateSeconds, state.clock);
        case Motion::Walk:
        case Motion::Cross:
        default:
        {
            // Slowing to a stop the stride fades into the standing pose.
            const float moving = smoothstep(0.08f, 0.55f, state.speed);
            Controls c = blend(idleClip(state.clock), locomotionClip(state.phase, state.speed, height), moving);
            if (motion == Motion::Cross)
            {
                // A glance at the traffic now and then, fading as they go.
                c.headYaw += 28.0f * std::sin(twoPi * state.stateSeconds / 3.0f) * std::exp(-state.stateSeconds / 4.0f);
            }
            return c;
        }
        }
    }

    Controls controlsFor(const WalkerMotion& motion, float height)
    {
        const Controls current = stateClip(motion.motion, motion, height);
        if (motion.fade >= 0.999f || motion.previousMotion == motion.motion)
            return current;
        WalkerMotion before = motion;
        before.stateSeconds = motion.stateSeconds + 10.0f;   // well into the old state
        return blend(stateClip(motion.previousMotion, before, height), current, smootherstep(motion.fade));
    }

    // ---- Building the pose ---------------------------------------------------

    glm::mat3 rotationY(float degrees)
    {
        return glm::mat3(glm::rotate(glm::mat4(1.0f), glm::radians(degrees), {0.0f, 1.0f, 0.0f}));
    }

    glm::mat3 rotationX(float degrees)
    {
        return glm::mat3(glm::rotate(glm::mat4(1.0f), glm::radians(degrees), {1.0f, 0.0f, 0.0f}));
    }

    glm::mat3 rotationZ(float degrees)
    {
        return glm::mat3(glm::rotate(glm::mat4(1.0f), glm::radians(degrees), {0.0f, 0.0f, 1.0f}));
    }

    // A part standing at `origin`, turned by `rotation`, scaled by `scale`.
    glm::mat4 placed(const glm::vec3& origin, const glm::mat3& rotation, const glm::vec3& scale)
    {
        glm::mat4 model(1.0f);
        model[0] = glm::vec4(rotation[0] * scale.x, 0.0f);
        model[1] = glm::vec4(rotation[1] * scale.y, 0.0f);
        model[2] = glm::vec4(rotation[2] * scale.z, 0.0f);
        model[3] = glm::vec4(origin, 1.0f);
        return model;
    }

    // A limb from one joint to the next: the mesh runs up its local y axis
    // from 0 to 1, so it is stretched to the bone's length; its local z is
    // turned as close to `facing` as the bone allows.
    glm::mat4 limb(const glm::vec3& from, const glm::vec3& to, const glm::vec3& facing, float girth)
    {
        glm::vec3 axis = to - from;
        const float length = std::max(glm::length(axis), 1.0e-4f);
        axis /= length;
        glm::vec3 front = facing - axis * glm::dot(facing, axis);
        if (glm::length(front) < 1.0e-3f)
            front = std::abs(axis.y) < 0.9f ? glm::vec3 {0.0f, 1.0f, 0.0f} - axis * axis.y : glm::vec3 {0.0f, 0.0f, 1.0f};
        front = glm::normalize(front);
        const glm::vec3 side = glm::cross(axis, front);
        glm::mat4 model(1.0f);
        model[0] = glm::vec4(side * girth, 0.0f);
        model[1] = glm::vec4(axis * length, 0.0f);
        model[2] = glm::vec4(front * girth, 0.0f);
        model[3] = glm::vec4(from, 1.0f);
        return model;
    }
}

BodyShape slotShape(int slot)
{
    switch (slot)
    {
    case SlotPelvis: return BodyShape::Pelvis;
    case SlotTorso: return BodyShape::Torso;
    case SlotNeck: return BodyShape::Neck;
    case SlotHead: return BodyShape::Head;
    case SlotHair: return BodyShape::Hair;
    case SlotUpperArmLeft: case SlotUpperArmRight: return BodyShape::UpperArm;
    case SlotForearmLeft: case SlotForearmRight: return BodyShape::Forearm;
    case SlotHandLeft: case SlotHandRight: return BodyShape::Hand;
    case SlotThighLeft: case SlotThighRight: return BodyShape::Thigh;
    case SlotShinLeft: case SlotShinRight: return BodyShape::Shin;
    case SlotFootLeft: case SlotFootRight: return BodyShape::Foot;
    case SlotUmbrellaCanopy: return BodyShape::UmbrellaCanopy;
    default: return BodyShape::UmbrellaShaft;
    }
}

glm::vec3 WalkerLook::color(int slot) const
{
    switch (slot)
    {
    case SlotPelvis: return trousers;
    case SlotTorso: case SlotUpperArmLeft: case SlotUpperArmRight: return top;
    case SlotForearmLeft: case SlotForearmRight: return longSleeves ? top : skin;
    case SlotHair: return bald ? skin : hair;
    case SlotThighLeft: case SlotThighRight: return trousers;
    case SlotShinLeft: case SlotShinRight: return shorts ? skin : trousers;
    case SlotFootLeft: case SlotFootRight: return shoes;
    case SlotUmbrellaCanopy: return umbrella;
    case SlotUmbrellaShaft: return {0.12f, 0.12f, 0.13f};
    default: return skin;   // neck, head, hands
    }
}

float Mannequin::stride(float speed, float height)
{
    // About 1.47 m at a walk of 1.4 m/s (1.9 steps a second) and 2.25 m
    // jogging at 3 m/s (2.7 steps a second), a little longer for the tall.
    return 0.8f * (height / standardHeight) + 0.48f * std::max(speed, 0.0f);
}

float Mannequin::stanceShare(float speed)
{
    return glm::mix(0.62f, 0.40f, smoothstep(1.9f, 2.7f, speed));
}

void Mannequin::plantFeet(const WalkerLook& look, WalkerMotion& motion)
{
    const float k = look.height / standardHeight;
    const Controls c = controlsFor(motion, look.height);
    const glm::mat3 body = rotationY(motion.yawDegrees);
    for (int side = 0; side < 2; ++side)
    {
        const glm::vec3 turned = body * glm::vec3 {c.foot[side].x * k, 0.0f, c.foot[side].z * k};
        const glm::vec2 gaitBase {motion.position.x + turned.x, motion.position.z + turned.z};
        FootLock& lock = motion.lock[side];
        if (c.stance[side] <= 0.5f)
        {
            lock.planted = false;
            continue;
        }
        if (!lock.valid || !lock.planted)
        {
            lock.point = gaitBase;
            lock.valid = true;
            lock.planted = true;
            continue;
        }
        // A foot left too far from where the body now wants it (turning on
        // the spot, a sudden change of pace) is shuffled along with it.
        constexpr float leeway = 0.25f;
        const glm::vec2 offset = gaitBase - lock.point;
        const float distance = glm::length(offset);
        if (distance > leeway)
            lock.point += offset * ((distance - leeway) / distance);
    }
}

float Mannequin::headYaw(const WalkerMotion& motion)
{
    return controlsFor(motion, standardHeight).headYaw;
}

void Mannequin::pose(const WalkerLook& look, const WalkerMotion& motion, const GroundHeight& ground, MannequinPose& out)
{
    const float k = look.height / standardHeight;
    const float girth = look.build * k;
    const Controls c = controlsFor(motion, look.height);

    const glm::mat3 body = rotationY(motion.yawDegrees);
    const glm::vec2 root {motion.position.x, motion.position.z};
    const auto groundPoint = [&body, &root](const glm::vec3& local)
    {
        const glm::vec3 turned = body * glm::vec3 {local.x, 0.0f, local.z};
        return root + glm::vec2 {turned.x, turned.z};
    };
    const glm::vec3 ahead = body * glm::vec3 {0.0f, 0.0f, 1.0f};
    const glm::vec2 aheadXZ {ahead.x, ahead.z};
    // A shoe rests on the highest ground under its heel, arch and toes (at
    // a kerb edge the toes may stick out over the road).
    const auto footGround = [&ground, &aheadXZ, k](glm::vec2 base)
    {
        return std::max({ground(base - aheadXZ * (heelBehind * k)), ground(base), ground(base + aheadXZ * (0.17f * k))});
    };

    // ---- Feet: where the gait wants them, on the ground under each ------------
    glm::vec3 ankleTarget[2];
    float groundUnder[2];
    for (int side = 0; side < 2; ++side)
    {
        // Where the foot is: held where it was put down while it stays on
        // the ground; after lift-off, easing from there onto the gait's path.
        const glm::vec3 local = c.foot[side] * k;
        const glm::vec2 gaitBase = groundPoint(local);
        const FootLock& lock = motion.lock[side];
        glm::vec2 base = gaitBase;
        if (lock.valid && lock.planted && c.stance[side] > 0.5f)
            base = lock.point;
        else if (lock.valid && !lock.planted)
            base = glm::mix(lock.point, gaitBase, smoothstep(0.0f, 0.4f, c.swing[side]));

        // The foot rolls over its heel (toes up) or its ball (heel up), so
        // the ankle moves round whichever of them is on the ground and that
        // point stays exactly where it is.
        float roll = 0.0f;
        const float pitch = glm::radians(c.footPitch[side]);
        const float ankle = Mannequin::ankleHeight * k;
        float rise = 0.0f;
        if (pitch >= 0.0f)
        {
            const float heel = heelBehind * k;
            roll = heel * std::cos(pitch) - heel - ankle * std::sin(pitch);
            rise = ankle * std::cos(pitch) + heel * std::sin(pitch);
        }
        else
        {
            const float ball = ballAhead * k;
            roll = ball - ball * std::cos(-pitch) + ankle * std::sin(-pitch);
            rise = ankle * std::cos(-pitch) + ball * std::sin(-pitch);
        }
        const glm::vec2 at = base + aheadXZ * roll;

        // On the ground: its own ground. In the air: from the height it left
        // to the height it will land on - rising early in the swing, so a
        // foot stepping up clears the kerb, dropping late, so one stepping
        // down stays over the kerb until it is past it - and never lower
        // than the ground under it.
        if (c.stance[side] > 0.5f)
        {
            groundUnder[side] = footGround(base);
        }
        else
        {
            const glm::vec2 landing = groundPoint({local.x, 0.0f, c.landing[side] * k});
            const glm::vec2 liftoff = lock.valid ? lock.point : base;
            const float from = footGround(liftoff);
            const float to = footGround(landing);
            float height = from;
            if (std::abs(to - from) > 0.005f)
            {
                // Where along the way the foot first meets the new level (the
                // edge), and how far along the way it is now.
                const glm::vec2 way = landing - liftoff;
                const float wayLength2 = glm::dot(way, way);
                const float progress = wayLength2 > 1.0e-6f ? glm::clamp(glm::dot(base - liftoff, way) / wayLength2, 0.0f, 1.0f) : 1.0f;
                float before = 0.0f;
                float edge = 1.0f;
                for (int halving = 0; halving < 12; ++halving)
                {
                    const float middle = 0.5f * (before + edge);
                    if (std::abs(footGround(liftoff + way * middle) - to) < 0.005f)
                        edge = middle;
                    else
                        before = middle;
                }
                // Up: raised before the toes reach the edge. Down: lowered
                // once the heel is past it, by the landing (the foot slows to
                // a stop there, so even a short last stretch takes a while).
                const float share = to > from ? smoothstep(std::max(0.0f, edge - 0.4f), std::max(edge, 1.0e-3f), progress)
                                              : smoothstep(edge, std::min(1.0f, edge + 0.35f), progress);
                height = glm::mix(from, to, share);
            }
            groundUnder[side] = std::max(height, footGround(base));
        }
        ankleTarget[side] = {at.x, groundUnder[side] + rise + local.y, at.y};
    }

    // ---- Pelvis: at its height over the ground between the feet, lowered
    //      just enough for both legs to reach -----------------------------------
    const glm::mat3 pelvisTurn = body * rotationY(c.pelvisYaw);
    const glm::vec3 swayed = body * glm::vec3 {c.pelvisSway * k, 0.0f, 0.0f};
    glm::vec3 pelvis {root.x + swayed.x, 0.5f * (groundUnder[0] + groundUnder[1]) + c.pelvisHeight * k, root.y + swayed.z};
    const float legLength = (Mannequin::thighLength + Mannequin::shinLength) * k;
    const float reach = legLength * 0.995f;
    glm::vec3 hipOffset[2];
    float lower = 0.0f;
    for (int side = 0; side < 2; ++side)
    {
        hipOffset[side] = pelvisTurn * glm::vec3 {(side == 0 ? 1.0f : -1.0f) * hipSpacing * k, -hipBelowPelvis * k, 0.0f};
        const glm::vec3 hip = pelvis + hipOffset[side];
        const glm::vec3 toAnkle = ankleTarget[side] - hip;
        const float across = glm::length(glm::vec2 {toAnkle.x, toAnkle.z});
        if (across >= reach)
            continue;
        const float down = std::sqrt(reach * reach - across * across);
        lower = std::max(lower, -toAnkle.y - down);
    }
    pelvis.y -= lower;

    // ---- Legs: two-bone IK, the knee bending forward ---------------------------
    const glm::vec3 forward = pelvisTurn * glm::vec3 {0.0f, 0.0f, 1.0f};
    const float thigh = Mannequin::thighLength * k;
    const float shin = Mannequin::shinLength * k;
    for (int side = 0; side < 2; ++side)
    {
        const glm::vec3 hip = pelvis + hipOffset[side];
        const glm::vec3 outward = pelvisTurn * glm::vec3 {side == 0 ? 1.0f : -1.0f, 0.0f, 0.0f};
        const glm::vec3 pole = glm::normalize(forward + 0.08f * outward);
        glm::vec3 toTarget = ankleTarget[side] - hip;
        float distance = glm::length(toTarget);
        const glm::vec3 direction = distance > 1.0e-5f ? toTarget / distance : glm::vec3 {0.0f, -1.0f, 0.0f};
        distance = glm::clamp(distance, std::abs(thigh - shin) + 0.01f, (thigh + shin) * 0.9999f);
        // Law of cosines for the angle at the hip between the thigh and the
        // line to the ankle.
        const float cosHip = glm::clamp((thigh * thigh + distance * distance - shin * shin) / (2.0f * thigh * distance), -1.0f, 1.0f);
        const float sinHip = std::sqrt(std::max(0.0f, 1.0f - cosHip * cosHip));
        glm::vec3 bend = pole - direction * glm::dot(pole, direction);
        bend = glm::length(bend) > 1.0e-4f ? glm::normalize(bend) : forward;
        const glm::vec3 knee = hip + thigh * (direction * cosHip + bend * sinHip);
        const glm::vec3 ankle = hip + direction * distance;

        out.parts[static_cast<std::size_t>(side == 0 ? SlotThighLeft : SlotThighRight)] = limb(hip, knee, forward, girth);
        out.parts[static_cast<std::size_t>(side == 0 ? SlotShinLeft : SlotShinRight)] = limb(knee, ankle, forward, girth);

        // Toes turned out a little.
        const glm::mat3 footTurn = body * rotationY(side == 0 ? 4.0f : -4.0f) * rotationX(-c.footPitch[side]);
        // The shoe: sole under the ankle, heel behind it, toes ahead.
        const glm::vec3 shoeCentre = ankle + footTurn * (glm::vec3 {0.0f, -0.5f * Mannequin::ankleHeight, 0.06f} * k);
        out.parts[static_cast<std::size_t>(side == 0 ? SlotFootLeft : SlotFootRight)] =
            placed(shoeCentre, footTurn, glm::vec3 {0.095f * girth, 0.085f * k, 0.25f * k});

        const glm::vec3 heel = ankle + footTurn * (glm::vec3 {0.0f, -Mannequin::ankleHeight, -heelBehind} * k);
        const glm::vec3 ball = ankle + footTurn * (glm::vec3 {0.0f, -Mannequin::ankleHeight, ballAhead} * k);
        out.ankle[side] = ankle;
        out.ankleTarget[side] = ankleTarget[side];
        out.heel[side] = heel;
        out.ball[side] = ball;
        out.planted[side] = c.stance[side] > 0.5f;
    }

    // ---- Pelvis, spine, neck and head ------------------------------------------
    out.parts[SlotPelvis] = placed(pelvis, pelvisTurn, glm::vec3 {girth, k, girth * 0.72f});
    const glm::vec3 waist = pelvis + pelvisTurn * glm::vec3 {0.0f, pelvisToWaist * k, 0.0f};
    const glm::mat3 chest = body * rotationY(c.pelvisYaw + c.twist) * rotationX(c.lean);
    out.parts[SlotTorso] = placed(waist, chest, glm::vec3 {girth, k, girth * 0.62f});

    const glm::vec3 neckBase = waist + chest * glm::vec3 {0.0f, Mannequin::torsoLength * k, 0.0f};
    const glm::vec3 neckTop = neckBase + chest * glm::vec3 {0.0f, Mannequin::neckLength * k, 0.0f};
    const glm::vec3 chestFront = chest * glm::vec3 {0.0f, 0.0f, 1.0f};
    out.parts[SlotNeck] = limb(neckBase, neckTop, chestFront, k);

    const glm::mat3 head = body * rotationY(c.headYaw) * rotationX(c.headPitch);
    out.parts[SlotHead] = placed(neckTop, head, glm::vec3 {k, k, k * 1.05f});
    out.parts[SlotHair] = out.parts[SlotHead];
    out.eye = neckTop + head * (glm::vec3 {0.0f, 0.125f, 0.085f} * k);
    out.lookYawDegrees = motion.yawDegrees + c.headYaw;

    // ---- Arms: swung from the shoulders; the right one holds the umbrella --------
    const float umbrella = glm::clamp(motion.umbrella, 0.0f, 1.0f);
    glm::vec3 rightHand {0.0f};
    for (int side = 0; side < 2; ++side)
    {
        const float sign = side == 0 ? 1.0f : -1.0f;
        float swing = c.armSwing[side];
        float elbow = c.elbow[side];
        float abduction = c.armOut[side];
        if (side == 1 && umbrella > 0.0f)
        {
            // Holding the umbrella up: the upper arm forward, the forearm
            // raised, the hand in front of the shoulder.
            swing = glm::mix(swing, 28.0f, umbrella);
            elbow = glm::mix(elbow, 100.0f, umbrella);
            abduction = glm::mix(abduction, 12.0f, umbrella);
        }
        const glm::vec3 shoulder = waist + chest * (glm::vec3 {sign * shoulderSpacing * look.build, shoulderHeight, 0.0f} * k);
        const glm::vec3 upperDirection = chest * (rotationZ(sign * abduction) * (rotationX(-swing) * glm::vec3 {0.0f, -1.0f, 0.0f}));
        const glm::vec3 elbowPoint = shoulder + upperDirection * (Mannequin::upperArmLength * k);
        const glm::vec3 lowerDirection = chest * (rotationZ(sign * abduction) * (rotationX(-(swing + elbow)) * glm::vec3 {0.0f, -1.0f, 0.0f}));
        const glm::vec3 wrist = elbowPoint + lowerDirection * (Mannequin::forearmLength * k);
        const glm::vec3 fingertips = wrist + lowerDirection * (Mannequin::handLength * k);

        const int upperSlot = side == 0 ? SlotUpperArmLeft : SlotUpperArmRight;
        const int lowerSlot = side == 0 ? SlotForearmLeft : SlotForearmRight;
        const int handSlot = side == 0 ? SlotHandLeft : SlotHandRight;
        out.parts[static_cast<std::size_t>(upperSlot)] = limb(shoulder, elbowPoint, chestFront, girth);
        out.parts[static_cast<std::size_t>(lowerSlot)] = limb(elbowPoint, wrist, chestFront, girth);
        out.parts[static_cast<std::size_t>(handSlot)] = limb(wrist, fingertips, chestFront, k);
        if (side == 1)
            rightHand = wrist + lowerDirection * (0.5f * Mannequin::handLength * k);
    }

    // ---- The umbrella: a shaft up from the hand, the canopy opening on top -------
    const float shaftLength = 0.95f * k;
    out.parts[SlotUmbrellaShaft] = placed(rightHand - glm::vec3 {0.0f, 0.08f * k, 0.0f}, glm::mat3(1.0f),
                                          glm::vec3 {0.022f, shaftLength, 0.022f});
    const float open = umbrella * umbrella * (3.0f - 2.0f * umbrella);
    out.parts[SlotUmbrellaCanopy] = placed(rightHand + glm::vec3 {0.0f, shaftLength - 0.08f * k, 0.0f}, body,
                                           glm::vec3 {k * glm::max(open, 0.05f), k * (0.35f + 0.65f * open), k * glm::max(open, 0.05f)});
}
