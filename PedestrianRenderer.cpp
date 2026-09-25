#include "PedestrianRenderer.h"

#include "LightManager.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace
{
    // A surface of revolution about the y axis from (radius, height)
    // control points (Lab 5), as MeshData.
    MeshData revolution(const std::vector<glm::vec2>& profile, unsigned int stacks, unsigned int slices)
    {
        return Mesh::bezierRevolutionData(profile, stacks, slices);
    }

    // The hair: a cap over the head, reaching lower at the back than over
    // the forehead, so the face shows.
    MeshData hairCap()
    {
        MeshData data = revolution({{0.0f, 0.262f}, {0.105f, 0.272f}, {0.13f, 0.17f}, {0.116f, 0.10f}}, 10, 16);
        for (Vertex& vertex : data.vertices)
        {
            const glm::vec2 around {vertex.position.x, vertex.position.z};
            const float front = glm::length(around) > 1.0e-4f ? around.y / glm::length(around) : 0.0f;
            const float t = glm::clamp((front + 0.2f) / 0.9f, 0.0f, 1.0f);
            const float edge = glm::mix(0.10f, 0.19f, t * t * (3.0f - 2.0f * t));
            vertex.position.y = std::max(vertex.position.y, edge);
            vertex.position.z -= 0.008f;
        }
        return data;
    }
}

PedestrianRenderer::PedestrianRenderer()
{
    // Profiles in metres for someone 1.75 m tall. Limbs run up the y axis
    // from 0 to 1 and are stretched to the bone; the rest are at full size.
    const auto set = [this](BodyShape shape, const MeshData& data)
    {
        meshes_[static_cast<std::size_t>(shape)] = Mesh(data);
    };
    set(BodyShape::Pelvis, revolution({{0.0f, -0.11f}, {0.21f, -0.11f}, {0.19f, 0.07f}, {0.13f, 0.14f}, {0.0f, 0.12f}}, 8, 14));
    set(BodyShape::Torso, revolution({{0.0f, -0.03f}, {0.15f, -0.03f}, {0.13f, 0.12f}, {0.23f, 0.33f},
                                      {0.20f, 0.47f}, {0.05f, 0.45f}, {0.0f, 0.45f}}, 12, 16));
    set(BodyShape::Neck, revolution({{0.0f, 0.0f}, {0.055f, 0.0f}, {0.05f, 0.5f}, {0.05f, 1.0f}, {0.0f, 1.0f}}, 5, 10));
    set(BodyShape::Head, revolution({{0.0f, -0.01f}, {0.075f, -0.01f}, {0.115f, 0.06f}, {0.11f, 0.21f},
                                     {0.06f, 0.255f}, {0.0f, 0.25f}}, 12, 16));
    set(BodyShape::Hair, hairCap());
    set(BodyShape::UpperArm, revolution({{0.0f, -0.02f}, {0.058f, -0.02f}, {0.06f, 0.2f}, {0.045f, 0.9f},
                                         {0.042f, 1.03f}, {0.0f, 1.03f}}, 8, 10));
    set(BodyShape::Forearm, revolution({{0.0f, -0.02f}, {0.043f, -0.02f}, {0.045f, 0.3f}, {0.031f, 0.95f},
                                        {0.029f, 1.02f}, {0.0f, 1.02f}}, 8, 10));
    set(BodyShape::Hand, revolution({{0.0f, 0.0f}, {0.035f, 0.0f}, {0.05f, 0.45f}, {0.03f, 0.95f}, {0.0f, 1.0f}}, 6, 8));
    set(BodyShape::Thigh, revolution({{0.0f, -0.03f}, {0.09f, -0.03f}, {0.10f, 0.2f}, {0.066f, 0.85f},
                                      {0.06f, 1.03f}, {0.0f, 1.03f}}, 8, 12));
    set(BodyShape::Shin, revolution({{0.0f, -0.02f}, {0.058f, -0.02f}, {0.064f, 0.25f}, {0.043f, 0.85f},
                                     {0.04f, 1.02f}, {0.0f, 1.02f}}, 8, 12));
    set(BodyShape::Foot, Mesh::beveledCubeData(0.22f));
    // The umbrella: eight panels, a dome with the rim below the tip.
    set(BodyShape::UmbrellaCanopy, revolution({{0.0f, 0.03f}, {0.34f, 0.03f}, {0.56f, -0.13f}, {0.58f, -0.30f}}, 8, 8));
    set(BodyShape::UmbrellaShaft, revolution({{0.5f, 0.0f}, {0.5f, 1.0f}}, 1, 6));
}

float PedestrianRenderer::shininess(std::size_t shape)
{
    switch (static_cast<BodyShape>(shape))
    {
    case BodyShape::Foot: return 36.0f;
    case BodyShape::UmbrellaCanopy: return 40.0f;
    case BodyShape::UmbrellaShaft: return 60.0f;
    case BodyShape::Head: case BodyShape::Neck: case BodyShape::Hand: return 18.0f;
    default: return 8.0f;
    }
}

bool PedestrianRenderer::matte(std::size_t shape)
{
    const BodyShape body = static_cast<BodyShape>(shape);
    return body != BodyShape::Foot && body != BodyShape::UmbrellaCanopy && body != BodyShape::UmbrellaShaft;
}

void PedestrianRenderer::begin(const glm::vec3& cameraPosition, const glm::mat4& viewProjection,
                               const glm::vec3& shadowReach)
{
    cameraPosition_ = cameraPosition;
    viewProjection_ = viewProjection;
    shadowReach_ = shadowReach;
    ++frame_;
    drawn_ = 0;
    for (std::vector<InstanceData>& list : instances_)
        list.clear();
}

void PedestrianRenderer::emit(const MannequinPose& pose, const WalkerLook& look, const glm::mat4& carry, float umbrella)
{
    for (int slot = 0; slot < static_cast<int>(bodySlotCount); ++slot)
    {
        if ((slot == SlotUmbrellaCanopy || slot == SlotUmbrellaShaft) && umbrella < 0.01f)
            continue;
        if (slot == SlotHair && look.bald)
            continue;
        InstanceData instance;
        instance.model = carry * pose.parts[static_cast<std::size_t>(slot)];
        instance.color = glm::vec4(look.color(slot), 0.0f);
        instances_[static_cast<std::size_t>(slotShape(slot))].push_back(instance);
    }
    ++drawn_;
}

void PedestrianRenderer::addCrowd(const std::vector<PedestrianPose>& poses, const std::vector<WalkerLook>& looks)
{
    if (cache_.size() < poses.size())
        cache_.resize(poses.size());

    for (std::size_t index = 0; index < poses.size() && index < looks.size(); ++index)
    {
        const PedestrianPose& person = poses[index];
        const WalkerMotion& motion = person.motion;

        // Nobody is posed or drawn who is out of view and casts no shadow
        // into it: the sphere holds the person and the ground their shadow
        // falls on.
        PointLight reach;
        reach.position = motion.position + glm::vec3 {0.0f, 0.9f, 0.0f} + 0.5f * shadowReach_;
        reach.range = (motion.umbrella > 0.01f ? 1.6f : 1.2f) + 0.5f * glm::length(shadowReach_);
        if (!LightBudget::reachInView(reach, viewProjection_))
            continue;

        // Near the camera every frame; further off every second or fourth,
        // staggered so the work spreads evenly over the frames.
        const float distance = glm::length(motion.position - cameraPosition_);
        const unsigned int interval = distance < 35.0f ? 1u : (distance < 90.0f ? 2u : 4u);
        Cached& cached = cache_[index];
        if (!cached.valid || (frame_ + static_cast<unsigned int>(index)) % interval == 0u)
        {
            const GroundHeight ground = [&person](glm::vec2 point) { return person.groundAt(point); };
            Mannequin::pose(looks[index], motion, ground, cached.pose);
            cached.root = motion.position;
            cached.yawDegrees = motion.yawDegrees;
            cached.valid = true;
            emit(cached.pose, looks[index], glm::mat4(1.0f), motion.umbrella);
            continue;
        }

        // Carry the last pose to where the person is now.
        glm::mat4 carry = glm::translate(glm::mat4(1.0f), motion.position);
        carry = glm::rotate(carry, glm::radians(motion.yawDegrees - cached.yawDegrees), {0.0f, 1.0f, 0.0f});
        carry = glm::translate(carry, -cached.root);
        emit(cached.pose, looks[index], carry, motion.umbrella);
    }
}

void PedestrianRenderer::addPerson(const WalkerLook& look, const WalkerMotion& motion, const GroundHeight& ground)
{
    Mannequin::pose(look, motion, ground, scratch_);
    emit(scratch_, look, glm::mat4(1.0f), motion.umbrella);
}
