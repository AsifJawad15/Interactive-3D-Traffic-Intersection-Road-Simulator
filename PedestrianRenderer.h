#pragma once

#include "Mannequin.h"
#include "Mesh.h"
#include "Pedestrians.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <cstddef>
#include <vector>

// Draws the people. Every body shape (pelvis, torso, head, upper arm, ...) is
// one mesh, a Bezier surface of revolution like the lamp posts, and all the
// copies of it in the city go out in one instanced draw call: about a dozen
// draw calls for the whole crowd, however many people there are.
//
// Posing someone (the gait, the blending, the leg IK) is done on the CPU. Near
// the camera it is done every frame; further away every second or fourth
// frame, and in between the last pose is carried along with the person, so
// the far crowd costs little and still moves smoothly.
class PedestrianRenderer
{
public:
    PedestrianRenderer();

    // Starts a frame's list of parts. `shadowReach` is where the shadow of
    // a person's head falls, from their feet (zero without shadows).
    void begin(const glm::vec3& cameraPosition, const glm::mat4& viewProjection,
               const glm::vec3& shadowReach = glm::vec3(0.0f));
    // Everyone in view, or whose shadow is.
    void addCrowd(const std::vector<PedestrianPose>& poses, const std::vector<WalkerLook>& looks);
    // One more person posed on the spot (you, on foot).
    void addPerson(const WalkerLook& look, const WalkerMotion& motion, const GroundHeight& ground);

    const Mesh& mesh(std::size_t shape) const { return meshes_[shape]; }
    const std::vector<InstanceData>& instances(std::size_t shape) const { return instances_[shape]; }
    static float shininess(std::size_t shape);
    static bool matte(std::size_t shape);
    InstanceBuffer& buffer() { return buffer_; }
    std::size_t peopleDrawn() const { return drawn_; }

private:
    struct Cached
    {
        MannequinPose pose;
        glm::vec3 root {0.0f};
        float yawDegrees = 0.0f;
        bool valid = false;
    };

    std::array<Mesh, bodyShapeCount> meshes_;
    std::array<std::vector<InstanceData>, bodyShapeCount> instances_;
    InstanceBuffer buffer_;
    std::vector<Cached> cache_;
    MannequinPose scratch_;
    glm::vec3 cameraPosition_ {0.0f};
    glm::mat4 viewProjection_ {1.0f};
    glm::vec3 shadowReach_ {0.0f};
    unsigned int frame_ = 0;
    std::size_t drawn_ = 0;

    void emit(const MannequinPose& pose, const WalkerLook& look, const glm::mat4& carry, float umbrella);
};
