#pragma once

#include "Collision.h"
#include "Mannequin.h"
#include "Simulation.h"
#include "World.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The people of the city (plan sections 4.6 and 5.1). They walk the
// sidewalks round every block, keeping to the right, and cross the roads at
// the zebra crossings: at a signalised junction on WALK, at a zebra when the
// traffic lets them, and over a roundabout arm in two halves with a wait on
// the splitter island. They step out only when every vehicle heading for
// the crossing can still stop comfortably, and once on it they never stop:
// the traffic keeps clear of them (TrafficSystem's crossing rules). Like the
// vehicles they move in fixed 1/60 s steps and are drawn blended between the
// last two.

// One person as drawn this frame.
struct PedestrianPose
{
    std::size_t id = 0;
    float height = 1.75f;
    WalkerMotion motion;
    // Their feet are on the kerb top everywhere except on the road of the
    // crossing they are at: across coordinates roadFrom..roadTo of the strip
    // through stripOrigin along stripAcross.
    bool nearRoad = false;
    glm::vec2 stripOrigin {0.0f};
    glm::vec2 stripAcross {1.0f, 0.0f};
    float roadFrom = 0.0f;
    float roadTo = 0.0f;

    float groundAt(glm::vec2 point) const;
};

// Measurements for --soak and the HUD.
struct PedestrianStats
{
    std::size_t overlapSteps = 0;      // steps in which a vehicle body touched a person
    std::size_t overlapPairsNow = 0;
    float closestVehicleGap = 1.0e9f;  // smallest vehicle-to-person gap seen, metres
    float longestWait = 0.0f;          // longest anyone stood still (idling aside), seconds
    std::size_t crossingsMade = 0;     // times someone walked over a crossing
    std::size_t startsAgainstLights = 0;   // must stay 0
    // For a failed run: the first touch, and where the longest wait was.
    std::string firstTouch;
    std::string longestWaitAt;
};

class PedestrianSystem
{
public:
    PedestrianSystem(const World& world, const TrafficSystem& traffic, std::size_t count = 80, unsigned int seed = 12345u);

    void reset();

    // One fixed step, after the traffic's. Tells the traffic who waits at
    // and who is on each crossing, for its next step.
    void update(float dt, TrafficSystem& traffic);

    // The player (car and on foot): nobody walks into them, and nobody steps
    // onto a crossing they are standing on.
    void setGuests(const std::vector<Guest>& guests) { guests_ = guests; }

    // Blended poses for drawing, as TrafficSystem::interpolatePoses.
    void interpolate(float alpha, std::vector<PedestrianPose>& poses) const;

    const std::vector<WalkerLook>& looks() const { return looks_; }
    std::size_t count() const { return walkers_.size(); }
    std::size_t waitingCount() const;
    std::size_t crossingCount() const;

    // Everyone's body, for the player's collisions.
    void bodies(std::vector<OrientedBox>& out) const;

    // Rain (0..1, from the weather): as it sets in, umbrellas go up one
    // person after another, and everyone walks a little faster (up to 10 %).
    void setRain(float amount);
    void openUmbrellasNow(float amount);

    const PedestrianStats& stats() const { return stats_; }
    void resetStats();

    // The sidewalk network: every path clear of posts, trunks, walls and the
    // road, every crossing joined to the sidewalks at both ends, and every
    // block reachable from every other. Used by --self-test.
    bool selfTest(std::string& report) const;

    // One line per person, for a failed soak run.
    std::string describe(const TrafficSystem& traffic) const;

    static constexpr float bodyRadius = 0.28f;

private:
    enum class State
    {
        Lane,       // walking along a sidewalk
        Idle,       // standing about on the sidewalk for a moment
        ToKerb,     // from the sidewalk to a place at the kerb
        Wait,       // at the kerb (or on the island), waiting to cross
        Cross,      // over the road
        FromKerb    // from the far kerb back onto a sidewalk
    };

    // A walking line round one block's sidewalk (or round the outside of the
    // ring road). Each block has two, one for each direction: people keep to
    // the right, so the one walked with the road on your right is the one
    // nearer the kerb. They swerve round lamp posts, signal poles and shelters.
    struct Attachment
    {
        std::size_t crossing = 0;
        int end = 0;
        float s = 0.0f;
    };
    struct Lane
    {
        std::size_t loop = 0;
        std::vector<glm::vec2> points;
        std::vector<float> lengths;   // along the line to each point
        float total = 0.0f;
        std::vector<Attachment> attachments;   // by s
    };
    // Where a crossing's end joins the sidewalks: the loop, and the place on
    // each of its two lanes. A refuge end (the island) joins nothing.
    struct CrossingEnd
    {
        bool refuge = false;
        std::size_t loop = 0;
        std::size_t lane[2] {};
        float s[2] {};
    };

    struct Walker
    {
        std::size_t id = 0;
        float preferredSpeed = 1.4f;
        State state = State::Lane;

        std::size_t lane = 0;
        float s = 0.0f;
        glm::vec2 position {0.0f};
        glm::vec2 target {0.0f};
        float speed = 0.0f;
        float yawDegrees = 0.0f;

        // The crossing being approached, waited at or walked over.
        std::size_t crossing = 0;
        int fromEnd = 0;
        int slot = 0;
        float reaction = 0.0f;
        bool onward = false;      // a roundabout: the second half follows
        std::size_t joinLane = 0;
        float joinS = 0.0f;

        float idleLeft = 0.0f;
        float stillSeconds = 0.0f;
        float sinceCrossing = 100.0f;   // metres walked since the last crossing
        float umbrella = 0.0f;
        float umbrellaDelay = 0.0f;

        // Animation.
        float phase = 0.0f;
        FootLock lock[2];
        Motion motion = Motion::Walk;
        Motion previousMotion = Motion::Walk;
        float fade = 1.0f;
        float stateSeconds = 0.0f;

        // The previous step, for blending.
        glm::vec2 previousPosition {0.0f};
        float previousYaw = 0.0f;
        float previousSpeed = 0.0f;
        float previousPhase = 0.0f;
        float previousFade = 1.0f;
        float previousStateSeconds = 0.0f;
        float previousUmbrella = 0.0f;
    };

    const World& world_;
    const RoadNetwork& network_;
    std::size_t count_ = 80;
    unsigned int seed_ = 12345u;
    unsigned int randomState_ = 12345u;
    double clock_ = 0.0;

    std::vector<Lane> lanes_;              // two per loop: 2 * loop + 0 and + 1
    std::size_t loopCount_ = 0;
    std::vector<CrossingEnd> ends_;        // two per crossing: 2 * crossing + end
    std::vector<std::size_t> partner_;     // the other half of a roundabout crossing, or itself
    std::vector<int> usableSlots_;         // waiting places that fit at each end (2 * crossing + end)
    std::vector<Walker> walkers_;
    std::vector<WalkerLook> looks_;
    std::vector<Guest> guests_;
    float umbrellaTarget_ = 0.0f;
    float rain_ = 0.0f;
    float pace(const Walker& walker) const { return walker.preferredSpeed * (1.0f + 0.1f * rain_); }
    PedestrianStats stats_;

    // Everything solid people must walk round, bucketed on a 4 m grid, for
    // laying out the walking lines (and checking them).
    std::vector<Circle> posts_;
    std::vector<OrientedBox> boxes_;
    std::vector<std::vector<std::uint32_t>> grid_;
    static constexpr float gridCell = 4.0f;
    static constexpr float gridReach = 320.0f;
    int gridSide_ = 0;

    // Scratch space for one step.
    std::vector<CrossingState> crossingStates_;
    std::vector<signed char> clearCache_;
    std::vector<std::vector<std::size_t>> laneOrder_;
    std::vector<OrientedBox> vehicleBodies_;

    // --- the sidewalk network (built once)
    void buildLanes();
    void buildCrossingEnds();
    glm::vec2 lanePoint(const Lane& lane, float s, glm::vec2* direction = nullptr) const;
    float nearestS(const Lane& lane, glm::vec2 point, float* distance = nullptr) const;
    bool pointClear(glm::vec2 point, float radius) const;

    // --- waiting places at a crossing's end
    glm::vec2 slotPosition(std::size_t crossing, int end, int slot) const;
    glm::vec2 crossingTarget(const Walker& walker) const;
    int freeSlot(std::size_t crossing, int end, std::size_t except) const;
    float alongLane(std::size_t crossing, int fromEnd) const;

    // --- decisions
    // `waitedLong`: the walker asking has stood at the kerb a long time.
    bool mayStart(std::size_t crossing, const TrafficSystem& traffic, bool waitedLong);
    bool guestOnCrossing(std::size_t crossing) const;
    void startApproach(Walker& walker, std::size_t crossing, int end);
    void arriveAcross(Walker& walker);
    void setMotion(Walker& walker, Motion motion);
    bool nearCrossing(const Walker& walker) const;

    void placeWalkers();
    void measure(const TrafficSystem& traffic);
    unsigned int nextRandom();
    float random01();
};
