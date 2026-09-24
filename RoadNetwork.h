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
// type: a crossroads never turns into a roundabout. The network is closed:
// no road leaves town, so the same cars circulate for ever.

enum class JunctionType
{
    SignalCross,   // signalised 4-way intersection
    SignalT,       // signalised T-junction
    GiveWayT,      // T-junction where the side road gives way
    Roundabout,    // 4-arm roundabout
    Bend           // a road turning a corner, with no side road
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
    // the middle of the road to the next junction.
    std::array<float, 4> armLength {};
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
    // Junctions sit on a 5 x 5 grid of points `spacing` apart, from -2 to +2
    // in each direction. The squares between the grid points are the cells
    // that make up the blocks.
    static constexpr float spacing = 100.0f;
    static constexpr int gridHalf = 2;

    static RoadNetwork makeCity();

    const std::vector<Junction>& junctions() const { return junctions_; }
    std::size_t junctionCount() const { return junctions_.size(); }
    int junctionAt(glm::vec2 centre) const;
    std::size_t centralJunction() const { return centralJunction_; }

    // The city blocks: every area enclosed by roads, each made of one or more
    // grid cells (square, long or L-shaped). The outline follows the kerb
    // (inset 0) or runs parallel to it further in, counter-clockwise. Its
    // corners follow the junction there: a kerb fillet, either side of a
    // bend, or the roundabout. Every outline of one block has the same number
    // of points, so two of them can be joined into a ring (the sidewalk).
    std::size_t blockCount() const { return blocks_.size(); }
    std::vector<glm::vec2> blockOutline(std::size_t block, float inset) const;

    // The outer kerb of the ring road, seen from outside the city: inset 0 is
    // the kerb, larger insets run parallel to it further out. Clockwise, so
    // the ground outside the city is on the left, as a block's is.
    std::vector<glm::vec2> outsideOutline(float inset) const;

    // Raised splitter island on a roundabout arm, as a closed outline.
    std::vector<glm::vec2> splitterIsland(std::size_t junction, int arm) const;

    // Street lamps along every road, plus the four Lab 3 lamps at the centre.
    const std::vector<StreetLamp>& streetLamps() const { return lamps_; }

    // Every road between two junctions. A road may run straight on past grid
    // points where nothing joins it.
    struct Road
    {
        std::size_t from = 0;
        int fromArm = 0;
        std::size_t to = 0;
        int toArm = 0;
        glm::vec2 start {0.0f};
        glm::vec2 end {0.0f};
    };
    const std::vector<Road>& roads() const { return roads_; }

    // How far from a junction centre the plain road begins (no markings of
    // the junction itself any more).
    static float junctionReach(const Junction& junction);

private:
    struct Cell
    {
        int x = 0;   // the cell spans x * spacing .. (x + 1) * spacing
        int z = 0;
    };
    struct Block
    {
        std::vector<Cell> cells;
    };

    std::vector<Junction> junctions_;
    std::vector<Road> roads_;
    std::vector<Block> blocks_;
    std::vector<StreetLamp> lamps_;
    std::size_t centralJunction_ = 0;

    void connect(std::size_t a, int armA, std::size_t b, int armB);
    void placeLamps();
    // The kerb around a set of cells. `outside` traces the set's boundary
    // the other way round, for the ground around the whole city.
    std::vector<glm::vec2> traceOutline(const std::vector<Cell>& cells, float inset, bool outside) const;
    void appendCorner(std::vector<glm::vec2>& outline, glm::vec2 corner, glm::vec2 quadrant,
                      float inset, bool reverse) const;
    // Where the kerb runs round the OUTSIDE of a bend: `in` and `out` are the
    // directions of travel along the kerb into and out of the corner.
    void appendOuterBend(std::vector<glm::vec2>& outline, glm::vec2 corner, glm::vec2 in, glm::vec2 out,
                         float inset) const;
};
