#include "Pedestrians.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace
{
    constexpr std::size_t none = static_cast<std::size_t>(-1);

    // The two walking lines of every sidewalk, as distances in from the kerb
    // (the sidewalk is 4.5 m wide): nearer the kerb for those walking with the
    // road on their right.
    constexpr std::array<float, 2> laneInsets = {2.0f, 3.3f};
    // How far a line may swerve to get round something: never nearer the
    // kerb than this, and onto the grass behind the sidewalk at most this far.
    constexpr float nearestToKerb = 1.7f;
    constexpr float furthestFromKerb = 6.2f;
    constexpr float sampleSpacing = 0.5f;
    // Clearance kept from posts and walls when the lines are laid out, and
    // what the self-test demands of the result.
    constexpr float layoutClearance = 0.52f;
    constexpr float requiredClearance = 0.3f;

    // Waiting at the kerb: two abreast, rows 0.65 m apart, three rows (on
    // an island, one behind the other).
    constexpr float slotRowSpacing = 0.6f;
    constexpr float slotColumnOffset = 0.27f;
    constexpr float crossingLaneOffset = 0.62f;   // keep right on the band
    constexpr int maximumSlots = 6;

    constexpr float motionFadeSeconds = 0.25f;
    constexpr float followSpacing = 0.95f;         // centre to centre
    constexpr float crossChance = 0.42f;
    constexpr float turnBackChance = 0.06f;
    constexpr float idleEverySeconds = 110.0f;     // on average, while walking

    float approach(float value, float target, float up, float down, float dt)
    {
        return value < target ? std::min(target, value + up * dt) : std::max(target, value - down * dt);
    }

    float blendAngle(float from, float to, float alpha)
    {
        const float difference = std::fmod(to - from + 540.0f, 360.0f) - 180.0f;
        return from + difference * alpha;
    }

    float headingOf(glm::vec2 direction)
    {
        return glm::degrees(std::atan2(direction.x, direction.y));
    }

    // Right of a direction of travel, in the heading convention (left is
    // (f.y, -f.x) there, as for the vehicles).
    glm::vec2 rightOf(glm::vec2 forward)
    {
        return {-forward.y, forward.x};
    }

    float wrap(float s, float total)
    {
        s = std::fmod(s, total);
        return s < 0.0f ? s + total : s;
    }
}

float PedestrianPose::groundAt(glm::vec2 point) const
{
    if (!nearRoad)
        return RoadNetwork::kerbTopY;
    const float a = glm::dot(point - stripOrigin, stripAcross);
    return a > roadFrom && a < roadTo ? RoadNetwork::roadY : RoadNetwork::kerbTopY;
}

PedestrianSystem::PedestrianSystem(const World& world, const TrafficSystem& traffic, std::size_t count, unsigned int seed)
    : world_(world), network_(traffic.network()), count_(count), seed_(seed)
{
    // Everything solid, on a coarse grid for the layout below.
    posts_ = world.solidPosts();
    for (const Circle& fountain : world.fountains())
        posts_.push_back(fountain);
    boxes_ = world.solidBoxes();
    gridSide_ = static_cast<int>(2.0f * gridReach / gridCell);
    grid_.assign(static_cast<std::size_t>(gridSide_ * gridSide_), {});
    const auto insert = [this](glm::vec2 centre, float radius, std::uint32_t index)
    {
        const int x0 = std::max(0, static_cast<int>((centre.x - radius + gridReach) / gridCell));
        const int x1 = std::min(gridSide_ - 1, static_cast<int>((centre.x + radius + gridReach) / gridCell));
        const int z0 = std::max(0, static_cast<int>((centre.y - radius + gridReach) / gridCell));
        const int z1 = std::min(gridSide_ - 1, static_cast<int>((centre.y + radius + gridReach) / gridCell));
        for (int z = z0; z <= z1; ++z)
        {
            for (int x = x0; x <= x1; ++x)
                grid_[static_cast<std::size_t>(z * gridSide_ + x)].push_back(index);
        }
    };
    for (std::size_t index = 0; index < posts_.size(); ++index)
        insert(posts_[index].centre, posts_[index].radius + 1.0f, static_cast<std::uint32_t>(index));
    for (std::size_t index = 0; index < boxes_.size(); ++index)
        insert(boxes_[index].centre, glm::length(boxes_[index].halfExtents) + 1.0f,
               static_cast<std::uint32_t>(posts_.size() + index));

    buildLanes();
    buildCrossingEnds();
    // As many waiting places at each end as fit (at X0 the corner signs
    // leave room for two rows only).
    usableSlots_.assign(network_.crossings().size() * 2, 0);
    for (std::size_t crossing = 0; crossing < network_.crossings().size(); ++crossing)
    {
        for (int end = 0; end < 2; ++end)
        {
            int& usable = usableSlots_[crossing * 2 + static_cast<std::size_t>(end)];
            while (usable < maximumSlots)
            {
                const glm::vec2 spot = slotPosition(crossing, end, usable);
                if (world_.surfaceHeight(spot) == RoadNetwork::roadY || !pointClear(spot, requiredClearance))
                    break;
                ++usable;
            }
        }
    }
    crossingStates_.assign(network_.crossings().size(), CrossingState {});
    clearCache_.assign(network_.crossings().size(), -1);
    laneOrder_.assign(lanes_.size(), {});
    reset();
}

// ---------------------------------------------------------------------------
// The sidewalk network
// ---------------------------------------------------------------------------

bool PedestrianSystem::pointClear(glm::vec2 point, float radius) const
{
    const int x = static_cast<int>((point.x + gridReach) / gridCell);
    const int z = static_cast<int>((point.y + gridReach) / gridCell);
    if (x < 0 || z < 0 || x >= gridSide_ || z >= gridSide_)
        return true;
    for (const std::uint32_t index : grid_[static_cast<std::size_t>(z * gridSide_ + x)])
    {
        if (index < posts_.size())
        {
            const Circle& post = posts_[index];
            if (glm::length(point - post.centre) < post.radius + radius)
                return false;
        }
        else
        {
            const OrientedBox& box = boxes_[index - posts_.size()];
            if (glm::length(point - closestPointOnBox(box, point)) < radius)
                return false;
        }
    }
    return true;
}

void PedestrianSystem::buildLanes()
{
    // One loop per block, and one round the outside of the ring road. Each
    // outline of a block has the same number of points at every inset, so
    // the point k of two insets are opposite each other: that gives the
    // direction "away from the road" at every point.
    loopCount_ = network_.blockCount() + 1;
    lanes_.assign(loopCount_ * 2, Lane {});
    const auto outline = [this](std::size_t loop, float inset)
    {
        return loop < network_.blockCount() ? network_.blockOutline(loop, inset) : network_.outsideOutline(inset);
    };

    for (std::size_t loop = 0; loop < loopCount_; ++loop)
    {
        for (int side = 0; side < 2; ++side)
        {
            const float inset = laneInsets[static_cast<std::size_t>(side)];
            const std::vector<glm::vec2> base = outline(loop, inset);
            const std::vector<glm::vec2> deeper = outline(loop, inset + 0.5f);

            // Resample the closed outline evenly, carrying the inward direction.
            std::vector<glm::vec2> corners;
            std::vector<glm::vec2> inwards;
            for (std::size_t index = 0; index < base.size(); ++index)
            {
                if (!corners.empty() && glm::length(base[index] - corners.back()) < 1.0e-3f)
                    continue;
                corners.push_back(base[index]);
                const glm::vec2 in = index < deeper.size() ? deeper[index] - base[index] : glm::vec2 {0.0f};
                inwards.push_back(glm::length(in) > 1.0e-4f ? glm::normalize(in) : glm::vec2 {0.0f});
            }
            if (glm::length(corners.front() - corners.back()) < 1.0e-3f)
            {
                corners.pop_back();
                inwards.pop_back();
            }
            float perimeter = 0.0f;
            for (std::size_t index = 0; index < corners.size(); ++index)
                perimeter += glm::length(corners[(index + 1) % corners.size()] - corners[index]);
            const std::size_t samples = std::max<std::size_t>(8, static_cast<std::size_t>(std::lround(perimeter / sampleSpacing)));
            const float step = perimeter / static_cast<float>(samples);

            std::vector<glm::vec2> points;
            std::vector<glm::vec2> normals;
            std::size_t segment = 0;
            float segmentStart = 0.0f;
            for (std::size_t sample = 0; sample < samples; ++sample)
            {
                const float s = static_cast<float>(sample) * step;
                float segmentLength = glm::length(corners[(segment + 1) % corners.size()] - corners[segment]);
                while (s > segmentStart + segmentLength && segment + 1 < corners.size())
                {
                    segmentStart += segmentLength;
                    ++segment;
                    segmentLength = glm::length(corners[(segment + 1) % corners.size()] - corners[segment]);
                }
                const float t = segmentLength > 1.0e-5f ? glm::clamp((s - segmentStart) / segmentLength, 0.0f, 1.0f) : 0.0f;
                const std::size_t next = (segment + 1) % corners.size();
                points.push_back(glm::mix(corners[segment], corners[next], t));
                glm::vec2 normal = glm::mix(inwards[segment], inwards[next], t);
                normals.push_back(glm::length(normal) > 1.0e-4f ? glm::normalize(normal) : inwards[segment]);
            }

            // Swerve round whatever stands in the way: the smallest sideways
            // move that clears it, spread out ahead and behind so the line
            // bends gently round it.
            const std::size_t n = points.size();
            std::vector<float> needed(n, 0.0f);
            for (std::size_t index = 0; index < n; ++index)
            {
                if (pointClear(points[index], layoutClearance))
                    continue;
                for (float move = 0.1f; move < 5.0f; move += 0.1f)
                {
                    bool found = false;
                    for (const float sign : {-1.0f, 1.0f})
                    {
                        const float offset = sign * move;
                        if (inset + offset < nearestToKerb || inset + offset > furthestFromKerb)
                            continue;
                        if (pointClear(points[index] + normals[index] * offset, layoutClearance))
                        {
                            needed[index] = offset;
                            found = true;
                            break;
                        }
                    }
                    if (found)
                        break;
                }
            }
            std::vector<float> widened(n, 0.0f);
            constexpr int spreadSamples = 6;
            for (std::size_t index = 0; index < n; ++index)
            {
                for (int k = -spreadSamples; k <= spreadSamples; ++k)
                {
                    const float other = needed[(index + n + static_cast<std::size_t>(k + static_cast<int>(n))) % n];
                    if (std::abs(other) > std::abs(widened[index]))
                        widened[index] = other;
                }
            }
            constexpr int smoothSamples = 4;
            std::vector<glm::vec2> smoothed(n);
            for (std::size_t index = 0; index < n; ++index)
            {
                float sum = 0.0f;
                for (int k = -smoothSamples; k <= smoothSamples; ++k)
                    sum += widened[(index + n + static_cast<std::size_t>(k + static_cast<int>(n))) % n];
                smoothed[index] = points[index] + normals[index] * (sum / static_cast<float>(2 * smoothSamples + 1));
            }

            // The line nearer the kerb is walked in the outline's direction
            // (the road on the right), the other the opposite way.
            if (side == 1)
                std::reverse(smoothed.begin(), smoothed.end());

            Lane& lane = lanes_[loop * 2 + static_cast<std::size_t>(side)];
            lane.loop = loop;
            lane.points = std::move(smoothed);
            lane.lengths.assign(n, 0.0f);
            for (std::size_t index = 1; index < n; ++index)
                lane.lengths[index] = lane.lengths[index - 1] + glm::length(lane.points[index] - lane.points[index - 1]);
            lane.total = lane.lengths.back() + glm::length(lane.points.front() - lane.points.back());
        }
    }
}

glm::vec2 PedestrianSystem::lanePoint(const Lane& lane, float s, glm::vec2* direction) const
{
    s = wrap(s, lane.total);
    const auto upper = std::upper_bound(lane.lengths.begin(), lane.lengths.end(), s);
    const std::size_t index = static_cast<std::size_t>(std::max<std::ptrdiff_t>(0, (upper - lane.lengths.begin()) - 1));
    const std::size_t next = (index + 1) % lane.points.size();
    const float start = lane.lengths[index];
    const float end = next == 0 ? lane.total : lane.lengths[next];
    const float t = end > start ? (s - start) / (end - start) : 0.0f;
    const glm::vec2 a = lane.points[index];
    const glm::vec2 b = lane.points[next];
    if (direction != nullptr)
    {
        const glm::vec2 along = b - a;
        *direction = glm::length(along) > 1.0e-5f ? glm::normalize(along) : glm::vec2 {0.0f, 1.0f};
    }
    return glm::mix(a, b, t);
}

float PedestrianSystem::nearestS(const Lane& lane, glm::vec2 point, float* distance) const
{
    float bestDistance = 1.0e9f;
    float bestS = 0.0f;
    const std::size_t n = lane.points.size();
    for (std::size_t index = 0; index < n; ++index)
    {
        const glm::vec2 a = lane.points[index];
        const glm::vec2 b = lane.points[(index + 1) % n];
        const glm::vec2 ab = b - a;
        const float lengthSquared = glm::dot(ab, ab);
        const float t = lengthSquared > 1.0e-8f ? glm::clamp(glm::dot(point - a, ab) / lengthSquared, 0.0f, 1.0f) : 0.0f;
        const float d = glm::length(point - (a + ab * t));
        if (d < bestDistance)
        {
            bestDistance = d;
            const float segment = (index + 1 < n ? lane.lengths[index + 1] : lane.total) - lane.lengths[index];
            bestS = lane.lengths[index] + segment * t;
        }
    }
    if (distance != nullptr)
        *distance = bestDistance;
    return bestS;
}

void PedestrianSystem::buildCrossingEnds()
{
    // Each end of a crossing joins the sidewalk loop nearest to it, on both
    // of its walking lines; an end on a splitter island joins nothing (the
    // other half of the crossing carries on from there).
    const std::vector<Crossing>& crossings = network_.crossings();
    ends_.assign(crossings.size() * 2, CrossingEnd {});
    partner_.resize(crossings.size());
    for (std::size_t index = 0; index < crossings.size(); ++index)
    {
        const Crossing& crossing = crossings[index];
        partner_[index] = index;
        for (std::size_t other = 0; other < crossings.size(); ++other)
        {
            if (other != index && crossing.refugeEnd >= 0 && crossings[other].junction == crossing.junction &&
                crossings[other].arm == crossing.arm)
                partner_[index] = other;
        }

        for (int end = 0; end < 2; ++end)
        {
            CrossingEnd& info = ends_[index * 2 + static_cast<std::size_t>(end)];
            if (end == crossing.refugeEnd)
            {
                info.refuge = true;
                continue;
            }
            const glm::vec2 spot = crossing.point(crossing.endAcross(end), 0.0f);
            float best = 1.0e9f;
            for (std::size_t loop = 0; loop < loopCount_; ++loop)
            {
                float distance = 0.0f;
                nearestS(lanes_[loop * 2], spot, &distance);
                if (distance < best)
                {
                    best = distance;
                    info.loop = loop;
                }
            }
            for (int side = 0; side < 2; ++side)
            {
                const std::size_t laneIndex = info.loop * 2 + static_cast<std::size_t>(side);
                info.lane[side] = laneIndex;
                info.s[side] = nearestS(lanes_[laneIndex], spot);
                lanes_[laneIndex].attachments.push_back({index, end, info.s[side]});
            }
        }
    }
    for (Lane& lane : lanes_)
    {
        std::sort(lane.attachments.begin(), lane.attachments.end(),
                  [](const Attachment& a, const Attachment& b) { return a.s < b.s; });
    }
}

// ---------------------------------------------------------------------------
// People
// ---------------------------------------------------------------------------

unsigned int PedestrianSystem::nextRandom()
{
    randomState_ = randomState_ * 1664525u + 1013904223u;
    return randomState_ >> 8;
}

float PedestrianSystem::random01()
{
    return static_cast<float>(nextRandom() % 100000u) / 100000.0f;
}

void PedestrianSystem::reset()
{
    randomState_ = seed_ * 2654435761u + 977u;
    clock_ = 0.0;
    walkers_.clear();
    looks_.clear();
    std::fill(crossingStates_.begin(), crossingStates_.end(), CrossingState {});

    static const std::array<glm::vec3, 6> skins = {{
        {0.96f, 0.80f, 0.69f}, {0.88f, 0.68f, 0.55f}, {0.78f, 0.57f, 0.42f},
        {0.64f, 0.45f, 0.31f}, {0.47f, 0.31f, 0.21f}, {0.33f, 0.22f, 0.15f}
    }};
    static const std::array<glm::vec3, 6> hairs = {{
        {0.07f, 0.05f, 0.04f}, {0.24f, 0.15f, 0.08f}, {0.48f, 0.32f, 0.17f},
        {0.82f, 0.68f, 0.42f}, {0.55f, 0.55f, 0.56f}, {0.52f, 0.20f, 0.09f}
    }};
    static const std::array<glm::vec3, 12> tops = {{
        {0.74f, 0.13f, 0.14f}, {0.14f, 0.33f, 0.68f}, {0.94f, 0.94f, 0.92f}, {0.20f, 0.52f, 0.30f},
        {0.95f, 0.74f, 0.18f}, {0.36f, 0.20f, 0.52f}, {0.10f, 0.10f, 0.12f}, {0.90f, 0.46f, 0.60f},
        {0.45f, 0.70f, 0.86f}, {0.70f, 0.40f, 0.18f}, {0.55f, 0.58f, 0.60f}, {0.12f, 0.45f, 0.48f}
    }};
    static const std::array<glm::vec3, 6> trousers = {{
        {0.10f, 0.12f, 0.24f}, {0.07f, 0.07f, 0.08f}, {0.60f, 0.52f, 0.38f},
        {0.34f, 0.35f, 0.39f}, {0.20f, 0.30f, 0.52f}, {0.28f, 0.20f, 0.14f}
    }};
    static const std::array<glm::vec3, 3> shoes = {{
        {0.06f, 0.05f, 0.05f}, {0.30f, 0.18f, 0.10f}, {0.92f, 0.92f, 0.90f}
    }};
    static const std::array<glm::vec3, 6> umbrellas = {{
        {0.10f, 0.12f, 0.18f}, {0.75f, 0.10f, 0.12f}, {0.12f, 0.40f, 0.70f},
        {0.95f, 0.80f, 0.15f}, {0.20f, 0.55f, 0.30f}, {0.60f, 0.20f, 0.60f}
    }};
    const auto pick = [this](const auto& palette) { return palette[nextRandom() % palette.size()]; };

    for (std::size_t index = 0; index < count_; ++index)
    {
        WalkerLook look;
        look.height = 1.56f + 0.34f * random01() * (0.4f + 0.6f * random01());
        look.build = 0.9f + 0.25f * random01();
        look.skin = pick(skins);
        look.hair = pick(hairs);
        look.top = pick(tops);
        look.trousers = pick(trousers);
        look.shoes = pick(shoes);
        look.umbrella = pick(umbrellas);
        look.longSleeves = random01() < 0.6f;
        look.shorts = random01() < 0.14f;
        look.bald = random01() < 0.07f;

        Walker walker;
        walker.id = index;
        // Some stroll, some hurry, and a few jog.
        const bool jogger = random01() < 0.07f;
        walker.preferredSpeed = jogger ? 2.75f + 0.45f * random01() : 1.15f + 0.45f * random01();
        if (jogger)
        {
            look.shorts = true;
            look.longSleeves = false;
        }
        walker.umbrellaDelay = random01() * 4.0f;
        walker.phase = random01();
        looks_.push_back(look);
        walkers_.push_back(walker);
    }
    placeWalkers();
    resetStats();
}

void PedestrianSystem::placeWalkers()
{
    float totalLength = 0.0f;
    for (const Lane& lane : lanes_)
        totalLength += lane.total;

    for (Walker& walker : walkers_)
    {
        for (int attempt = 0; attempt < 200; ++attempt)
        {
            // Anywhere on any sidewalk, in proportion to its length.
            float pick = random01() * totalLength;
            std::size_t laneIndex = 0;
            while (laneIndex + 1 < lanes_.size() && pick > lanes_[laneIndex].total)
            {
                pick -= lanes_[laneIndex].total;
                ++laneIndex;
            }
            const float s = pick;
            bool clear = true;
            for (const Walker& other : walkers_)
            {
                if (&other == &walker)
                    break;
                if (other.lane == laneIndex)
                {
                    const float gap = std::abs(wrap(other.s - s + 0.5f * lanes_[laneIndex].total, lanes_[laneIndex].total) -
                                               0.5f * lanes_[laneIndex].total);
                    clear = clear && gap > 3.0f;
                }
            }
            if (!clear && attempt < 199)
                continue;
            walker.lane = laneIndex;
            walker.s = s;
            walker.state = State::Lane;
            glm::vec2 direction {0.0f, 1.0f};
            walker.position = lanePoint(lanes_[laneIndex], s, &direction);
            walker.yawDegrees = headingOf(direction);
            walker.speed = walker.preferredSpeed;
            break;
        }
        walker.previousPosition = walker.position;
        walker.previousYaw = walker.yawDegrees;
        walker.previousSpeed = walker.speed;
        walker.previousPhase = walker.phase;
    }
}

void PedestrianSystem::resetStats()
{
    stats_ = PedestrianStats {};
    for (Walker& walker : walkers_)
        walker.stillSeconds = 0.0f;
}

void PedestrianSystem::openUmbrellasNow(float amount)
{
    umbrellaTarget_ = amount;
    for (Walker& walker : walkers_)
        walker.umbrella = walker.previousUmbrella = amount;
}

// ---------------------------------------------------------------------------
// Crossings
// ---------------------------------------------------------------------------

float PedestrianSystem::alongLane(std::size_t crossing, int fromEnd) const
{
    // Keep right on the band: people crossing the two ways pass each other.
    const Crossing& info = network_.crossings()[crossing];
    const glm::vec2 direction = info.across * (fromEnd == 0 ? 1.0f : -1.0f);
    return crossingLaneOffset * (glm::dot(rightOf(direction), info.along) >= 0.0f ? 1.0f : -1.0f);
}

glm::vec2 PedestrianSystem::slotPosition(std::size_t crossing, int end, int slot) const
{
    const Crossing& info = network_.crossings()[crossing];
    const float lane = alongLane(crossing, end);
    if (end == info.refugeEnd)
    {
        // On the island: one behind the other along it.
        return info.point(0.0f, lane + (lane >= 0.0f ? 1.0f : -1.0f) * static_cast<float>(slot) * slotRowSpacing);
    }
    const int row = slot / 2;
    const int column = slot % 2;
    const float away = end == 0 ? -1.0f : 1.0f;   // from the road, onto the sidewalk
    return info.point(info.endAcross(end) + away * static_cast<float>(row) * slotRowSpacing,
                      lane + (column == 0 ? -slotColumnOffset : slotColumnOffset));
}

glm::vec2 PedestrianSystem::crossingTarget(const Walker& walker) const
{
    const Crossing& info = network_.crossings()[walker.crossing];
    const int far = 1 - walker.fromEnd;
    const float lane = alongLane(walker.crossing, walker.fromEnd);
    const float column = far == info.refugeEnd ? 0.0f : (walker.slot % 2 == 0 ? -slotColumnOffset : slotColumnOffset);
    return info.point(info.endAcross(far), lane + column);
}

int PedestrianSystem::freeSlot(std::size_t crossing, int end, std::size_t except) const
{
    std::array<bool, maximumSlots> used {};
    for (const Walker& walker : walkers_)
    {
        if (walker.id == except || walker.crossing != crossing || walker.fromEnd != end)
            continue;
        if ((walker.state == State::ToKerb || walker.state == State::Wait) && walker.slot >= 0 && walker.slot < maximumSlots)
            used[static_cast<std::size_t>(walker.slot)] = true;
    }
    const int usable = usableSlots_.empty() ? maximumSlots : usableSlots_[crossing * 2 + static_cast<std::size_t>(end)];
    for (int slot = 0; slot < usable; ++slot)
    {
        if (!used[static_cast<std::size_t>(slot)])
            return slot;
    }
    return -1;
}

bool PedestrianSystem::guestOnCrossing(std::size_t crossing) const
{
    if (guests_.empty())
        return false;
    const Crossing& info = network_.crossings()[crossing];
    const OrientedBox band = makeOrientedBox(info.point(0.5f * (info.from + info.to), 0.0f),
                                             headingOf(info.along),
                                             {0.5f * (info.to - info.from) + 0.6f, info.halfWidth + 0.5f});
    for (const Guest& guest : guests_)
    {
        if (boxesOverlap(band, guest.body))
            return true;
    }
    return false;
}

bool PedestrianSystem::mayStart(std::size_t crossing, const TrafficSystem& traffic)
{
    signed char& cached = clearCache_[crossing];
    if (cached < 0)
        cached = traffic.walkAllowed(crossing) && !guestOnCrossing(crossing) && traffic.crossingClear(crossing) ? 1 : 0;
    return cached == 1;
}

void PedestrianSystem::startApproach(Walker& walker, std::size_t crossing, int end)
{
    const int slot = freeSlot(crossing, end, walker.id);
    if (slot < 0)
        return;
    walker.state = State::ToKerb;
    walker.crossing = crossing;
    walker.fromEnd = end;
    walker.slot = slot;
    walker.target = slotPosition(crossing, end, slot);
    walker.sinceCrossing = 0.0f;
}

void PedestrianSystem::arriveAcross(Walker& walker)
{
    const Crossing& info = network_.crossings()[walker.crossing];
    const int far = 1 - walker.fromEnd;
    ++stats_.crossingsMade;
    if (far == info.refugeEnd)
    {
        // Half way over a roundabout arm: wait on the island for the other half.
        const std::size_t next = partner_[walker.crossing];
        const int nextEnd = network_.crossings()[next].refugeEnd;
        const int slot = freeSlot(next, nextEnd, walker.id);
        walker.state = State::Wait;
        walker.crossing = next;
        walker.fromEnd = nextEnd;
        walker.slot = std::max(slot, 0);
        walker.target = slotPosition(next, nextEnd, walker.slot);
        walker.reaction = 0.2f + 0.5f * random01();
        return;
    }
    // Back onto a sidewalk, on one of its two lines.
    const CrossingEnd& end = ends_[walker.crossing * 2 + static_cast<std::size_t>(far)];
    const int side = static_cast<int>(nextRandom() % 2u);
    walker.state = State::FromKerb;
    walker.joinLane = end.lane[side];
    walker.joinS = end.s[side];
    walker.target = lanePoint(lanes_[walker.joinLane], walker.joinS);
}

void PedestrianSystem::setMotion(Walker& walker, Motion motion)
{
    if (walker.motion == motion)
        return;
    walker.previousMotion = walker.motion;
    walker.motion = motion;
    walker.fade = 0.0f;
    walker.stateSeconds = 0.0f;
}

bool PedestrianSystem::nearCrossing(const Walker& walker) const
{
    return walker.state == State::ToKerb || walker.state == State::Wait || walker.state == State::Cross ||
           walker.state == State::FromKerb;
}

// ---------------------------------------------------------------------------
// The step
// ---------------------------------------------------------------------------

void PedestrianSystem::update(float dt, TrafficSystem& traffic)
{
    dt = glm::clamp(dt, 0.0f, 0.05f);
    if (dt <= 0.0f)
        return;
    clock_ += dt;
    std::fill(clearCache_.begin(), clearCache_.end(), static_cast<signed char>(-1));

    for (Walker& walker : walkers_)
    {
        walker.previousPosition = walker.position;
        walker.previousYaw = walker.yawDegrees;
        walker.previousSpeed = walker.speed;
        walker.previousPhase = walker.phase;
        walker.previousFade = walker.fade;
        walker.previousStateSeconds = walker.stateSeconds;
        walker.previousUmbrella = walker.umbrella;
    }

    // Who walks on each line, in order along it.
    for (std::vector<std::size_t>& order : laneOrder_)
        order.clear();
    for (std::size_t index = 0; index < walkers_.size(); ++index)
    {
        const Walker& walker = walkers_[index];
        if (walker.state == State::Lane || walker.state == State::Idle)
            laneOrder_[walker.lane].push_back(index);
    }
    for (std::vector<std::size_t>& order : laneOrder_)
    {
        std::sort(order.begin(), order.end(), [this](std::size_t a, std::size_t b) { return walkers_[a].s < walkers_[b].s; });
    }
    // The gap to the next person ahead, and from the one behind, on a line.
    const auto gaps = [this](std::size_t index, float& ahead, float& behind)
    {
        const Walker& walker = walkers_[index];
        const std::vector<std::size_t>& order = laneOrder_[walker.lane];
        ahead = behind = 1.0e9f;
        if (order.size() < 2)
            return;
        const float total = lanes_[walker.lane].total;
        const auto at = std::find(order.begin(), order.end(), index);
        const std::size_t position = static_cast<std::size_t>(at - order.begin());
        const Walker& next = walkers_[order[(position + 1) % order.size()]];
        const Walker& previous = walkers_[order[(position + order.size() - 1) % order.size()]];
        ahead = wrap(next.s - walker.s, total);
        behind = wrap(walker.s - previous.s, total);
    };
    // Whether a guest (the player) stands just ahead.
    const auto guestAhead = [this](glm::vec2 position, glm::vec2 direction)
    {
        const glm::vec2 probe = position + direction * 0.8f;
        for (const Guest& guest : guests_)
        {
            if (glm::length(probe - closestPointOnBox(guest.body, probe)) < 0.55f)
                return true;
        }
        return false;
    };
    // Straight towards a point, easing in; true on arrival.
    const auto moveToward = [dt](Walker& walker, glm::vec2 target, float wanted)
    {
        const glm::vec2 to = target - walker.position;
        const float distance = glm::length(to);
        if (distance < 0.02f)
        {
            walker.position = target;
            walker.speed = approach(walker.speed, 0.0f, 1.5f, 3.0f, dt);
            return true;
        }
        walker.speed = approach(walker.speed, std::min(wanted, 1.4f * distance + 0.25f), 1.5f, 3.0f, dt);
        const float step = std::min(distance, walker.speed * dt);
        walker.position += to / distance * step;
        return distance - step < 0.02f;
    };

    const std::vector<Crossing>& crossings = network_.crossings();
    for (std::size_t index = 0; index < walkers_.size(); ++index)
    {
        Walker& walker = walkers_[index];
        glm::vec2 facing {0.0f};
        switch (walker.state)
        {
        case State::Lane:
        {
            Lane& lane = lanes_[walker.lane];
            float ahead = 0.0f;
            float behind = 0.0f;
            gaps(index, ahead, behind);
            glm::vec2 direction {0.0f, 1.0f};
            lanePoint(lane, walker.s, &direction);
            float wanted = walker.preferredSpeed;
            if (ahead < 4.0f)
                wanted = std::min(wanted, std::max(0.0f, (ahead - followSpacing) * 1.6f));
            if (guestAhead(walker.position, direction))
                wanted = 0.0f;
            walker.speed = approach(walker.speed, wanted, 1.2f, 3.0f, dt);
            const float travel = walker.speed * dt;
            const float before = walker.s;
            walker.s = wrap(walker.s + travel, lane.total);
            walker.sinceCrossing += travel;

            // Passing the end of a crossing: over the road, now and then.
            for (const Attachment& attachment : lane.attachments)
            {
                const bool passed = before <= walker.s ? attachment.s > before && attachment.s <= walker.s
                                                       : attachment.s > before || attachment.s <= walker.s;
                if (!passed)
                    continue;
                const float roll = random01();
                if (walker.sinceCrossing > 25.0f && roll < crossChance)
                {
                    startApproach(walker, attachment.crossing, attachment.end);
                    break;
                }
                if (roll > 1.0f - turnBackChance)
                {
                    // Turn round: over to the other line of this sidewalk.
                    const CrossingEnd& end = ends_[attachment.crossing * 2 + static_cast<std::size_t>(attachment.end)];
                    const int otherSide = end.lane[0] == walker.lane ? 1 : 0;
                    walker.state = State::FromKerb;
                    walker.joinLane = end.lane[otherSide];
                    walker.joinS = end.s[otherSide];
                    walker.target = lanePoint(lanes_[walker.joinLane], walker.joinS);
                    break;
                }
            }
            if (walker.state == State::Lane)
            {
                walker.position = lanePoint(lane, walker.s, &direction);
                facing = direction;
                // Now and then, a pause (nobody close behind).
                if (behind > 6.0f && walker.speed > 0.5f && random01() < dt / idleEverySeconds)
                {
                    walker.state = State::Idle;
                    walker.idleLeft = 2.0f + 4.0f * random01();
                }
            }
            break;
        }
        case State::Idle:
            walker.speed = approach(walker.speed, 0.0f, 1.2f, 3.0f, dt);
            walker.s = wrap(walker.s + walker.speed * dt, lanes_[walker.lane].total);
            walker.position = lanePoint(lanes_[walker.lane], walker.s);
            walker.idleLeft -= dt;
            if (walker.idleLeft <= 0.0f)
                walker.state = State::Lane;
            break;
        case State::ToKerb:
            if (moveToward(walker, walker.target, walker.preferredSpeed))
            {
                walker.state = State::Wait;
                walker.reaction = 0.25f + 0.7f * random01();
            }
            facing = walker.target - walker.previousPosition;
            break;
        case State::Wait:
        {
            // Shuffle into place, face the road, and go once allowed.
            walker.target = slotPosition(walker.crossing, walker.fromEnd, walker.slot);
            moveToward(walker, walker.target, 0.8f);
            const Crossing& info = crossings[walker.crossing];
            facing = info.across * (walker.fromEnd == 0 ? 1.0f : -1.0f);
            if (mayStart(walker.crossing, traffic))
            {
                walker.reaction -= dt;
                if (walker.reaction <= 0.0f)
                {
                    if (!traffic.walkAllowed(walker.crossing))
                        ++stats_.startsAgainstLights;
                    walker.state = State::Cross;
                    walker.target = crossingTarget(walker);
                }
            }
            break;
        }
        case State::Cross:
        {
            // Never stopping; a little quicker than on the sidewalk, and
            // hurrying when the lights start to flash.
            const bool flashing = traffic.walkLight(walker.crossing) == WalkLight::Flashing;
            const float wanted = walker.preferredSpeed * (flashing ? 1.35f : 1.1f);
            walker.speed = approach(walker.speed, wanted, 1.5f, 3.0f, dt);
            const glm::vec2 to = walker.target - walker.position;
            const float distance = glm::length(to);
            const float step = std::min(distance, walker.speed * dt);
            if (distance > 1.0e-4f)
                walker.position += to / distance * step;
            facing = to;
            if (distance - step < 0.02f)
                arriveAcross(walker);
            break;
        }
        case State::FromKerb:
        {
            const bool arrived = moveToward(walker, walker.target, walker.preferredSpeed);
            facing = walker.target - walker.previousPosition;
            if (!arrived)
                break;
            // Step onto the line only where nobody is right there.
            bool free = true;
            const float total = lanes_[walker.joinLane].total;
            for (const std::size_t other : laneOrder_[walker.joinLane])
            {
                const float gap = std::abs(wrap(walkers_[other].s - walker.joinS + 0.5f * total, total) - 0.5f * total);
                free = free && gap > 1.1f;
            }
            if (free)
            {
                walker.state = State::Lane;
                walker.lane = walker.joinLane;
                walker.s = walker.joinS;
                std::vector<std::size_t>& order = laneOrder_[walker.lane];
                order.insert(std::lower_bound(order.begin(), order.end(), walker.s,
                                              [this](std::size_t other, float s) { return walkers_[other].s < s; }),
                             index);
            }
            break;
        }
        }

        // Turn towards where they are going (or facing the road, waiting).
        if (glm::length(facing) > 1.0e-4f && (walker.speed > 0.05f || walker.state == State::Wait))
        {
            const float wantedYaw = headingOf(glm::normalize(facing));
            const float difference = std::fmod(wantedYaw - walker.yawDegrees + 540.0f, 360.0f) - 180.0f;
            const float turn = 400.0f * dt;
            walker.yawDegrees += glm::clamp(difference, -turn, turn);
        }

        // Animation: the graph state, the cross-fade, and the gait phase from
        // the distance actually walked.
        const float moved = glm::length(walker.position - walker.previousPosition);
        const Motion motion = walker.state == State::Idle ? Motion::Idle
                            : walker.state == State::Wait ? (moved / dt > 0.25f ? Motion::Walk : Motion::Wait)
                            : walker.state == State::Cross ? Motion::Cross : Motion::Walk;
        setMotion(walker, motion);
        walker.fade = std::min(1.0f, walker.fade + dt / motionFadeSeconds);
        walker.stateSeconds += dt;
        walker.phase = std::fmod(walker.phase + moved / Mannequin::stride(moved / dt, looks_[index].height), 1.0f);

        // Feet: lock the ones coming down where they land.
        WalkerMotion now;
        now.position = {walker.position.x, 0.0f, walker.position.y};
        now.yawDegrees = walker.yawDegrees;
        now.speed = moved / dt;
        now.phase = walker.phase;
        now.motion = walker.motion;
        now.previousMotion = walker.previousMotion;
        now.fade = walker.fade;
        now.stateSeconds = walker.stateSeconds;
        now.clock = static_cast<float>(clock_) + 1.7f * static_cast<float>(walker.id);
        now.lock[0] = walker.lock[0];
        now.lock[1] = walker.lock[1];
        Mannequin::plantFeet(looks_[index], now);
        walker.lock[0] = now.lock[0];
        walker.lock[1] = now.lock[1];

        // Standing still against their will (a pause is not that).
        if (moved / dt < 0.05f && walker.state != State::Idle)
        {
            walker.stillSeconds += dt;
            if (walker.stillSeconds > stats_.longestWait)
            {
                stats_.longestWait = walker.stillSeconds;
                if (walker.stillSeconds > 45.0f)
                {
                    static const char* const states[] = {"lane", "idle", "to kerb", "waiting", "crossing", "from kerb"};
                    const Crossing& crossing = crossings[walker.crossing];
                    char line[200];
                    std::snprintf(line, sizeof(line), "person %zu, %s, crossing %zu at %s %s, at (%.1f, %.1f), %.0f s at %.0f s",
                                  walker.id, states[static_cast<int>(walker.state)], walker.crossing,
                                  network_.junctions()[crossing.junction].name.c_str(), armName(crossing.arm),
                                  walker.position.x, walker.position.y, walker.stillSeconds, clock_);
                    stats_.longestWaitAt = line;
                }
            }
        }
        else
        {
            walker.stillSeconds = 0.0f;
        }

        // Umbrellas: most people carry one, and put it up a moment apart.
        const float wantUmbrella = (walker.id % 5u != 4u && umbrellaTarget_ > 0.0f &&
                                    clock_ > walker.umbrellaDelay) ? umbrellaTarget_ : 0.0f;
        walker.umbrella = approach(walker.umbrella, wantUmbrella, 1.2f, 1.2f, dt);
    }

    // Tell the traffic who waits at, and who is on, each crossing.
    std::fill(crossingStates_.begin(), crossingStates_.end(), CrossingState {});
    for (const Walker& walker : walkers_)
    {
        if (walker.state == State::Wait ||
            (walker.state == State::ToKerb && glm::length(walker.target - walker.position) < 3.0f))
        {
            ++crossingStates_[walker.crossing].waiting;
        }
        else if (walker.state == State::Cross)
        {
            CrossingState& state = crossingStates_[walker.crossing];
            ++state.onBand;
            state.clearSeconds = std::max(state.clearSeconds,
                                          glm::length(walker.target - walker.position) / std::max(walker.speed, 0.8f));
        }
    }
    traffic.setCrossingStates(crossingStates_);

    measure(traffic);
}

void PedestrianSystem::measure(const TrafficSystem& traffic)
{
    // Every step, every person against every vehicle body.
    traffic.bodies(vehicleBodies_);
    std::size_t touching = 0;
    static const char* const names[] = {"on a sidewalk", "standing about", "going to the kerb", "waiting", "crossing", "leaving the kerb"};
    for (const Walker& walker : walkers_)
    {
        std::size_t bodyIndex = 0;
        for (const Vehicle& vehicle : traffic.vehicles())
        {
            if (!vehicle.active)
                continue;
            const OrientedBox& body = vehicleBodies_[bodyIndex++];
            const glm::vec2 offset = body.centre - walker.position;
            if (glm::dot(offset, offset) > 64.0f)
                continue;
            const float gap = glm::length(walker.position - closestPointOnBox(body, walker.position)) - bodyRadius;
            stats_.closestVehicleGap = std::min(stats_.closestVehicleGap, gap);
            if (gap >= 0.0f)
                continue;
            ++touching;
            if (stats_.firstTouch.empty())
            {
                char line[240];
                const Crossing& crossing = network_.crossings()[walker.crossing];
                std::snprintf(line, sizeof(line), "person %zu %s (crossing at %s %s) at (%.2f, %.2f) touched by %s %zu at (%.2f, %.2f), %.2f m in, %s, %.1f m/s, %.1f s",
                              walker.id, names[static_cast<int>(walker.state)],
                              network_.junctions()[crossing.junction].name.c_str(), armName(crossing.arm),
                              walker.position.x, walker.position.y, vehicleKindName(vehicle.kind), vehicle.id,
                              vehicle.position.x, vehicle.position.z, -gap, vehicle.committed ? "committed" : "waiting",
                              vehicle.currentSpeed, clock_);
                stats_.firstTouch = line;
            }
        }
    }
    stats_.overlapPairsNow = touching;
    if (touching > 0)
        ++stats_.overlapSteps;
}

// ---------------------------------------------------------------------------
// Outputs
// ---------------------------------------------------------------------------

void PedestrianSystem::interpolate(float alpha, std::vector<PedestrianPose>& poses) const
{
    alpha = glm::clamp(alpha, 0.0f, 1.0f);
    poses.resize(walkers_.size());
    const std::vector<Crossing>& crossings = network_.crossings();
    for (std::size_t index = 0; index < walkers_.size(); ++index)
    {
        const Walker& walker = walkers_[index];
        PedestrianPose& pose = poses[index];
        pose.id = walker.id;
        pose.height = looks_[index].height;
        pose.nearRoad = nearCrossing(walker);
        if (pose.nearRoad)
        {
            const Crossing& crossing = crossings[walker.crossing];
            pose.stripOrigin = crossing.origin;
            pose.stripAcross = crossing.across;
            pose.roadFrom = crossing.from;
            pose.roadTo = crossing.to;
        }

        WalkerMotion& motion = pose.motion;
        const glm::vec2 position = glm::mix(walker.previousPosition, walker.position, alpha);
        motion.position = {position.x, pose.groundAt(position), position.y};
        motion.yawDegrees = blendAngle(walker.previousYaw, walker.yawDegrees, alpha);
        motion.speed = glm::mix(walker.previousSpeed, walker.speed, alpha);
        float phaseStep = walker.phase - walker.previousPhase;
        if (phaseStep < -0.5f)
            phaseStep += 1.0f;
        motion.phase = std::fmod(walker.previousPhase + phaseStep * alpha + 1.0f, 1.0f);
        motion.motion = walker.motion;
        motion.previousMotion = walker.previousMotion;
        motion.fade = walker.fade < walker.previousFade ? walker.fade : glm::mix(walker.previousFade, walker.fade, alpha);
        motion.stateSeconds = walker.stateSeconds < walker.previousStateSeconds
            ? walker.stateSeconds : glm::mix(walker.previousStateSeconds, walker.stateSeconds, alpha);
        motion.clock = static_cast<float>(clock_) + 1.7f * static_cast<float>(walker.id);
        motion.umbrella = glm::mix(walker.previousUmbrella, walker.umbrella, alpha);
        motion.lock[0] = walker.lock[0];
        motion.lock[1] = walker.lock[1];
    }
}

std::size_t PedestrianSystem::waitingCount() const
{
    return static_cast<std::size_t>(std::count_if(walkers_.begin(), walkers_.end(),
        [](const Walker& walker) { return walker.state == State::Wait; }));
}

std::size_t PedestrianSystem::crossingCount() const
{
    return static_cast<std::size_t>(std::count_if(walkers_.begin(), walkers_.end(),
        [](const Walker& walker) { return walker.state == State::Cross; }));
}

void PedestrianSystem::bodies(std::vector<OrientedBox>& out) const
{
    for (const Walker& walker : walkers_)
        out.push_back(makeOrientedBox(walker.position, walker.yawDegrees, {bodyRadius, bodyRadius}));
}

std::string PedestrianSystem::describe(const TrafficSystem& traffic) const
{
    static const char* const names[] = {"lane", "idle", "to kerb", "waiting", "crossing", "from kerb"};
    static const char* const lights[] = {"zebra", "WALK", "flashing", "DON'T WALK"};
    std::string text;
    const std::vector<Crossing>& crossings = network_.crossings();
    for (const Walker& walker : walkers_)
    {
        char line[320];
        const bool atCrossing = nearCrossing(walker);
        std::snprintf(line, sizeof(line), "  person %2zu %-9s at (%7.1f, %7.1f) v %.2f still %4.0f s",
                      walker.id, names[static_cast<int>(walker.state)], walker.position.x, walker.position.y,
                      walker.speed, walker.stillSeconds);
        text += line;
        if (atCrossing)
        {
            const Crossing& crossing = crossings[walker.crossing];
            const char* reason = "";
            const Vehicle* blocker = traffic.crossingBlocker(walker.crossing, &reason);
            const CrossingState& state = traffic.crossingStates()[walker.crossing];
            std::snprintf(line, sizeof(line), "  crossing %zu at %s %s from end %d: %s, %d waiting, %d on it%s%s%s",
                          walker.crossing, network_.junctions()[crossing.junction].name.c_str(), armName(crossing.arm),
                          walker.fromEnd, lights[static_cast<int>(traffic.walkLight(walker.crossing))], state.waiting,
                          state.onBand, blocker != nullptr ? ", blocked by " : "",
                          blocker != nullptr ? (std::string(vehicleKindName(blocker->kind)) + " " + std::to_string(blocker->id)).c_str() : "",
                          blocker != nullptr ? (std::string(" (") + reason + ")").c_str() : "");
            text += line;
        }
        text += "\n";
    }
    return text;
}

// ---------------------------------------------------------------------------
// --self-test
// ---------------------------------------------------------------------------

bool PedestrianSystem::selfTest(std::string& report) const
{
    std::size_t checks = 0;
    std::size_t failures = 0;
    const auto fail = [&report, &failures](const std::string& message)
    {
        if (++failures <= 20)
            report += "FAIL: " + message + "\n";
    };
    const auto clearPath = [this](glm::vec2 a, glm::vec2 b)
    {
        const int steps = std::max(1, static_cast<int>(glm::length(b - a) / 0.1f));
        for (int step = 0; step <= steps; ++step)
        {
            if (!pointClear(glm::mix(a, b, static_cast<float>(step) / static_cast<float>(steps)), requiredClearance))
                return false;
        }
        return true;
    };

    // Every walking line clear of every post, trunk and wall, on the
    // sidewalk or the grass behind it, and never on the road.
    float walked = 0.0f;
    for (std::size_t laneIndex = 0; laneIndex < lanes_.size(); ++laneIndex)
    {
        const Lane& lane = lanes_[laneIndex];
        walked += lane.total;
        std::size_t blocked = 0;
        std::size_t offRoad = 0;
        glm::vec2 firstBlocked {0.0f};
        for (float s = 0.0f; s < lane.total; s += 0.1f)
        {
            const glm::vec2 point = lanePoint(lane, s);
            if (!pointClear(point, requiredClearance))
            {
                if (blocked++ == 0)
                    firstBlocked = point;
            }
            if (world_.surfaceHeight(point) != RoadNetwork::kerbTopY)
                ++offRoad;
        }
        if (blocked > 0)
        {
            char line[160];
            std::snprintf(line, sizeof(line), "walking line %zu passes within %.1f m of something solid at %zu places, first (%.1f, %.1f)",
                          laneIndex, requiredClearance, blocked, firstBlocked.x, firstBlocked.y);
            fail(line);
        }
        if (offRoad > 0)
            fail("walking line " + std::to_string(laneIndex) + " runs onto the road");
        checks += 2;
    }

    // Every crossing: its ends joined to a sidewalk close by, with a clear
    // way from the line to the kerb, and room for at least four to wait at
    // each end, on the sidewalk (or the island) and clear of everything.
    const std::vector<Crossing>& crossings = network_.crossings();
    std::size_t slotsInAll = 0;
    for (std::size_t crossing = 0; crossing < crossings.size(); ++crossing)
    {
        const Crossing& info = crossings[crossing];
        const std::string name = network_.junctions()[info.junction].name + " " + armName(info.arm) +
                                 (info.refugeEnd >= 0 ? (info.refugeEnd == 1 ? " (first half)" : " (second half)") : "");
        for (int end = 0; end < 2; ++end)
        {
            const CrossingEnd& info2 = ends_[crossing * 2 + static_cast<std::size_t>(end)];
            const int usable = usableSlots_[crossing * 2 + static_cast<std::size_t>(end)];
            if (usable < 4)
                fail(name + ": only " + std::to_string(usable) + " waiting places fit at end " + std::to_string(end));
            ++checks;
            slotsInAll += static_cast<std::size_t>(usable);
            if (info2.refuge)
                continue;
            for (int side = 0; side < 2; ++side)
            {
                const glm::vec2 join = lanePoint(lanes_[info2.lane[side]], info2.s[side]);
                const glm::vec2 kerb = slotPosition(crossing, end, 0);
                if (glm::length(join - kerb) > 5.0f)
                    fail(name + ": end " + std::to_string(end) + " is " + std::to_string(glm::length(join - kerb)) +
                         " m from its sidewalk");
                if (!clearPath(join, kerb))
                    fail(name + ": the way from the sidewalk to the kerb at end " + std::to_string(end) + " is blocked");
                checks += 2;
            }
        }
        // The band itself: the road between the two kerbs, nothing on it.
        const glm::vec2 a = info.point(info.endAcross(0), 0.0f);
        const glm::vec2 b = info.point(info.endAcross(1), 0.0f);
        if (!clearPath(a, b))
            fail(name + ": something stands on the crossing");
        ++checks;
    }

    // Every sidewalk reachable from every other over the crossings.
    std::vector<std::size_t> group(loopCount_);
    for (std::size_t loop = 0; loop < loopCount_; ++loop)
        group[loop] = loop;
    const auto find = [&group](std::size_t loop)
    {
        while (group[loop] != loop)
            loop = group[loop] = group[group[loop]];
        return loop;
    };
    for (std::size_t crossing = 0; crossing < crossings.size(); ++crossing)
    {
        const std::size_t other = partner_[crossing];
        std::array<std::size_t, 2> loops {none, none};
        for (int end = 0; end < 2; ++end)
        {
            const CrossingEnd& a = ends_[crossing * 2 + static_cast<std::size_t>(end)];
            if (!a.refuge)
                loops[static_cast<std::size_t>(end)] = a.loop;
        }
        // A roundabout's two halves join the sidewalks either side of the arm.
        if (other != crossing)
        {
            for (int end = 0; end < 2; ++end)
            {
                const CrossingEnd& b = ends_[other * 2 + static_cast<std::size_t>(end)];
                if (!b.refuge)
                    loops[loops[0] == none ? 0 : 1] = b.loop;
            }
        }
        if (loops[0] != none && loops[1] != none)
            group[find(loops[0])] = find(loops[1]);
    }
    std::size_t groups = 0;
    for (std::size_t loop = 0; loop < loopCount_; ++loop)
        groups += find(loop) == loop ? 1 : 0;
    if (groups != 1)
        fail("the sidewalks fall into " + std::to_string(groups) + " parts that no crossing joins");
    ++checks;

    if (failures > 20)
        report += "... and " + std::to_string(failures - 20) + " more failures\n";
    if (failures == 0)
    {
        char line[240];
        std::snprintf(line, sizeof(line),
                      "All %zu sidewalk checks passed (%zu walking lines, %.0f m in all, round %zu sidewalks; %zu crossings "
                      "with %zu waiting places; every sidewalk reachable).\n",
                      checks, lanes_.size(), walked, loopCount_, crossings.size(), slotsInAll);
        report += line;
    }
    return failures == 0;
}
