#pragma once

#include "Collision.h"
#include "LightManager.h"
#include "RoadNetwork.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstddef>
#include <string>
#include <vector>

// Everything that stands in the city besides the roads: buildings, trees,
// crates, signs, signal heads, billboards and neon signs. One list serves both
// the renderer (what to draw) and the player (what to bump into), so the two
// can never disagree about where something is.
//
// Facing angles use the vehicles' heading convention: a thing with facing
// angle a faces the direction (sin a, cos a) in world (x, z).

struct Building
{
    glm::vec3 position {0.0f};   // centre of the box
    glm::vec3 size {1.0f};
    glm::vec3 tint {1.0f};
};

struct Tree
{
    glm::vec2 position {0.0f};
    float scale = 1.0f;
    float twistDegrees = 0.0f;
};

struct Crate
{
    glm::vec3 position {0.0f};
    float yawDegrees = 0.0f;
};

struct RoadSign
{
    glm::vec3 position {0.0f};
    float yawDegrees = 0.0f;
    glm::vec3 color {1.0f};
};

// A signal head on the driver's right of an approach, facing the cars.
struct SignalHead
{
    std::size_t junction = 0;
    int arm = 0;
    glm::vec2 foot {0.0f};
    float yawDegrees = 0.0f;   // the lenses face local -z; this turns them to the arm
};

struct GiveWaySign
{
    glm::vec2 foot {0.0f};
    float yawDegrees = 0.0f;
};

// A roadside billboard on two posts. At night its face is lit from behind,
// except for the one at the central crossroads: that one is lit from the
// front by a small lamp on an arm below it - the Lab 3 spot light.
struct Billboard
{
    glm::vec2 centre {0.0f};     // between the two posts, on the ground
    float facingDegrees = 0.0f;  // the printed face looks this way
    int design = 0;              // which picture (World::billboardText)
    bool spotLit = false;
};

// Neon lettering on a building front.
struct NeonSign
{
    glm::vec3 centre {0.0f};     // middle of the lettering, just off the wall
    float facingDegrees = 0.0f;
    std::string text;
    glm::vec3 color {1.0f};
    float letterHeight = 0.8f;   // metres
    bool flickers = false;       // one tired tube, for character
};

// The spot light on the lamp arm of the spot-lit billboard (Lab 3).
struct SpotLamp
{
    glm::vec3 position {0.0f};
    glm::vec3 target {0.0f};
    float innerDegrees = 30.0f;
    float outerDegrees = 42.0f;
};

class World
{
public:
    static World make(const RoadNetwork& network);

    static constexpr float billboardWidth = 6.0f;
    static constexpr float billboardHeight = 3.0f;
    static constexpr float billboardBottom = 2.6f;   // underside of the panel above the ground
    static const std::vector<std::string>& billboardText();

    const std::vector<Building>& buildings() const { return buildings_; }
    const std::vector<Tree>& trees() const { return trees_; }
    const std::vector<Crate>& crates() const { return crates_; }
    const std::vector<RoadSign>& roadSigns() const { return roadSigns_; }
    const std::vector<SignalHead>& signalHeads() const { return signalHeads_; }
    const std::vector<GiveWaySign>& giveWaySigns() const { return giveWaySigns_; }
    const std::vector<Billboard>& billboards() const { return billboards_; }
    const std::vector<NeonSign>& neonSigns() const { return neonSigns_; }
    const SpotLamp& spotLamp() const { return spotLamp_; }

    // Night lights besides the street lamps: the coloured spill of every neon
    // sign and the glow in front of every back-lit billboard. They join the
    // street lamps in the light budget.
    std::vector<PointLight> signLights() const;

    // ---- Collision ----------------------------------------------------------
    // Solid things everyone bumps into: buildings, crates, billboard panels
    // (boxes), and trunks, posts and poles (circles).
    const std::vector<OrientedBox>& solidBoxes() const { return solidBoxes_; }
    const std::vector<Circle>& solidPosts() const { return solidPosts_; }
    // The raised roundabout islands stop a car but not a person on foot; the
    // fountain basin stops both.
    const std::vector<Circle>& islands() const { return islands_; }
    const std::vector<Circle>& fountains() const { return fountains_; }

    // Height of the ground at a point: the asphalt, or the raised sidewalks,
    // lawns and splitter islands behind the kerbs.
    float surfaceHeight(glm::vec2 point) const;

    // Nobody may leave this square (the lawn runs on towards the fog).
    static constexpr float boundary = 300.0f;

private:
    std::vector<Building> buildings_;
    std::vector<Tree> trees_;
    std::vector<Crate> crates_;
    std::vector<RoadSign> roadSigns_;
    std::vector<SignalHead> signalHeads_;
    std::vector<GiveWaySign> giveWaySigns_;
    std::vector<Billboard> billboards_;
    std::vector<NeonSign> neonSigns_;
    SpotLamp spotLamp_;

    std::vector<OrientedBox> solidBoxes_;
    std::vector<Circle> solidPosts_;
    std::vector<Circle> islands_;
    std::vector<Circle> fountains_;

    // Outlines of everything raised above the road, for surfaceHeight.
    std::vector<std::vector<glm::vec2>> raised_;
    std::vector<glm::vec2> cityEdge_;
};
