#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <cstddef>
#include <string>
#include <vector>

// The city's roads: where the junctions are, what kind each one is, and the
// cross-section every road shares. This is pure geometry - the traffic system
// builds its routes from it and the renderer builds its meshes from it, so
// the painted lanes and the driven lanes cannot disagree.
//
// World x is east and world z is north. Every junction keeps one permanent
// type: a crossroads never turns into a roundabout.

enum class JunctionType
{
    SignalCross,   // signalised 4-way intersection
    SignalT,       // signalised T-junction
    GiveWayT,      // T-junction where the side road gives way
    Roundabout,    // 4-arm roundabout
    Bend           // corner of the loop road
};

// Arms by compass direction from the junction centre.
enum Arm : int
{
    ArmNorth = 0,
    ArmEast = 1,
    ArmSouth = 2,
    ArmWest = 3
};

glm::vec2 armDirection(int arm);
const char* armName(int arm);

struct Junction
{
    std::string name;
    JunctionType type = JunctionType::SignalCross;
    glm::vec2 centre {0.0f};
    std::array<bool, 4> hasArm {};
    // Distance from the centre to where this junction's routes start and end:
    // the middle of the road to the next junction, or the edge of town.
    std::array<float, 4> armLength {};
    std::array<bool, 4> leavesTown {};
    // Give-way T-junction: the arms of the road that has priority.
    std::array<bool, 4> majorArm {};
    bool fountain = false;

    bool isIntersection() const
    {
        return type == JunctionType::SignalCross || type == JunctionType::SignalT ||
               type == JunctionType::GiveWayT;
    }
    bool isSignalised() const
    {
        return type == JunctionType::SignalCross || type == JunctionType::SignalT;
    }
};

struct StreetLamp
{
    glm::vec3 position {0.0f};   // foot of the post
    bool lab = false;            // one of the four Lab 3 lamps at the centre
};

class RoadNetwork
{
public:
    // ---- Cross-section (plan section 3.3) ---------------------------------
    static constexpr std::array<float, 2> laneOffsets = {1.75f, 5.25f};   // inner, outer
    static constexpr float laneWidth = 3.5f;
    static constexpr float halfWidth = 7.0f;          // kerb to centreline
    static constexpr float sidewalkWidth = 4.5f;
    static constexpr float roadY = 0.06f;             // asphalt surface
    static constexpr float kerbTopY = 0.21f;          // sidewalk surface

    // ---- Junction geometry -------------------------------------------------
    static constexpr float cornerRadius = 5.0f;       // kerb fillet at intersections
    static constexpr float boxHalf = halfWidth + cornerRadius;   // 12: where turns start
    static constexpr float crossingNear = 12.6f;      // zebra band at intersections
    static constexpr float crossingFar = 15.4f;
    static constexpr float stopLine = 16.0f;          // painted stop line
    static constexpr float bendRadius = 20.0f;        // loop corners, road centreline
    static constexpr float ringRadius = 13.0f;        // roundabout circulating lane
    static constexpr float islandRadius = 9.5f;
    static constexpr float roundaboutRadius = 17.0f;  // outer kerb of the circulating road
    static constexpr float entryRadius = 8.0f;        // roundabout entry and exit arcs
    static constexpr float roundaboutCrossingNear = 25.0f;
    static constexpr float roundaboutCrossingFar = 28.0f;

    // ---- City layout -------------------------------------------------------
    static constexpr float spacing = 100.0f;
    static constexpr float outOfTownLength = 240.0f;  // where through-traffic appears and leaves
    static constexpr float visibleRoadLength = 900.0f;

    static RoadNetwork makeCity();

    const std::vector<Junction>& junctions() const { return junctions_; }
    std::size_t junctionCount() const { return junctions_.size(); }
    int junctionAt(glm::vec2 centre) const;
    std::size_t centralJunction() const { return centralJunction_; }

    // The four city blocks inside the loop road. The outline follows the kerb
    // (inset 0) or runs parallel to it further in; its corners follow the
    // junction there: a kerb fillet, the inside of a bend, or the roundabout.
    std::size_t blockCount() const { return blocks_.size(); }
    std::vector<glm::vec2> blockOutline(std::size_t block, float inset) const;

    // Raised splitter island on a roundabout arm, as a closed outline.
    std::vector<glm::vec2> splitterIsland(std::size_t junction, int arm) const;

    // Street lamps along every road, plus the four Lab 3 lamps at the centre.
    const std::vector<StreetLamp>& streetLamps() const { return lamps_; }

    // Every road between two junctions (or from a junction out of town).
    struct Road
    {
        std::size_t from = 0;
        int fromArm = 0;
        int to = -1;       // -1: leaves town
        int toArm = 0;
        glm::vec2 start {0.0f};
        glm::vec2 end {0.0f};
    };
    const std::vector<Road>& roads() const { return roads_; }

    // How far from a junction centre the plain road begins (no markings of
    // the junction itself any more).
    static float junctionReach(const Junction& junction);

private:
    struct Block
    {
        glm::vec2 minimum {0.0f};   // south-west corner junction centre
        glm::vec2 maximum {0.0f};   // north-east corner junction centre
    };

    std::vector<Junction> junctions_;
    std::vector<Road> roads_;
    std::vector<Block> blocks_;
    std::vector<StreetLamp> lamps_;
    std::size_t centralJunction_ = 0;

    void connect(std::size_t a, int armA, std::size_t b, int armB);
    void leaveTown(std::size_t junction, int arm);
    void placeLamps();
    void appendCorner(std::vector<glm::vec2>& outline, glm::vec2 corner, glm::vec2 quadrant,
                      float inset, bool reverse) const;
};
