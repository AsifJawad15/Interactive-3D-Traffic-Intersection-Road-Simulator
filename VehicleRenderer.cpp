#include "VehicleRenderer.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

#include <stb_easy_font.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <string>

namespace
{
    // A vehicle's origin rides 0.1 m above the road, whose surface is at
    // 0.06 m: the ground is 4 cm below the local origin. Every height below is
    // written as a height above the ground.
    constexpr float ground = -0.04f;

    const glm::vec3 glassColor {0.055f, 0.085f, 0.11f};
    const glm::vec3 trimColor {0.05f, 0.055f, 0.06f};
    const glm::vec3 tyreColor {0.028f, 0.03f, 0.034f};
    const glm::vec3 hubColor {0.60f, 0.62f, 0.66f};
    const glm::vec3 lensColor {0.80f, 0.81f, 0.79f};
    const glm::vec3 tailColor {0.50f, 0.035f, 0.025f};
    const glm::vec3 amberColor {0.95f, 0.52f, 0.10f};
    const glm::vec3 redLensColor {0.75f, 0.05f, 0.05f};
    const glm::vec3 blueLensColor {0.06f, 0.16f, 0.78f};

    // ---- Collecting triangles for one material ------------------------------

    struct Builder
    {
        MeshData data;

        void quad(const std::array<glm::vec3, 4>& p, const std::array<glm::vec3, 4>& n)
        {
            const unsigned int base = static_cast<unsigned int>(data.vertices.size());
            for (std::size_t corner = 0; corner < 4; ++corner)
                data.vertices.push_back({p[corner], n[corner], {0.0f, 0.0f}});
            data.indices.insert(data.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
        }

        void fan(const glm::vec3& centre, const std::vector<glm::vec3>& ring, const glm::vec3& normal)
        {
            const unsigned int base = static_cast<unsigned int>(data.vertices.size());
            data.vertices.push_back({centre, normal, {0.5f, 0.5f}});
            for (const glm::vec3& point : ring)
                data.vertices.push_back({point, normal, {0.0f, 0.0f}});
            const unsigned int count = static_cast<unsigned int>(ring.size());
            for (unsigned int index = 0; index < count; ++index)
                data.indices.insert(data.indices.end(), {base, base + 1 + index, base + 1 + (index + 1) % count});
        }

        void append(const MeshData& other, const glm::mat4& transform)
        {
            const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(transform)));
            const unsigned int base = static_cast<unsigned int>(data.vertices.size());
            for (const Vertex& vertex : other.vertices)
            {
                const glm::vec4 position = transform * glm::vec4(vertex.position, 1.0f);
                data.vertices.push_back({glm::vec3(position), glm::normalize(normalMatrix * vertex.normal), vertex.texCoord});
            }
            for (unsigned int index : other.indices)
                data.indices.push_back(base + index);
        }

        // A box with softened edges, `size` across, centred on `centre` (the
        // height is above the ground), turned `yawDegrees` about the vertical.
        void box(const glm::vec3& centre, const glm::vec3& size, float yawDegrees = 0.0f, float bevel = 0.12f)
        {
            static const MeshData soft = Mesh::beveledCubeData(0.12f);
            static const MeshData sharp = Mesh::beveledCubeData(0.03f);
            glm::mat4 transform = glm::translate(glm::mat4(1.0f), centre + glm::vec3(0.0f, ground, 0.0f));
            transform = glm::rotate(transform, glm::radians(yawDegrees), {0.0f, 1.0f, 0.0f});
            transform = glm::scale(transform, size);
            append(bevel > 0.06f ? soft : sharp, transform);
        }

        Mesh build() const { return data.indices.empty() ? Mesh() : Mesh(data); }
    };

    // The pixel rectangles stb_easy_font draws a line of text with, in its
    // own units (a capital is 7 units tall).
    struct EasyFontVertex
    {
        float x, y, z;
        unsigned char color[4];
    };

    // Text as small boxes on a plane, `height` metres tall, centred on
    // `centre`, readable from the side that `facing` (local +z or -z, or +x
    // or -x) looks at.
    void addText(Builder& builder, const std::string& text, const glm::vec3& centre, float height, int facing)
    {
        std::vector<char> buffer(text.begin(), text.end());
        buffer.push_back('\0');
        std::vector<unsigned char> vertices(16 * 1024);
        const int quads = stb_easy_font_print(0.0f, 0.0f, buffer.data(), nullptr,
                                              vertices.data(), static_cast<int>(vertices.size()));
        const auto* raw = reinterpret_cast<const EasyFontVertex*>(vertices.data());
        float right = 0.0f;
        for (int quad = 0; quad < quads; ++quad)
            right = std::max(right, std::max(raw[quad * 4].x, raw[quad * 4 + 2].x));
        const float scale = height / 7.0f;
        for (int quad = 0; quad < quads; ++quad)
        {
            const EasyFontVertex& a = raw[quad * 4];
            const EasyFontVertex& c = raw[quad * 4 + 2];
            const float u = (0.5f * (a.x + c.x) - 0.5f * right) * scale;
            const float v = (3.5f - 0.5f * (a.y + c.y)) * scale;
            const float w = std::abs(c.x - a.x) * scale;
            const float h = std::abs(c.y - a.y) * scale;
            // Reading direction: the text runs to the reader's right, which
            // for someone looking at the +z side is +x.
            glm::vec3 offset {0.0f};
            glm::vec3 size {0.0f};
            switch (facing)
            {
            case 0: offset = {u, v, 0.0f}; size = {w, h, 0.012f}; break;     // read from +z: right is +x
            case 1: offset = {-u, v, 0.0f}; size = {w, h, 0.012f}; break;    // read from -z: right is -x
            case 2: offset = {0.0f, v, -u}; size = {0.012f, h, w}; break;    // read from +x: right is -z
            default: offset = {0.0f, v, u}; size = {0.012f, h, w}; break;    // read from -x: right is +z
            }
            builder.box(centre + offset, size, 0.0f, 0.0f);
        }
    }

    // ---- The lofted body --------------------------------------------------------

    struct Loft
    {
        float rearZ = -2.0f;
        float frontZ = 2.0f;
        float width = 1.8f;
        // The top line seen from the side, as corner points (z, height)
        // from the rear end to the front end.
        std::vector<glm::vec2> corners;
        float roundness = 0.3f;   // share of each run the Bezier corners take
        float sill = 0.3f;
        float beltRear = 1.0f;    // the window line
        float beltFront = 1.0f;
        float tumblehome = 0.28f; // inward lean of the glass house, per metre up
        float planRadius = 0.3f;  // corners rounded seen from above
        float windowFrom = 0.0f;  // side windows between these
        float windowTo = 0.0f;
        std::vector<glm::vec2> pillars;   // (from, to): paint inside the windows
        float windscreenFrom = 99.0f;     // steep top above the belt ahead of this: glass
        float rearWindowTo = -99.0f;      // ... and behind this
        std::vector<glm::vec2> arches;    // (z, wheel radius)
        float bonnetFrom = 99.0f;         // what lies ahead of this is the bonnet
    };

    // The top line: straight runs between the corner points, every corner
    // rounded by a quadratic Bezier curve (the Lab 5 Bernstein form) from
    // `roundness` of the way along the run before it to the same share of
    // the run after it.
    std::vector<glm::vec2> topLine(const Loft& loft)
    {
        const std::vector<glm::vec2>& c = loft.corners;
        std::vector<glm::vec2> line {c.front()};
        for (std::size_t index = 1; index + 1 < c.size(); ++index)
        {
            const std::vector<glm::vec2> control = {
                c[index] + (c[index - 1] - c[index]) * loft.roundness,
                c[index],
                c[index] + (c[index + 1] - c[index]) * loft.roundness
            };
            for (int step = 0; step <= 10; ++step)
                line.push_back(Mesh::bezier(static_cast<float>(step) / 10.0f, control));
        }
        line.push_back(c.back());
        return line;
    }

    float heightAt(const std::vector<glm::vec2>& line, float z)
    {
        if (z <= line.front().x)
            return line.front().y;
        for (std::size_t index = 1; index < line.size(); ++index)
        {
            if (z <= line[index].x)
            {
                const glm::vec2 a = line[index - 1];
                const glm::vec2 b = line[index];
                const float span = b.x - a.x;
                return span > 1.0e-5f ? glm::mix(a.y, b.y, (z - a.x) / span) : b.y;
            }
        }
        return line.back().y;
    }

    float planHalfWidth(const Loft& loft, float z)
    {
        const float half = 0.5f * loft.width;
        const float r = loft.planRadius;
        float over = 0.0f;
        if (z > loft.frontZ - r)
            over = z - (loft.frontZ - r);
        else if (z < loft.rearZ + r)
            over = (loft.rearZ + r) - z;
        over = std::min(over, r);
        return half - r + std::sqrt(std::max(0.0f, r * r - over * over));
    }

    // Where the front (or rear) face is at a sideways position x, and which
    // way it faces there (degrees about the vertical).
    void faceAt(const Loft& loft, float x, bool front, float& z, float& yawDegrees)
    {
        const float flat = 0.5f * loft.width - loft.planRadius;
        z = front ? loft.frontZ : loft.rearZ;
        yawDegrees = 0.0f;
        const float out = std::abs(x) - flat;
        if (out <= 0.0f)
            return;
        const float r = loft.planRadius;
        const float dx = std::min(out, r * 0.999f);
        const float inset = r - std::sqrt(r * r - dx * dx);
        z += front ? -inset : inset;
        const float angle = glm::degrees(std::asin(dx / r));
        yawDegrees = (x > 0.0f) == front ? angle : -angle;
    }

    // A lamp lens on the front or rear face.
    void addLamp(Builder& builder, const Loft& loft, float x, float height, glm::vec2 size, bool front)
    {
        float z = 0.0f;
        float yaw = 0.0f;
        faceAt(loft, x, front, z, yaw);
        builder.box({x, height, z + (front ? -0.015f : 0.015f)}, {size.x, size.y, 0.07f}, yaw, 0.12f);
    }

    struct LoftBuilders
    {
        Builder* paint;
        Builder* glass;
        Builder* trim;
        Builder* bonnet;
    };

    // Sweeps the body. Every station across the length gets the same outline
    // (13 points): the lower flank, the side up to the window line, the
    // glass house leaning in to the roof edge, a slightly domed roof, and the
    // same down the other side. Neighbouring stations are joined band by
    // band; each band decides for itself whether it is paint or glass.
    void buildLoft(const Loft& loft, const LoftBuilders& out)
    {
        const std::vector<glm::vec2> line = topLine(loft);
        const float length = loft.frontZ - loft.rearZ;
        const int count = std::max(28, static_cast<int>(length / 0.08f));
        constexpr int points = 13;
        const float crown = 0.022f * loft.width;

        std::vector<float> zs(static_cast<std::size_t>(count) + 1);
        std::vector<std::array<glm::vec3, points>> rings(zs.size());
        std::vector<float> tops(zs.size());
        std::vector<float> belts(zs.size());
        std::vector<float> middles(zs.size());
        for (int k = 0; k <= count; ++k)
        {
            // Stations crowd towards both ends, where the body turns fastest.
            const float u = static_cast<float>(k) / static_cast<float>(count);
            const float z = loft.rearZ + length * (0.5f - 0.5f * std::cos(std::numbers::pi_v<float> * u));
            const float t = (z - loft.rearZ) / length;
            const float top = heightAt(line, z);
            const float belt = std::min(glm::mix(loft.beltRear, loft.beltFront, t), top);
            float sill = loft.sill;
            for (const glm::vec2& arch : loft.arches)
            {
                const float reach = arch.y + 0.07f;
                const float along = std::abs(z - arch.x);
                if (along < reach)
                    sill = std::max(sill, arch.y + std::sqrt(reach * reach - along * along));
            }
            sill = std::min(sill, std::min(belt, top) - 0.1f);
            const float lower = std::min(sill + 0.1f, belt);
            const float half = planHalfWidth(loft, z);
            const float roof = std::max(0.12f, half - loft.tumblehome * (top - belt));

            const auto domed = [&](float x)
            {
                const float share = x / roof;
                return top + crown * (1.0f - share * share);
            };
            const std::array<glm::vec2, points> outline = {
                glm::vec2{half - 0.05f, sill}, {half, lower}, {half, belt}, {roof, top},
                {roof * 2.0f / 3.0f, domed(roof * 2.0f / 3.0f)}, {roof / 3.0f, domed(roof / 3.0f)}, {0.0f, top + crown},
                {-roof / 3.0f, domed(roof / 3.0f)}, {-roof * 2.0f / 3.0f, domed(roof * 2.0f / 3.0f)},
                {-roof, top}, {-half, belt}, {-half, lower}, {-(half - 0.05f), sill}
            };
            const std::size_t station = static_cast<std::size_t>(k);
            zs[station] = z;
            tops[station] = top;
            belts[station] = belt;
            middles[station] = 0.5f * (sill + top) + ground;
            for (int j = 0; j < points; ++j)
                rings[station][static_cast<std::size_t>(j)] = {outline[static_cast<std::size_t>(j)].x,
                                                               outline[static_cast<std::size_t>(j)].y + ground, z};
        }

        // The bands, as runs of outline points.
        enum class Part { Lower, Side, House, Roof, Under };
        struct Band
        {
            Part part;
            std::vector<int> run;
        };
        const std::array<Band, 8> bands = {{
            {Part::Lower, {0, 1}}, {Part::Side, {1, 2}}, {Part::House, {2, 3}},
            {Part::Roof, {3, 4, 5, 6, 7, 8, 9}},
            {Part::House, {9, 10}}, {Part::Side, {10, 11}}, {Part::Lower, {11, 12}}, {Part::Under, {12, 0}}
        }};

        const float middleZ = 0.5f * (loft.rearZ + loft.frontZ);
        const auto normalAt = [&](int k, const Band& band, std::size_t position)
        {
            const std::size_t station = static_cast<std::size_t>(k);
            const int j = band.run[position];
            const int previous = band.run[position == 0 ? 0 : position - 1];
            const int next = band.run[std::min(position + 1, band.run.size() - 1)];
            // Outward: away from the middle of the body.
            const glm::vec3 point = rings[station][static_cast<std::size_t>(j)];
            const glm::vec3 hint {point.x / (0.5f * loft.width), (point.y - middles[station]) / 0.8f,
                                  (point.z - middleZ) / (0.5f * length)};

            glm::vec3 across = rings[station][static_cast<std::size_t>(next)] - rings[station][static_cast<std::size_t>(previous)];
            // A band closed up to nothing here (the glass house over the
            // bonnet): take the way the outline runs through this point.
            if (glm::length(across) < 1.0e-4f)
                across = rings[station][static_cast<std::size_t>((j + 1) % points)] -
                         rings[station][static_cast<std::size_t>((j + points - 1) % points)];
            const glm::vec3 along = rings[static_cast<std::size_t>(std::min(k + 1, count))][static_cast<std::size_t>(j)] -
                                    rings[static_cast<std::size_t>(std::max(k - 1, 0))][static_cast<std::size_t>(j)];
            glm::vec3 normal = glm::cross(across, along);
            if (glm::length(normal) < 1.0e-7f)
                normal = glm::cross(across, glm::vec3(0.0f, 0.0f, 1.0f));
            if (glm::length(normal) < 1.0e-7f)
                normal = hint;
            normal = glm::normalize(normal);
            return glm::dot(normal, hint) < 0.0f ? -normal : normal;
        };

        for (int k = 0; k < count; ++k)
        {
            const std::size_t a = static_cast<std::size_t>(k);
            const std::size_t b = a + 1;
            const float zMiddle = 0.5f * (zs[a] + zs[b]);
            const bool house = tops[a] - belts[a] > 0.04f && tops[b] - belts[b] > 0.04f;
            const float slope = std::abs(tops[b] - tops[a]) / std::max(zs[b] - zs[a], 1.0e-4f);
            const bool screen = house && slope > 0.4f && (zMiddle > loft.windscreenFrom || zMiddle < loft.rearWindowTo);
            bool window = house && !screen && slope <= 0.4f && zMiddle > loft.windowFrom && zMiddle < loft.windowTo;
            for (const glm::vec2& pillar : loft.pillars)
                window = window && !(zMiddle > pillar.x && zMiddle < pillar.y);
            const bool bonnet = zMiddle > loft.bonnetFrom;

            for (const Band& band : bands)
            {
                Builder* target = out.paint;
                if (band.part == Part::Under)
                    target = out.trim;
                else if (band.part == Part::Roof && screen)
                    target = out.glass;
                else if (band.part == Part::House && window)
                    target = out.glass;

                for (std::size_t position = 0; position + 1 < band.run.size(); ++position)
                {
                    const std::size_t j0 = static_cast<std::size_t>(band.run[position]);
                    const std::size_t j1 = static_cast<std::size_t>(band.run[position + 1]);
                    const std::array<glm::vec3, 4> corners = {rings[a][j0], rings[b][j0], rings[b][j1], rings[a][j1]};
                    const std::array<glm::vec3, 4> normals = {normalAt(k, band, position), normalAt(k + 1, band, position),
                                                              normalAt(k + 1, band, position + 1), normalAt(k, band, position + 1)};
                    target->quad(corners, normals);
                    if (bonnet && target == out.paint && out.bonnet != nullptr)
                        out.bonnet->quad(corners, normals);
                }
            }
        }

        // The two end faces.
        for (const int k : {0, count})
        {
            const std::size_t station = static_cast<std::size_t>(k);
            std::vector<glm::vec3> ring(rings[station].begin(), rings[station].end());
            glm::vec3 centre {0.0f};
            for (const glm::vec3& point : ring)
                centre += point;
            centre /= static_cast<float>(ring.size());
            const glm::vec3 normal {0.0f, 0.0f, k == 0 ? -1.0f : 1.0f};
            out.paint->fan(centre, ring, normal);
            if (k == count && out.bonnet != nullptr && loft.bonnetFrom < loft.frontZ)
                out.bonnet->fan(centre, ring, normal);
        }
    }

    // ---- Parts shared by many kinds ----------------------------------------------

    struct Parts
    {
        Builder paint, second, glass, trim, headLamps, tailLamps, leftIndicators, rightIndicators,
                redFlashers, blueFlashers, sign, doors, bonnet, dashboard;

        LoftBuilders loft()
        {
            return {&paint, &glass, &trim, &bonnet};
        }
    };

    // Headlights, tail lights and the four indicators of an ordinary body.
    void addLamps(Parts& parts, const Loft& loft, float headHeight, float headX, float tailHeight, float tailX,
                  glm::vec2 headSize = {0.34f, 0.14f}, glm::vec2 tailSize = {0.30f, 0.14f})
    {
        for (float side : {-1.0f, 1.0f})
        {
            addLamp(parts.headLamps, loft, side * headX, headHeight, headSize, true);
            addLamp(parts.tailLamps, loft, side * tailX, tailHeight, tailSize, false);
            Builder& indicators = side > 0.0f ? parts.leftIndicators : parts.rightIndicators;
            const float cornerX = side * (0.5f * loft.width - 0.10f);
            addLamp(indicators, loft, cornerX, headHeight - 0.02f, {0.10f, 0.09f}, true);
            addLamp(indicators, loft, cornerX, tailHeight - 0.13f, {0.12f, 0.08f}, false);
        }
    }

    void addBumpers(Parts& parts, const Loft& loft, float frontHeight, float rearHeight)
    {
        const float span = loft.width - 0.12f;
        parts.trim.box({0.0f, frontHeight, loft.frontZ - 0.02f}, {span, 0.15f, 0.12f});
        parts.trim.box({0.0f, rearHeight, loft.rearZ + 0.02f}, {span, 0.15f, 0.12f});
    }

    void addMirrors(Parts& parts, float z, float height, float halfWidth)
    {
        for (float side : {-1.0f, 1.0f})
            parts.trim.box({side * (halfWidth + 0.09f), height, z}, {0.16f, 0.11f, 0.22f});
    }

    // Dashboard and steering wheel, only drawn in the view from the seat.
    void addDashboard(Parts& parts, const glm::vec3& eye, float width)
    {
        parts.dashboard.box({0.0f, eye.y - 0.34f, eye.z + 0.62f}, {width - 0.18f, 0.10f, 0.32f});
        glm::mat4 wheel = glm::translate(glm::mat4(1.0f), {eye.x, eye.y - 0.30f + ground, eye.z + 0.42f});
        wheel = glm::rotate(wheel, glm::radians(70.0f), {1.0f, 0.0f, 0.0f});
        wheel = glm::scale(wheel, {0.36f, 0.04f, 0.36f});
        parts.dashboard.append(Mesh::beveledCubeData(0.24f), wheel);
    }
}

// ---------------------------------------------------------------------------
// The kinds
// ---------------------------------------------------------------------------

VehicleRenderer::VehicleRenderer()
    : tyre_(Mesh::makeCylinder(22)), hub_(Mesh::makeCylinder(12))
{
    for (std::size_t index = 0; index < vehicleKindCount; ++index)
    {
        const VehicleKind kind = static_cast<VehicleKind>(index);
        const VehicleSpec& spec = vehicleSpec(kind);
        const SizeClassSpec& size = sizeClassSpec(spec.sizeClass);
        const float frontAxle = size.frontAxle;
        const float rearAxle = size.frontAxle - size.wheelBase;
        const float halfLength = 0.5f * spec.length;
        const float halfWidth = 0.5f * spec.width;

        Parts parts;
        KindMeshes& meshes = kinds_[index];
        const auto wheelsAt = [&](float radius, float width, float rearWidth)
        {
            const float x = halfWidth - 0.5f * width - 0.06f;
            for (float side : {-1.0f, 1.0f})
            {
                meshes.wheels.push_back({{side * x, radius + ground, frontAxle}, radius, width, true});
                meshes.wheels.push_back({{side * (halfWidth - 0.5f * rearWidth - 0.06f), radius + ground, rearAxle},
                                         radius, rearWidth, false});
            }
        };

        // A saloon-car body scaled to the kind's length and width.
        const auto saloon = [&](float wheelRadius)
        {
            Loft loft;
            loft.rearZ = -halfLength;
            loft.frontZ = halfLength;
            loft.width = spec.width;
            const float s = spec.length / 4.70f;
            loft.corners = {{-2.35f * s, 0.74f}, {-2.29f * s, 0.98f}, {-1.60f * s, 1.03f}, {-0.74f * s, 1.42f},
                            {0.40f * s, 1.44f}, {1.10f * s, 1.00f}, {2.20f * s, 0.87f}, {2.35f * s, 0.70f}};
            loft.sill = 0.32f;
            loft.beltRear = 1.00f;
            loft.beltFront = 0.96f;
            loft.windowFrom = -0.95f * s;
            loft.windowTo = 0.98f * s;
            loft.pillars = {{-0.07f, 0.05f}};
            loft.windscreenFrom = 0.3f * s;
            loft.rearWindowTo = -0.6f * s;
            loft.arches = {{frontAxle, wheelRadius}, {rearAxle, wheelRadius}};
            loft.bonnetFrom = 1.05f * s;
            buildLoft(loft, parts.loft());
            addLamps(parts, loft, 0.70f, halfWidth - 0.33f, 0.86f, halfWidth - 0.30f);
            addBumpers(parts, loft, 0.40f, 0.44f);
            addMirrors(parts, 0.98f * s, 1.02f, halfWidth);
            wheelsAt(wheelRadius, 0.22f, 0.22f);
            return loft;
        };

        switch (kind)
        {
        case VehicleKind::Sedan:
            saloon(0.33f);
            addDashboard(parts, spec.eye, spec.width);
            break;

        case VehicleKind::Taxi:
        {
            saloon(0.33f);
            // The lit roof sign, lettered on both faces, and a dark band
            // along both sides.
            parts.trim.box({0.0f, 1.47f, -0.05f}, {0.74f, 0.05f, 0.30f});
            parts.sign.box({0.0f, 1.58f, -0.05f}, {0.66f, 0.18f, 0.24f}, 0.0f, 0.2f);
            addText(parts.trim, "TAXI", {0.0f, 1.58f, 0.075f}, 0.10f, 0);
            addText(parts.trim, "TAXI", {0.0f, 1.58f, -0.175f}, 0.10f, 1);
            for (float side : {-1.0f, 1.0f})
                parts.second.box({side * (halfWidth + 0.004f), 0.80f, 0.0f}, {0.012f, 0.07f, 2.9f}, 0.0f, 0.0f);
            meshes.secondColor = {0.06f, 0.06f, 0.07f};
            meshes.signColor = {1.0f, 0.78f, 0.25f};
            addDashboard(parts, spec.eye, spec.width);
            break;
        }

        case VehicleKind::Police:
        {
            saloon(0.33f);
            // Blue doors lettered POLICE, and the light bar on the roof.
            for (float side : {-1.0f, 1.0f})
            {
                parts.second.box({side * (halfWidth + 0.004f), 0.74f, 0.0f}, {0.012f, 0.36f, 2.1f}, 0.0f, 0.0f);
                addText(parts.paint, "POLICE", {side * (halfWidth + 0.012f), 0.74f, 0.0f}, 0.18f, side > 0.0f ? 2 : 3);
            }
            parts.trim.box({0.0f, 1.49f, -0.15f}, {1.10f, 0.06f, 0.26f});
            parts.redFlashers.box({0.27f, 1.58f, -0.15f}, {0.52f, 0.12f, 0.22f}, 0.0f, 0.2f);
            parts.blueFlashers.box({-0.27f, 1.58f, -0.15f}, {0.52f, 0.12f, 0.22f}, 0.0f, 0.2f);
            meshes.secondColor = {0.05f, 0.12f, 0.42f};
            meshes.lightBar = {0.0f, 1.9f + ground, -0.15f};
            addDashboard(parts, spec.eye, spec.width);
            break;
        }

        case VehicleKind::Hatchback:
        {
            Loft loft;
            loft.rearZ = -halfLength;
            loft.frontZ = halfLength;
            loft.width = spec.width;
            loft.corners = {{-2.03f, 0.80f}, {-2.00f, 1.05f}, {-1.90f, 1.38f}, {-1.58f, 1.47f},
                            {0.25f, 1.48f}, {0.95f, 1.00f}, {1.90f, 0.86f}, {2.03f, 0.68f}};
            loft.sill = 0.30f;
            loft.beltRear = 1.02f;
            loft.beltFront = 0.96f;
            loft.windowFrom = -1.55f;
            loft.windowTo = 0.88f;
            loft.pillars = {{-0.02f, 0.09f}};
            loft.windscreenFrom = 0.2f;
            loft.rearWindowTo = -1.75f;
            loft.arches = {{frontAxle, 0.32f}, {rearAxle, 0.32f}};
            loft.bonnetFrom = 0.95f;
            buildLoft(loft, parts.loft());
            addLamps(parts, loft, 0.72f, halfWidth - 0.32f, 0.95f, halfWidth - 0.22f, {0.32f, 0.13f}, {0.18f, 0.22f});
            addBumpers(parts, loft, 0.40f, 0.44f);
            addMirrors(parts, 0.88f, 1.02f, halfWidth);
            wheelsAt(0.32f, 0.21f, 0.21f);
            addDashboard(parts, spec.eye, spec.width);
            break;
        }

        case VehicleKind::Suv:
        {
            Loft loft;
            loft.rearZ = -halfLength;
            loft.frontZ = halfLength;
            loft.width = spec.width;
            loft.corners = {{-2.40f, 0.95f}, {-2.36f, 1.22f}, {-2.26f, 1.66f}, {-1.95f, 1.74f},
                            {0.55f, 1.74f}, {1.20f, 1.26f}, {2.25f, 1.12f}, {2.40f, 0.90f}};
            loft.sill = 0.46f;
            loft.beltRear = 1.24f;
            loft.beltFront = 1.20f;
            loft.tumblehome = 0.22f;
            loft.windowFrom = -2.05f;
            loft.windowTo = 1.10f;
            loft.pillars = {{0.02f, 0.14f}, {-1.14f, -1.03f}};
            loft.windscreenFrom = 0.5f;
            loft.rearWindowTo = -2.1f;
            loft.arches = {{frontAxle, 0.38f}, {rearAxle, 0.38f}};
            loft.bonnetFrom = 1.15f;
            buildLoft(loft, parts.loft());
            addLamps(parts, loft, 0.94f, halfWidth - 0.33f, 1.10f, halfWidth - 0.22f, {0.34f, 0.14f}, {0.18f, 0.26f});
            addBumpers(parts, loft, 0.55f, 0.60f);
            addMirrors(parts, 1.10f, 1.28f, halfWidth);
            for (float side : {-1.0f, 1.0f})
                parts.trim.box({side * 0.68f, 1.78f, -0.55f}, {0.05f, 0.05f, 2.4f});
            wheelsAt(0.38f, 0.24f, 0.24f);
            addDashboard(parts, spec.eye, spec.width);
            break;
        }

        case VehicleKind::Van:
        {
            Loft loft;
            loft.rearZ = -halfLength;
            loft.frontZ = halfLength;
            loft.width = spec.width;
            loft.corners = {{-2.60f, 0.80f}, {-2.58f, 2.14f}, {-2.48f, 2.25f}, {1.05f, 2.25f},
                            {1.62f, 1.36f}, {2.45f, 1.06f}, {2.60f, 0.84f}};
            loft.roundness = 0.25f;
            loft.sill = 0.40f;
            loft.beltRear = 1.28f;
            loft.beltFront = 1.26f;
            loft.tumblehome = 0.10f;
            loft.planRadius = 0.25f;
            loft.windowFrom = 0.50f;
            loft.windowTo = 1.18f;
            loft.windscreenFrom = 1.0f;
            loft.arches = {{frontAxle, 0.36f}, {rearAxle, 0.36f}};
            loft.bonnetFrom = 1.60f;
            buildLoft(loft, parts.loft());
            addLamps(parts, loft, 0.88f, halfWidth - 0.30f, 1.05f, halfWidth - 0.12f, {0.30f, 0.14f}, {0.14f, 0.40f});
            addBumpers(parts, loft, 0.45f, 0.48f);
            addMirrors(parts, 1.15f, 1.45f, halfWidth);
            // A sliding-door rail and a stripe along the panel sides.
            for (float side : {-1.0f, 1.0f})
            {
                parts.trim.box({side * (halfWidth + 0.01f), 1.28f, -0.55f}, {0.02f, 0.03f, 1.6f}, 0.0f, 0.0f);
                parts.second.box({side * (halfWidth + 0.004f), 0.95f, -0.9f}, {0.012f, 0.10f, 3.2f}, 0.0f, 0.0f);
            }
            meshes.secondColor = {0.95f, 0.55f, 0.08f};
            wheelsAt(0.36f, 0.23f, 0.23f);
            addDashboard(parts, spec.eye, spec.width);
            break;
        }

        case VehicleKind::Pickup:
        {
            Loft loft;
            loft.rearZ = -halfLength;
            loft.frontZ = halfLength;
            loft.width = spec.width;
            loft.corners = {{-2.70f, 0.92f}, {-2.66f, 1.12f}, {-0.64f, 1.12f}, {-0.56f, 1.78f},
                            {0.85f, 1.82f}, {1.40f, 1.26f}, {2.55f, 1.12f}, {2.70f, 0.90f}};
            loft.roundness = 0.22f;
            loft.sill = 0.50f;
            loft.beltRear = 1.22f;
            loft.beltFront = 1.20f;
            loft.tumblehome = 0.20f;
            loft.windowFrom = -0.45f;
            loft.windowTo = 1.30f;
            loft.pillars = {{0.40f, 0.49f}};
            loft.windscreenFrom = 0.8f;
            loft.rearWindowTo = -0.5f;
            loft.arches = {{frontAxle, 0.38f}, {rearAxle, 0.38f}};
            loft.bonnetFrom = 1.35f;
            buildLoft(loft, parts.loft());
            // The open load bed: a dark liner between the bed rails.
            parts.trim.box({0.0f, 1.125f, -1.62f}, {spec.width - 0.24f, 0.02f, 1.90f}, 0.0f, 0.0f);
            addLamps(parts, loft, 0.98f, halfWidth - 0.32f, 1.00f, halfWidth - 0.12f, {0.34f, 0.15f}, {0.14f, 0.28f});
            addBumpers(parts, loft, 0.58f, 0.60f);
            addMirrors(parts, 1.30f, 1.30f, halfWidth);
            wheelsAt(0.38f, 0.25f, 0.25f);
            addDashboard(parts, spec.eye, spec.width);
            break;
        }

        case VehicleKind::Ambulance:
        {
            Loft loft;
            loft.rearZ = -halfLength;
            loft.frontZ = halfLength;
            loft.width = spec.width;
            loft.corners = {{-2.75f, 0.86f}, {-2.73f, 2.52f}, {-2.64f, 2.60f}, {0.78f, 2.60f}, {0.86f, 2.20f},
                            {1.28f, 2.20f}, {1.88f, 1.36f}, {2.60f, 1.10f}, {2.75f, 0.88f}};
            loft.roundness = 0.2f;
            loft.sill = 0.42f;
            loft.beltRear = 1.30f;
            loft.beltFront = 1.28f;
            loft.tumblehome = 0.05f;
            loft.planRadius = 0.2f;
            loft.windowFrom = 1.02f;
            loft.windowTo = 1.75f;
            loft.windscreenFrom = 1.3f;
            loft.arches = {{frontAxle, 0.37f}, {rearAxle, 0.37f}};
            loft.bonnetFrom = 1.85f;
            buildLoft(loft, parts.loft());
            // A red stripe round the body and a red cross on each side.
            for (float side : {-1.0f, 1.0f})
            {
                const float x = side * (halfWidth + 0.004f);
                parts.second.box({x, 1.15f, 0.0f}, {0.012f, 0.22f, spec.length - 0.3f}, 0.0f, 0.0f);
                parts.second.box({x, 1.95f, -1.0f}, {0.012f, 0.60f, 0.20f}, 0.0f, 0.0f);
                parts.second.box({x, 1.95f, -1.0f}, {0.012f, 0.20f, 0.60f}, 0.0f, 0.0f);
            }
            parts.second.box({0.0f, 1.15f, -halfLength - 0.004f}, {spec.width - 0.4f, 0.22f, 0.012f}, 0.0f, 0.0f);
            // Light bars at the front of the cab roof and the rear of the box.
            parts.redFlashers.box({0.36f, 2.27f, 1.08f}, {0.50f, 0.12f, 0.20f}, 0.0f, 0.2f);
            parts.blueFlashers.box({-0.36f, 2.27f, 1.08f}, {0.50f, 0.12f, 0.20f}, 0.0f, 0.2f);
            parts.blueFlashers.box({0.80f, 2.54f, -2.66f}, {0.14f, 0.12f, 0.12f}, 0.0f, 0.2f);
            parts.redFlashers.box({-0.80f, 2.54f, -2.66f}, {0.14f, 0.12f, 0.12f}, 0.0f, 0.2f);
            addLamps(parts, loft, 0.90f, halfWidth - 0.30f, 1.00f, halfWidth - 0.12f, {0.30f, 0.14f}, {0.14f, 0.32f});
            addBumpers(parts, loft, 0.46f, 0.50f);
            addMirrors(parts, 1.70f, 1.50f, halfWidth);
            meshes.secondColor = {0.86f, 0.07f, 0.05f};
            meshes.lightBar = {0.0f, 2.6f + ground, 1.0f};
            wheelsAt(0.37f, 0.23f, 0.23f);
            addDashboard(parts, spec.eye, spec.width);
            break;
        }

        case VehicleKind::BoxTruck:
        {
            // The cab is lofted; the cargo box behind it is a plain box.
            Loft cab;
            cab.rearZ = 1.52f;
            cab.frontZ = halfLength;
            cab.width = spec.width - 0.10f;
            cab.corners = {{1.52f, 1.00f}, {1.54f, 2.62f}, {1.66f, 2.70f}, {2.96f, 2.70f},
                           {3.56f, 1.56f}, {3.84f, 1.34f}, {3.95f, 1.02f}};
            cab.roundness = 0.22f;
            cab.sill = 0.62f;
            cab.beltRear = 1.62f;
            cab.beltFront = 1.58f;
            cab.tumblehome = 0.05f;
            cab.planRadius = 0.18f;
            cab.windowFrom = 2.30f;
            cab.windowTo = 2.98f;
            cab.windscreenFrom = 2.9f;
            cab.arches = {{frontAxle, 0.48f}};
            cab.bonnetFrom = 3.0f;
            buildLoft(cab, parts.loft());
            const float boxLength = 1.50f + halfLength;
            parts.second.box({0.0f, 2.16f, 1.50f - 0.5f * boxLength}, {spec.width, 2.28f, boxLength}, 0.0f, 0.05f);
            parts.trim.box({0.0f, 0.82f, 1.50f - 0.5f * boxLength}, {spec.width - 0.5f, 0.42f, boxLength - 0.2f}, 0.0f, 0.0f);
            parts.trim.box({0.0f, 0.52f, -halfLength + 0.08f}, {spec.width - 0.3f, 0.12f, 0.12f});
            // Rear lamps sit low on the box's back.
            Loft back = cab;
            back.rearZ = -halfLength;
            back.width = spec.width;
            back.planRadius = 0.02f;
            for (float side : {-1.0f, 1.0f})
            {
                addLamp(parts.tailLamps, back, side * (halfWidth - 0.22f), 1.18f, {0.26f, 0.14f}, false);
                addLamp(side > 0.0f ? parts.leftIndicators : parts.rightIndicators, back, side * (halfWidth - 0.08f), 1.18f,
                        {0.10f, 0.14f}, false);
                addLamp(parts.headLamps, cab, side * (0.5f * cab.width - 0.30f), 0.96f, {0.32f, 0.16f}, true);
                addLamp(side > 0.0f ? parts.leftIndicators : parts.rightIndicators, cab, side * (0.5f * cab.width - 0.08f),
                        0.96f, {0.10f, 0.12f}, true);
            }
            parts.trim.box({0.0f, 0.55f, halfLength - 0.03f}, {cab.width - 0.1f, 0.22f, 0.12f});
            addMirrors(parts, 3.05f, 2.05f, 0.5f * cab.width);
            meshes.secondColor = {0.93f, 0.93f, 0.90f};
            wheelsAt(0.48f, 0.28f, 0.44f);
            addDashboard(parts, spec.eye, cab.width);
            break;
        }

        case VehicleKind::Bus:
        {
            Loft loft;
            loft.rearZ = -halfLength;
            loft.frontZ = halfLength;
            loft.width = spec.width;
            loft.corners = {{-6.00f, 0.95f}, {-5.98f, 2.98f}, {-5.86f, 3.12f}, {5.55f, 3.12f},
                            {5.80f, 3.00f}, {5.96f, 1.30f}, {6.00f, 0.95f}};
            loft.roundness = 0.3f;
            loft.sill = 0.32f;
            loft.beltRear = 1.30f;
            loft.beltFront = 1.18f;
            loft.tumblehome = 0.04f;
            loft.planRadius = 0.28f;
            loft.windowFrom = -5.75f;
            loft.windowTo = 5.55f;
            for (float z = -4.6f; z < 5.0f; z += 1.35f)
                loft.pillars.push_back({z - 0.06f, z + 0.06f});
            loft.windscreenFrom = 5.5f;
            loft.rearWindowTo = -5.9f;
            loft.arches = {{frontAxle, 0.50f}, {rearAxle, 0.50f}};
            buildLoft(loft, parts.loft());
            // A light roof and band above the windows, an air-conditioning
            // unit, the destination display, and two doors on the kerb side
            // (the driver's right, local -x).
            parts.second.box({0.0f, 3.16f, 0.0f}, {spec.width - 0.3f, 0.05f, spec.length - 0.6f}, 0.0f, 0.0f);
            for (float side : {-1.0f, 1.0f})
                parts.second.box({side * (halfWidth - 0.01f), 2.93f, -0.1f}, {0.03f, 0.16f, spec.length - 0.7f}, 0.0f, 0.0f);
            parts.second.box({0.0f, 3.30f, -3.2f}, {1.5f, 0.24f, 2.2f});
            parts.trim.box({0.0f, 2.80f, halfLength - 0.02f}, {1.70f, 0.28f, 0.06f}, 0.0f, 0.0f);
            addText(parts.sign, "1 CITY LOOP", {0.0f, 2.80f, halfLength + 0.012f}, 0.16f, 0);
            parts.trim.box({0.0f, 2.80f, -halfLength + 0.02f}, {0.50f, 0.28f, 0.06f}, 0.0f, 0.0f);
            addText(parts.sign, "1", {0.0f, 2.80f, -halfLength - 0.012f}, 0.18f, 1);
            for (float z : {4.60f, 0.05f})
            {
                parts.doors.box({-(halfWidth + 0.01f), 1.62f, z}, {0.04f, 2.46f, 1.10f}, 0.0f, 0.0f);
                parts.trim.box({-(halfWidth - 0.02f), 1.62f, z}, {0.03f, 2.56f, 1.22f}, 0.0f, 0.0f);
            }
            addLamps(parts, loft, 0.72f, halfWidth - 0.36f, 0.90f, halfWidth - 0.20f, {0.40f, 0.16f}, {0.18f, 0.34f});
            addBumpers(parts, loft, 0.42f, 0.46f);
            for (float side : {-1.0f, 1.0f})
            {
                parts.trim.box({side * (halfWidth + 0.30f), 2.55f, halfLength - 0.35f}, {0.50f, 0.04f, 0.04f});
                parts.trim.box({side * (halfWidth + 0.52f), 2.30f, halfLength - 0.35f}, {0.08f, 0.40f, 0.14f});
            }
            meshes.secondColor = {0.88f, 0.89f, 0.90f};
            meshes.signColor = {1.0f, 0.58f, 0.06f};
            meshes.signAlwaysLit = true;
            wheelsAt(0.50f, 0.30f, 0.46f);
            addDashboard(parts, spec.eye, spec.width);
            break;
        }

        case VehicleKind::Motorbike:
        {
            // Frame, tank, engine, seat and fork, and the rider: legs,
            // body in a dark jacket, arms to the bars, a helmet in the
            // bike's colour.
            parts.paint.box({0.0f, 0.80f, 0.18f}, {0.30f, 0.24f, 0.62f});
            parts.paint.box({0.0f, 0.72f, -0.74f}, {0.18f, 0.06f, 0.46f});
            parts.trim.box({0.0f, 0.50f, 0.02f}, {0.30f, 0.32f, 0.46f});
            parts.trim.box({0.0f, 0.86f, -0.36f}, {0.26f, 0.10f, 0.58f});
            parts.trim.box({0.0f, 0.62f, 0.62f}, {0.10f, 0.06f, 0.46f}, 0.0f, 0.0f);
            parts.trim.box({0.0f, 1.00f, 0.52f}, {0.66f, 0.04f, 0.04f}, 0.0f, 0.0f);
            parts.second.box({0.0f, 1.20f, -0.22f}, {0.38f, 0.56f, 0.30f});
            for (float side : {-1.0f, 1.0f})
            {
                parts.second.box({side * 0.21f, 1.12f, 0.10f}, {0.10f, 0.10f, 0.52f});
                parts.trim.box({side * 0.17f, 0.82f, -0.10f}, {0.13f, 0.14f, 0.50f});
                parts.trim.box({side * 0.18f, 0.55f, 0.10f}, {0.11f, 0.42f, 0.13f});
            }
            parts.paint.box({0.0f, 1.62f, -0.18f}, {0.28f, 0.30f, 0.32f}, 0.0f, 0.24f);
            parts.glass.box({0.0f, 1.62f, -0.03f}, {0.22f, 0.12f, 0.04f}, 0.0f, 0.0f);
            parts.headLamps.box({0.0f, 0.92f, 0.80f}, {0.16f, 0.14f, 0.08f}, 0.0f, 0.2f);
            parts.tailLamps.box({0.0f, 0.80f, -0.98f}, {0.14f, 0.07f, 0.05f});
            for (float side : {-1.0f, 1.0f})
            {
                Builder& indicators = side > 0.0f ? parts.leftIndicators : parts.rightIndicators;
                indicators.box({side * 0.36f, 1.00f, 0.52f}, {0.06f, 0.05f, 0.06f});
                indicators.box({side * 0.13f, 0.78f, -0.96f}, {0.06f, 0.05f, 0.05f});
            }
            meshes.secondColor = {0.20f, 0.18f, 0.17f};
            meshes.wheels.push_back({{0.0f, 0.31f + ground, 0.70f}, 0.31f, 0.12f, true});
            meshes.wheels.push_back({{0.0f, 0.31f + ground, -0.70f}, 0.31f, 0.15f, false});
            parts.bonnet.box({0.0f, 0.95f, 0.45f}, {0.20f, 0.16f, 0.30f});
            break;
        }
        }

        meshes.paint = parts.paint.build();
        meshes.second = parts.second.build();
        meshes.glass = parts.glass.build();
        meshes.trim = parts.trim.build();
        meshes.headLamps = parts.headLamps.build();
        meshes.tailLamps = parts.tailLamps.build();
        meshes.leftIndicators = parts.leftIndicators.build();
        meshes.rightIndicators = parts.rightIndicators.build();
        meshes.redFlashers = parts.redFlashers.build();
        meshes.blueFlashers = parts.blueFlashers.build();
        meshes.sign = parts.sign.build();
        meshes.doors = parts.doors.build();
        meshes.bonnet = parts.bonnet.build();
        meshes.dashboard = parts.dashboard.build();
    }
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

namespace
{
    // Light bar pattern: red, then blue, each flashing twice, 2.5 times a
    // second. 0 = dark, 1 = red, 2 = blue.
    int flasherState(float seconds, std::size_t id)
    {
        const float phase = std::fmod(seconds * 1.25f + static_cast<float>(id) * 0.37f, 1.0f);
        const float within = std::fmod(phase, 0.5f);
        const bool lit = within < 0.12f || (within > 0.20f && within < 0.32f);
        if (!lit)
            return 0;
        return phase < 0.5f ? 1 : 2;
    }
}

void VehicleRenderer::collect(const VehiclePose& pose, const VehicleLamps& lamps, std::vector<VehiclePart>& parts) const
{
    const KindMeshes& meshes = kinds_[static_cast<std::size_t>(pose.kind)];
    glm::mat4 frame = glm::translate(glm::mat4(1.0f), pose.position);
    frame = glm::rotate(frame, glm::radians(pose.yawDegrees), {0.0f, 1.0f, 0.0f});

    const auto add = [&parts, &frame](const Mesh& mesh, const glm::vec3& color, float shininess,
                                      const glm::vec3& emissive = glm::vec3(0.0f), bool matte = false)
    {
        if (!mesh.empty())
            parts.push_back({&mesh, frame, color, shininess, emissive, matte});
    };

    // A soft dark patch on the road under the body.
    const VehicleSpec& spec = vehicleSpec(pose.kind);
    parts.push_back({&tyre_, glm::scale(glm::translate(frame, {0.0f, ground + 0.012f, 0.0f}),
                                        {spec.width * 0.95f, 0.02f, spec.length * 0.92f}),
                     {0.035f, 0.038f, 0.042f}, 2.0f, glm::vec3(0.0f)});

    add(meshes.paint, pose.color, 70.0f);
    add(meshes.second, meshes.secondColor, 44.0f);
    add(meshes.glass, glassColor, 120.0f);
    add(meshes.trim, trimColor, 26.0f);

    // Lamps. Colours in the source are 0..1; the shader scales emissive up.
    add(meshes.headLamps, lensColor, 90.0f,
        lamps.headlights ? glm::vec3(1.0f, 0.93f, 0.78f) : glm::vec3(0.03f), true);
    const glm::vec3 tail = pose.braking ? glm::vec3(1.0f, 0.04f, 0.02f)
                         : lamps.headlights ? glm::vec3(0.32f, 0.012f, 0.005f) : glm::vec3(0.0f);
    add(meshes.tailLamps, tailColor, 60.0f, tail, true);
    const bool blinkOn = std::fmod(lamps.seconds + static_cast<float>(pose.id) * 0.113f, 0.75f) < 0.40f;
    // Kept below full strength: bright amber would tone-map to white.
    const glm::vec3 amber {0.62f, 0.25f, 0.01f};
    add(meshes.leftIndicators, amberColor, 80.0f, pose.indicator < 0 && blinkOn ? amber : glm::vec3(0.0f), true);
    add(meshes.rightIndicators, amberColor, 80.0f, pose.indicator > 0 && blinkOn ? amber : glm::vec3(0.0f), true);

    const int flash = flasherState(lamps.seconds, pose.id);
    add(meshes.redFlashers, redLensColor, 90.0f, flash == 1 ? glm::vec3(1.0f, 0.06f, 0.04f) : glm::vec3(0.02f, 0.0f, 0.0f));
    add(meshes.blueFlashers, blueLensColor, 90.0f, flash == 2 ? glm::vec3(0.10f, 0.28f, 1.0f) : glm::vec3(0.0f, 0.0f, 0.02f));
    add(meshes.sign, meshes.signColor, 60.0f,
        meshes.signAlwaysLit || lamps.headlights ? meshes.signColor * 0.9f : meshes.signColor * 0.05f);

    if (!meshes.doors.empty())
    {
        // Plug doors: out from the side, then back along it.
        const glm::mat4 open = glm::translate(frame, {-0.12f * pose.doorOpen, 0.0f, -0.62f * pose.doorOpen});
        parts.push_back({&meshes.doors, open, glassColor * 1.4f, 110.0f, glm::vec3(0.0f)});
    }

    // Wheels: hub, steering, rolling. The simulation rolls one size of
    // wheel; each kind turns its own at the matching rate.
    for (const Wheel& wheel : meshes.wheels)
    {
        glm::mat4 hub = glm::translate(frame, wheel.centre);
        if (wheel.steers)
            hub = glm::rotate(hub, glm::radians(pose.steerAngleDegrees), {0.0f, 1.0f, 0.0f});
        hub = glm::rotate(hub, glm::radians(90.0f), {0.0f, 0.0f, 1.0f});
        hub = glm::rotate(hub, glm::radians(pose.wheelAngleDegrees * 0.34f / wheel.radius), {0.0f, 1.0f, 0.0f});
        parts.push_back({&tyre_, glm::scale(hub, {2.0f * wheel.radius, wheel.width, 2.0f * wheel.radius}),
                         tyreColor, 12.0f, glm::vec3(0.0f), true});
        parts.push_back({&hub_, glm::scale(hub, {1.15f * wheel.radius, wheel.width + 0.02f, 1.15f * wheel.radius}),
                         hubColor, 70.0f, glm::vec3(0.0f)});
    }
}

void VehicleRenderer::collectDriverView(VehicleKind kind, const glm::vec3& position, float yawDegrees,
                                        const glm::vec3& color, bool dashboard, std::vector<VehiclePart>& parts) const
{
    const KindMeshes& meshes = kinds_[static_cast<std::size_t>(kind)];
    glm::mat4 frame = glm::translate(glm::mat4(1.0f), position);
    frame = glm::rotate(frame, glm::radians(yawDegrees), {0.0f, 1.0f, 0.0f});
    if (!meshes.bonnet.empty())
        parts.push_back({&meshes.bonnet, frame, color, 76.0f, glm::vec3(0.0f)});
    if (dashboard && !meshes.dashboard.empty())
        parts.push_back({&meshes.dashboard, frame, {0.035f, 0.040f, 0.048f}, 30.0f, glm::vec3(0.0f)});
}

bool VehicleRenderer::lightBarLight(const VehiclePose& pose, float seconds, PointLight& light) const
{
    if (!vehicleSpec(pose.kind).emergency)
        return false;
    const int flash = flasherState(seconds, pose.id);
    const float yaw = glm::radians(pose.yawDegrees);
    const glm::vec3 local = kinds_[static_cast<std::size_t>(pose.kind)].lightBar;
    light.position = pose.position + glm::vec3(local.x * std::cos(yaw) + local.z * std::sin(yaw), local.y,
                                               -local.x * std::sin(yaw) + local.z * std::cos(yaw));
    light.color = flash == 1 ? glm::vec3(2.4f, 0.10f, 0.06f) : flash == 2 ? glm::vec3(0.18f, 0.45f, 2.8f) : glm::vec3(0.0f);
    light.range = 12.0f;
    light.alwaysOn = false;
    return true;
}
