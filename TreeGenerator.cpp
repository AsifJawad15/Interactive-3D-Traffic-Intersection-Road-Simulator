#include "TreeGenerator.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <random>

namespace
{
    constexpr float pi = 3.14159265358979f;
    const glm::vec3 up {0.0f, 1.0f, 0.0f};

    // The four quarters of the leaf atlas, as (u0, v0, u1, v1), inset a few
    // texels so a card never samples its neighbour.
    constexpr float inset = 0.006f;
    const glm::vec4 broadleafA {0.0f + inset, 0.0f + inset, 0.5f - inset, 0.5f - inset};
    const glm::vec4 broadleafB {0.0f + inset, 0.5f + inset, 0.5f - inset, 1.0f - inset};
    const glm::vec4 coniferSpray {0.5f + inset, 0.0f + inset, 1.0f - inset, 0.5f - inset};
    const glm::vec4 palmFrond {0.5f + inset, 0.5f + inset, 1.0f - inset, 1.0f - inset};

    class Random
    {
    public:
        explicit Random(unsigned seed) : engine_(seed) {}
        float uniform(float low, float high) { return std::uniform_real_distribution<float>(low, high)(engine_); }

    private:
        std::mt19937 engine_;
    };

    // A point on a Bezier curve in 3D, by de Casteljau's construction: the
    // same curve as the Bernstein form of Lab 5, evaluated by repeated
    // linear interpolation.
    glm::vec3 bezier3(float t, std::vector<glm::vec3> points)
    {
        for (std::size_t level = points.size() - 1; level > 0; --level)
        {
            for (std::size_t index = 0; index < level; ++index)
                points[index] = glm::mix(points[index], points[index + 1], t);
        }
        return points.front();
    }

    // How far a point of a tree of height `height` sways: nothing at the
    // foot, most at the top and at the ends of long branches.
    float swayWeight(const glm::vec3& point, float height)
    {
        const float rise = std::max(point.y, 0.0f) / height;
        return glm::clamp(0.9f * rise * rise + 0.07f * glm::length(glm::vec2{point.x, point.z}), 0.0f, 1.6f);
    }

    // A tube swept along a Bezier curve, its radius tapering from r0 to r1.
    // The ring at each sample is carried along the curve by parallel
    // transport, so the tube does not twist.
    void addTube(MeshData& mesh, const std::vector<glm::vec3>& control, float r0, float r1, int segments, int sides,
                 const glm::vec3& color, float height, bool banded = false)
    {
        const auto base = static_cast<unsigned int>(mesh.vertices.size());
        glm::vec3 previous = bezier3(0.0f, control);
        glm::vec3 tangent = glm::normalize(bezier3(0.02f, control) - previous);
        glm::vec3 normal = std::abs(tangent.y) < 0.9f ? glm::normalize(glm::cross(tangent, up)) : glm::vec3{1.0f, 0.0f, 0.0f};
        float travelled = 0.0f;
        for (int step = 0; step <= segments; ++step)
        {
            const float t = static_cast<float>(step) / static_cast<float>(segments);
            const glm::vec3 centre = bezier3(t, control);
            const glm::vec3 ahead = bezier3(std::min(t + 0.02f, 1.0f), control);
            const glm::vec3 behind = bezier3(std::max(t - 0.02f, 0.0f), control);
            tangent = glm::normalize(ahead - behind);
            normal = glm::normalize(normal - glm::dot(normal, tangent) * tangent);
            const glm::vec3 binormal = glm::cross(tangent, normal);
            travelled += glm::length(centre - previous);
            previous = centre;
            const float radius = glm::mix(r0, r1, t);
            // A palm's trunk is ringed where old fronds fell.
            const float shade = banded ? (step % 2 == 0 ? 0.82f : 1.0f) : 1.0f;
            for (int side = 0; side <= sides; ++side)
            {
                const float angle = 2.0f * pi * static_cast<float>(side) / static_cast<float>(sides);
                const glm::vec3 out = std::cos(angle) * normal + std::sin(angle) * binormal;
                const glm::vec3 position = centre + out * radius;
                mesh.vertices.push_back({position, out, {static_cast<float>(side) / static_cast<float>(sides), travelled},
                                         glm::vec4(color * shade, swayWeight(position, height))});
            }
        }
        const auto ring = static_cast<unsigned int>(sides + 1);
        for (unsigned int step = 0; step < static_cast<unsigned int>(segments); ++step)
        {
            for (unsigned int side = 0; side < static_cast<unsigned int>(sides); ++side)
            {
                const unsigned int a = base + step * ring + side;
                const unsigned int b = a + ring;
                mesh.indices.insert(mesh.indices.end(), {a, b, a + 1, a + 1, b, b + 1});
            }
        }
    }

    // A leaf card: a quad from centre - u - v to centre + u + v, mapped onto
    // one quarter of the atlas. Its normals lean outward from `crown`.
    void addCard(MeshData& mesh, const glm::vec3& centre, const glm::vec3& u, const glm::vec3& v, const glm::vec4& region,
                 const glm::vec3& crown, const glm::vec3& tint, float height, float bend = 0.68f)
    {
        glm::vec3 face = glm::normalize(glm::cross(u, v));
        if (glm::dot(face, centre - crown) < 0.0f)
            face = -face;
        const glm::vec3 corners[4] = {centre - u - v, centre + u - v, centre + u + v, centre - u + v};
        const glm::vec2 uvs[4] = {{region.x, region.y}, {region.z, region.y}, {region.z, region.w}, {region.x, region.w}};
        const auto base = static_cast<unsigned int>(mesh.vertices.size());
        for (int corner = 0; corner < 4; ++corner)
        {
            glm::vec3 outward = corners[corner] - crown;
            outward = glm::length(outward) > 1.0e-3f ? glm::normalize(outward) : face;
            const glm::vec3 normal = glm::normalize((1.0f - bend) * face + bend * outward);
            // Leaves flutter a little even low down: a small extra sway.
            mesh.vertices.push_back({corners[corner], normal, uvs[corner],
                                     glm::vec4(tint, swayWeight(corners[corner], height) + 0.12f)});
        }
        mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }

    glm::vec3 anyPerpendicular(const glm::vec3& direction)
    {
        const glm::vec3 other = std::abs(direction.y) < 0.9f ? up : glm::vec3{1.0f, 0.0f, 0.0f};
        return glm::normalize(glm::cross(direction, other));
    }

    glm::vec3 rotateAround(const glm::vec3& vector, const glm::vec3& axis, float angle)
    {
        // Rodrigues' rotation formula.
        return vector * std::cos(angle) + glm::cross(axis, vector) * std::sin(angle) +
               axis * glm::dot(axis, vector) * (1.0f - std::cos(angle));
    }

    TreeModel makeBroadleaf(int variant)
    {
        Random random(1000u + static_cast<unsigned>(variant) * 77u);
        TreeModel tree;
        tree.height = 7.2f;
        const glm::vec3 barkColor {0.40f, 0.30f, 0.22f};
        const glm::vec4 region = variant == 1 ? broadleafB : broadleafA;

        // The trunk, with a gentle kink, up to where it forks.
        const float fork = random.uniform(2.6f, 3.1f);
        const glm::vec3 lean {random.uniform(-0.25f, 0.25f), 0.0f, random.uniform(-0.25f, 0.25f)};
        const std::vector<glm::vec3> trunk = {
            {0.0f, 0.0f, 0.0f}, lean * 0.4f + glm::vec3{0.0f, fork * 0.35f, 0.0f},
            -lean * 0.3f + glm::vec3{0.0f, fork * 0.7f, 0.0f}, lean + glm::vec3{0.0f, fork, 0.0f}};
        addTube(tree.bark, trunk, 0.27f, 0.15f, 8, 10, barkColor, tree.height);
        // The leader carries on up through the middle of the crown.
        const glm::vec3 top = trunk.back();
        const std::vector<glm::vec3> leader = {top, top + glm::vec3{0.1f, 1.2f, -0.05f}, top + glm::vec3{-0.15f, 2.6f, 0.1f}};
        addTube(tree.bark, leader, 0.15f, 0.05f, 5, 7, barkColor, tree.height);

        struct Placement
        {
            glm::vec3 centre;
            glm::vec3 along;   // the twig's direction: the card's picture points this way
            float size;
        };
        std::vector<Placement> cards;

        const int branches = 6 + variant % 2;
        for (int branch = 0; branch < branches; ++branch)
        {
            const float azimuth = 2.0f * pi * static_cast<float>(branch) / static_cast<float>(branches) + random.uniform(-0.3f, 0.3f);
            const float elevation = random.uniform(0.5f, 0.95f);
            const float length = random.uniform(2.1f, 2.9f);
            const glm::vec3 flat {std::cos(azimuth), 0.0f, std::sin(azimuth)};
            const glm::vec3 start = bezier3(random.uniform(0.72f, 1.0f), trunk) + glm::vec3{0.0f, random.uniform(0.0f, 0.9f), 0.0f};
            const glm::vec3 direction = glm::normalize(flat * std::cos(elevation) + up * std::sin(elevation));
            const std::vector<glm::vec3> limb = {
                start, start + direction * (0.45f * length),
                start + flat * (0.8f * length) + up * (0.55f * length * std::sin(elevation) + 0.2f),
                start + flat * length + up * (0.62f * length * std::sin(elevation) + 0.1f)};
            addTube(tree.bark, limb, 0.10f, 0.035f, 6, 6, barkColor, tree.height);

            // Twigs off the branch, each ending in a cluster of leaf cards.
            for (float t : {0.35f, 0.55f, 0.75f, 0.95f})
            {
                const glm::vec3 from = bezier3(t, limb);
                const glm::vec3 tangent = glm::normalize(bezier3(std::min(t + 0.05f, 1.0f), limb) - bezier3(std::max(t - 0.05f, 0.0f), limb));
                const glm::vec3 wander {random.uniform(-0.8f, 0.8f), random.uniform(-0.1f, 0.7f), random.uniform(-0.8f, 0.8f)};
                const glm::vec3 twigDirection = glm::normalize(tangent + wander);
                const float twigLength = random.uniform(0.8f, 1.3f);
                const glm::vec3 end = from + twigDirection * twigLength;
                addTube(tree.bark, {from, from + twigDirection * (0.5f * twigLength) + up * 0.1f, end}, 0.03f, 0.012f, 2, 3,
                        barkColor, tree.height);
                for (int leaf = 0; leaf < 4; ++leaf)
                {
                    const glm::vec3 jitter {random.uniform(-0.45f, 0.45f), random.uniform(-0.3f, 0.45f), random.uniform(-0.45f, 0.45f)};
                    cards.push_back({end + jitter, glm::normalize(twigDirection + 0.6f * jitter), random.uniform(1.1f, 1.45f)});
                }
            }
            cards.push_back({limb.back() + up * 0.3f, direction, 1.4f});
        }
        const glm::vec3 crest = leader.back();
        for (int leaf = 0; leaf < 6; ++leaf)
        {
            const float angle = 2.0f * pi * static_cast<float>(leaf) / 6.0f;
            const glm::vec3 out {std::cos(angle), 0.6f, std::sin(angle)};
            cards.push_back({crest + out * 0.7f, glm::normalize(out), 1.4f});
        }

        // The middle of the crown, which the cards' normals lean away from.
        glm::vec3 crown {0.0f};
        for (const Placement& card : cards)
            crown += card.centre;
        crown /= static_cast<float>(cards.size());
        const float variantTint = variant == 2 ? 0.9f : 1.0f;
        for (const Placement& card : cards)
        {
            // The picture's twig base points back along the twig.
            const glm::vec3 v = card.along * (0.5f * card.size);
            const glm::vec3 side = rotateAround(anyPerpendicular(card.along), card.along, random.uniform(0.0f, pi));
            const glm::vec3 u = side * (0.5f * card.size);
            const float shade = random.uniform(0.88f, 1.08f);
            const glm::vec3 tint = glm::vec3{shade * variantTint, shade, shade * random.uniform(0.9f, 1.0f)};
            addCard(tree.leaves, card.centre + v * 0.6f, u, v, region, crown, tint, tree.height);
            tree.crownRadius = std::max(tree.crownRadius, glm::length(glm::vec2{card.centre.x, card.centre.z}) + 0.6f);
            tree.height = std::max(tree.height, card.centre.y + 0.7f);
        }
        return tree;
    }

    TreeModel makeConifer(int variant)
    {
        Random random(2000u + static_cast<unsigned>(variant) * 131u);
        TreeModel tree;
        tree.height = random.uniform(8.2f, 9.6f);
        const glm::vec3 barkColor {0.34f, 0.25f, 0.19f};
        const float height = tree.height;
        addTube(tree.bark, {{0.0f, 0.0f, 0.0f}, {0.05f, 0.5f * height, -0.04f}, {0.0f, height, 0.0f}}, 0.24f, 0.03f, 8, 8,
                barkColor, height);

        // Whorls of drooping sprays, shorter towards the top: a cone.
        const float bottom = 1.3f;
        int whorl = 0;
        for (float y = bottom; y < height - 0.5f; y += 0.42f, ++whorl)
        {
            const float up01 = (y - bottom) / (height - bottom);
            const float length = 0.45f + 2.35f * std::pow(1.0f - up01, 0.85f);
            const int count = y > height - 2.0f ? 6 : 9;
            for (int spray = 0; spray < count; ++spray)
            {
                const float azimuth = 2.0f * pi * static_cast<float>(spray) / static_cast<float>(count) +
                                      0.41f * static_cast<float>(whorl) + random.uniform(-0.2f, 0.2f);
                const glm::vec3 flat {std::cos(azimuth), 0.0f, std::sin(azimuth)};
                const float droop = random.uniform(0.18f, 0.36f);
                const glm::vec3 along = glm::normalize(flat * std::cos(droop) - up * std::sin(droop));
                // The spray's flat face is tilted so it is never seen edge on.
                glm::vec3 across = glm::normalize(glm::cross(up, flat));
                across = rotateAround(across, along, random.uniform(0.5f, 0.9f));
                const glm::vec3 base = glm::vec3{0.0f, y, 0.0f} + flat * 0.1f;
                const float width = 0.85f + 0.55f * length / 2.8f;
                // u runs from the trunk to the tip, v across the spray.
                const glm::vec3 centre = base + along * (0.5f * length);
                const glm::vec3 tint = glm::vec3{random.uniform(0.9f, 1.05f)};
                addCard(tree.leaves, centre, along * (0.5f * length), across * (0.5f * width), coniferSpray,
                        glm::vec3{0.0f, y + 0.4f, 0.0f}, tint, height, 0.72f);
                tree.crownRadius = std::max(tree.crownRadius, length + 0.2f);
            }
        }
        // The leading shoot: two crossed sprays standing up at the top.
        for (int cross = 0; cross < 2; ++cross)
        {
            const float azimuth = 0.5f * pi * static_cast<float>(cross);
            const glm::vec3 across {std::cos(azimuth), 0.0f, std::sin(azimuth)};
            addCard(tree.leaves, {0.0f, height - 0.2f, 0.0f}, up * 0.7f, across * 0.35f, coniferSpray,
                    {0.0f, height - 1.0f, 0.0f}, glm::vec3{1.0f}, height, 0.5f);
        }
        return tree;
    }

    TreeModel makePalm(int variant)
    {
        Random random(3000u + static_cast<unsigned>(variant) * 57u);
        TreeModel tree;
        tree.height = random.uniform(6.4f, 7.4f);
        const float height = tree.height;
        const glm::vec3 barkColor {0.56f, 0.48f, 0.38f};
        const glm::vec3 lean {random.uniform(0.4f, 0.9f), 0.0f, random.uniform(-0.3f, 0.3f)};
        const std::vector<glm::vec3> trunk = {
            {0.0f, 0.0f, 0.0f}, glm::vec3{0.0f, 0.4f * height, 0.0f} + lean * 0.2f,
            glm::vec3{0.0f, 0.75f * height, 0.0f} + lean * 0.7f, glm::vec3{0.0f, height, 0.0f} + lean};
        addTube(tree.bark, trunk, 0.24f, 0.17f, 18, 9, barkColor, height, true);
        const glm::vec3 top = trunk.back();
        // The crown shaft where the fronds spring from.
        addTube(tree.bark, {top - up * 0.2f, top + up * 0.35f, top + up * 0.55f}, 0.22f, 0.08f, 3, 8, {0.46f, 0.50f, 0.30f}, height);

        const int fronds = 12;
        const glm::vec3 crown = top + up * 0.3f;
        for (int frond = 0; frond < fronds; ++frond)
        {
            const float azimuth = 2.0f * pi * static_cast<float>(frond) / static_cast<float>(fronds) + random.uniform(-0.15f, 0.15f);
            const glm::vec3 flat {std::cos(azimuth), 0.0f, std::sin(azimuth)};
            const float length = random.uniform(3.2f, 3.9f);
            const float lift = random.uniform(0.35f, 0.7f);
            const float sag = random.uniform(0.7f, 1.0f);
            const glm::vec3 side = glm::normalize(glm::cross(up, flat));
            constexpr int pieces = 7;
            const auto spine = [&](float t)
            {
                return crown + flat * (length * t) + up * (length * (lift * t - sag * t * t));
            };
            // Two strips folded down from the rib, like a shallow V.
            for (float half : {-1.0f, 1.0f})
            {
                const auto base = static_cast<unsigned int>(tree.leaves.vertices.size());
                for (int piece = 0; piece <= pieces; ++piece)
                {
                    const float t = static_cast<float>(piece) / static_cast<float>(pieces);
                    const glm::vec3 rib = spine(t);
                    const float width = 0.7f * std::pow(std::sin(pi * (0.08f + 0.92f * t)), 0.7f);
                    const glm::vec3 edge = rib + side * (half * width * 0.93f) - up * (width * 0.37f);
                    const glm::vec3 tangent = glm::normalize(spine(std::min(t + 0.05f, 1.0f)) - spine(std::max(t - 0.05f, 0.0f)));
                    glm::vec3 faceNormal = glm::normalize(glm::cross(tangent, edge - rib + glm::vec3{1.0e-4f}));
                    if (faceNormal.y < 0.0f)
                        faceNormal = -faceNormal;
                    const float u = glm::mix(palmFrond.x, palmFrond.z, t);
                    const float vRib = 0.5f * (palmFrond.y + palmFrond.w);
                    const float vEdge = half < 0.0f ? palmFrond.y : palmFrond.w;
                    const float weight = swayWeight(rib, height) + 0.5f * t;
                    for (int corner = 0; corner < 2; ++corner)
                    {
                        const glm::vec3 position = corner == 0 ? rib : edge;
                        const glm::vec3 normal = glm::normalize(0.55f * faceNormal + 0.45f * glm::normalize(position - crown + up * 0.5f));
                        tree.leaves.vertices.push_back({position, normal, {u, corner == 0 ? vRib : vEdge},
                                                        glm::vec4(glm::vec3{random.uniform(0.95f, 1.05f)}, weight)});
                    }
                }
                for (unsigned int piece = 0; piece < static_cast<unsigned int>(pieces); ++piece)
                {
                    const unsigned int a = base + piece * 2;
                    tree.leaves.indices.insert(tree.leaves.indices.end(), {a, a + 1, a + 3, a, a + 3, a + 2});
                }
            }
            tree.crownRadius = std::max(tree.crownRadius, glm::length(glm::vec2{spine(0.8f).x, spine(0.8f).z}));
        }
        return tree;
    }

    // ---- The leaf atlas -------------------------------------------------------

    struct Canvas
    {
        int size;
        std::vector<float> rgba;   // linear 0..1 while painting, sRGB-ish values

        void blend(int x, int y, const glm::vec3& color, float alpha)
        {
            if (x < 0 || y < 0 || x >= size || y >= size || alpha <= 0.0f)
                return;
            float* texel = &rgba[(static_cast<std::size_t>(y) * size + x) * 4];
            const float keep = 1.0f - alpha;
            for (int channel = 0; channel < 3; ++channel)
                texel[channel] = texel[channel] * keep * texel[3] / std::max(texel[3] * keep + alpha, 1.0e-4f) +
                                 color[channel] * alpha / std::max(texel[3] * keep + alpha, 1.0e-4f);
            texel[3] = texel[3] * keep + alpha;
        }
    };

    // A pointed leaf (a lens shape) from `base` along `direction` in one
    // quarter (region, in texels).
    void paintLeaf(Canvas& canvas, glm::vec2 base, glm::vec2 direction, float length, float width,
                   const glm::vec3& color, const glm::vec4& clip)
    {
        const glm::vec2 side {-direction.y, direction.x};
        const glm::vec2 tip = base + direction * length;
        const glm::vec2 low = glm::min(base, tip) - width;
        const glm::vec2 high = glm::max(base, tip) + width;
        for (int y = static_cast<int>(std::max(low.y, clip.y)); y <= static_cast<int>(std::min(high.y, clip.w)); ++y)
        {
            for (int x = static_cast<int>(std::max(low.x, clip.x)); x <= static_cast<int>(std::min(high.x, clip.z)); ++x)
            {
                const glm::vec2 point = glm::vec2{static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f} - base;
                const float t = glm::dot(point, direction) / length;
                if (t < 0.0f || t > 1.0f)
                    continue;
                const float halfWidth = 0.5f * width * std::pow(std::sin(pi * t), 0.8f);
                const float off = std::abs(glm::dot(point, side));
                const float alpha = glm::clamp(halfWidth - off + 0.5f, 0.0f, 1.0f);
                if (alpha <= 0.0f)
                    continue;
                // Lighter along the midrib and towards the tip, darker at the edges.
                const float across = off / std::max(halfWidth, 0.5f);
                glm::vec3 shade = color * (0.78f + 0.22f * (1.0f - across)) * (0.9f + 0.2f * t);
                if (off < 0.7f)
                    shade = glm::mix(shade, color * 1.35f, 0.6f);
                canvas.blend(x, y, glm::min(shade, glm::vec3{1.0f}), alpha);
            }
        }
    }

    void paintLine(Canvas& canvas, glm::vec2 from, glm::vec2 to, float width, const glm::vec3& color)
    {
        const float length = glm::length(to - from);
        const int steps = static_cast<int>(length * 2.0f) + 1;
        for (int step = 0; step <= steps; ++step)
        {
            const glm::vec2 point = glm::mix(from, to, static_cast<float>(step) / static_cast<float>(steps));
            const int radius = static_cast<int>(std::ceil(width));
            for (int dy = -radius; dy <= radius; ++dy)
            {
                for (int dx = -radius; dx <= radius; ++dx)
                {
                    const float distance = glm::length(glm::vec2{static_cast<float>(dx), static_cast<float>(dy)});
                    canvas.blend(static_cast<int>(point.x) + dx, static_cast<int>(point.y) + dy, color,
                                 glm::clamp(0.5f * width - distance + 0.5f, 0.0f, 1.0f));
                }
            }
        }
    }

    void paintBroadleaf(Canvas& canvas, glm::vec4 region, const glm::vec3& green, unsigned seed)
    {
        // A twig rising from the bottom middle with side shoots, leaves all
        // along them.
        Random random(seed);
        const glm::vec2 origin {region.x, region.y};
        const glm::vec2 size {region.z - region.x, region.w - region.y};
        const auto at = [&](float u, float v) { return origin + glm::vec2{u, v} * size; };
        const glm::vec3 twig {0.30f, 0.22f, 0.14f};
        struct Shoot
        {
            glm::vec2 from, to;
        };
        const std::vector<Shoot> shoots = {
            {at(0.5f, 0.02f), at(0.52f, 0.9f)}, {at(0.5f, 0.3f), at(0.16f, 0.62f)}, {at(0.51f, 0.42f), at(0.86f, 0.7f)},
            {at(0.51f, 0.62f), at(0.24f, 0.9f)}, {at(0.52f, 0.66f), at(0.8f, 0.92f)}, {at(0.5f, 0.18f), at(0.82f, 0.36f)},
            {at(0.5f, 0.14f), at(0.2f, 0.3f)}};
        for (const Shoot& shoot : shoots)
            paintLine(canvas, shoot.from, shoot.to, 2.2f, twig);
        for (const Shoot& shoot : shoots)
        {
            const glm::vec2 along = glm::normalize(shoot.to - shoot.from);
            const float length = glm::length(shoot.to - shoot.from);
            for (float t = 0.2f; t <= 1.0f; t += 0.16f)
            {
                const glm::vec2 point = glm::mix(shoot.from, shoot.to, t);
                for (float sideSign : {-1.0f, 1.0f})
                {
                    const float angle = sideSign * random.uniform(0.5f, 0.9f);
                    const glm::vec2 direction {along.x * std::cos(angle) - along.y * std::sin(angle),
                                               along.x * std::sin(angle) + along.y * std::cos(angle)};
                    const float leafLength = size.x * random.uniform(0.15f, 0.21f) * (0.75f + 0.25f * length / size.y);
                    const glm::vec3 color = green * random.uniform(0.8f, 1.15f);
                    paintLeaf(canvas, point, direction, leafLength, leafLength * 0.5f, color, region);
                }
            }
            paintLeaf(canvas, shoot.to - along * 2.0f, along, size.x * 0.18f, size.x * 0.09f, green * 1.05f, region);
        }
    }

    void paintConifer(Canvas& canvas, glm::vec4 region)
    {
        // A spray along u (from the trunk at u = 0 to the tip), needles
        // slanting towards the tip on both sides of the twig.
        Random random(77u);
        const glm::vec2 origin {region.x, region.y};
        const glm::vec2 size {region.z - region.x, region.w - region.y};
        const glm::vec3 twig {0.28f, 0.20f, 0.13f};
        const glm::vec3 green {0.18f, 0.37f, 0.18f};
        const float middle = origin.y + 0.5f * size.y;
        paintLine(canvas, {origin.x + 2.0f, middle}, {origin.x + size.x - 4.0f, middle}, 3.0f, twig);
        for (float u = 0.03f; u < 0.96f; u += 0.012f)
        {
            const float reach = size.y * 0.47f * (1.0f - 0.55f * u) * random.uniform(0.75f, 1.0f);
            for (float sideSign : {-1.0f, 1.0f})
            {
                const glm::vec2 base {origin.x + u * size.x, middle};
                const float slant = random.uniform(0.5f, 0.8f);
                const glm::vec2 direction = glm::normalize(glm::vec2{std::cos(slant), sideSign * std::sin(slant)});
                paintLeaf(canvas, base, direction, reach, 3.4f, green * random.uniform(0.8f, 1.2f), region);
            }
        }
    }

    void paintPalm(Canvas& canvas, glm::vec4 region)
    {
        // A frond along u with leaflets from the rib at v = 0.5 out to both
        // edges, slanting towards the tip.
        Random random(91u);
        const glm::vec2 origin {region.x, region.y};
        const glm::vec2 size {region.z - region.x, region.w - region.y};
        const glm::vec3 green {0.24f, 0.42f, 0.14f};
        const float middle = origin.y + 0.5f * size.y;
        for (float u = 0.02f; u < 0.98f; u += 0.022f)
        {
            const float reach = size.y * 0.5f * std::pow(std::sin(3.14159f * (0.06f + 0.94f * u)), 0.6f);
            for (float sideSign : {-1.0f, 1.0f})
            {
                const glm::vec2 base {origin.x + u * size.x, middle};
                const glm::vec2 direction = glm::normalize(glm::vec2{0.55f, sideSign * 1.0f});
                paintLeaf(canvas, base, direction, reach * 1.1f, size.x * 0.034f, green * random.uniform(0.85f, 1.15f), region);
            }
        }
        paintLine(canvas, {origin.x, middle}, {origin.x + size.x, middle}, 3.0f, {0.52f, 0.50f, 0.26f});
    }
}

namespace TreeGenerator
{
    TreeModel make(TreeSpecies species, int variant)
    {
        variant = ((variant % variantsPerSpecies) + variantsPerSpecies) % variantsPerSpecies;
        switch (species)
        {
        case TreeSpecies::Conifer: return makeConifer(variant);
        case TreeSpecies::Palm: return makePalm(variant);
        default: return makeBroadleaf(variant);
        }
    }

    std::vector<unsigned char> leafAtlas(int size)
    {
        Canvas canvas {size, std::vector<float>(static_cast<std::size_t>(size) * size * 4, 0.0f)};
        // The transparent background carries a leaf green, so filtering at
        // the edge of a leaf fades to green rather than to black.
        for (std::size_t index = 0; index < canvas.rgba.size(); index += 4)
        {
            canvas.rgba[index] = 0.2f;
            canvas.rgba[index + 1] = 0.32f;
            canvas.rgba[index + 2] = 0.14f;
        }
        const float s = static_cast<float>(size);
        const auto texels = [s](const glm::vec4& region) { return glm::vec4{region.x * s, region.y * s, region.z * s, region.w * s}; };
        paintBroadleaf(canvas, texels(broadleafA), {0.26f, 0.45f, 0.16f}, 5u);
        paintBroadleaf(canvas, texels(broadleafB), {0.36f, 0.48f, 0.14f}, 9u);
        paintConifer(canvas, texels(coniferSpray));
        paintPalm(canvas, texels(palmFrond));

        std::vector<unsigned char> bytes(canvas.rgba.size());
        for (std::size_t index = 0; index < bytes.size(); ++index)
            bytes[index] = static_cast<unsigned char>(glm::clamp(canvas.rgba[index], 0.0f, 1.0f) * 255.0f + 0.5f);
        return bytes;
    }
}
