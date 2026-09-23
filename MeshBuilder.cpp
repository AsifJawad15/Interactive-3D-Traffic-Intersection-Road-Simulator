#include "MeshBuilder.h"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

void MeshBuilder::addTriangle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
                              const glm::vec2& uvA, const glm::vec2& uvB, const glm::vec2& uvC,
                              const glm::vec3& expectedNormal)
{
    glm::vec3 normal = glm::cross(b - a, c - a);
    const float length = glm::length(normal);
    if (length < 1.0e-8f)
        return;   // degenerate: nothing to draw
    normal /= length;

    const unsigned int base = static_cast<unsigned int>(data_.vertices.size());
    // Wind every triangle the same way as its intended normal.
    if (glm::dot(normal, expectedNormal) < 0.0f)
    {
        normal = -normal;
        data_.vertices.push_back({a, normal, uvA});
        data_.vertices.push_back({c, normal, uvC});
        data_.vertices.push_back({b, normal, uvB});
    }
    else
    {
        data_.vertices.push_back({a, normal, uvA});
        data_.vertices.push_back({b, normal, uvB});
        data_.vertices.push_back({c, normal, uvC});
    }
    data_.indices.insert(data_.indices.end(), {base, base + 1, base + 2});
}

void MeshBuilder::addFlatTriangle(glm::vec2 a, glm::vec2 b, glm::vec2 c, float y)
{
    addTriangle({a.x, y, a.y}, {b.x, y, b.y}, {c.x, y, c.y}, a, b, c, {0.0f, 1.0f, 0.0f});
}

void MeshBuilder::addFlatPolygon(const std::vector<glm::vec2>& outline, float y, float tile, const glm::vec2* apex)
{
    if (outline.size() < 3)
        return;

    glm::vec2 centroid {0.0f};
    for (const glm::vec2& point : outline)
        centroid += point;
    centroid /= static_cast<float>(outline.size());
    if (apex != nullptr)
        centroid = *apex;

    const glm::vec3 up {0.0f, 1.0f, 0.0f};
    const glm::vec3 middle {centroid.x, y, centroid.y};
    for (std::size_t index = 0; index < outline.size(); ++index)
    {
        const glm::vec2 a = outline[index];
        const glm::vec2 b = outline[(index + 1) % outline.size()];
        addTriangle(middle, {a.x, y, a.y}, {b.x, y, b.y},
                    centroid / tile, a / tile, b / tile, up);
    }
}

void MeshBuilder::addFlatQuad(glm::vec2 a, glm::vec2 b, glm::vec2 c, glm::vec2 d, float y, float tile)
{
    const glm::vec3 up {0.0f, 1.0f, 0.0f};
    addTriangle({a.x, y, a.y}, {b.x, y, b.y}, {c.x, y, c.y}, a / tile, b / tile, c / tile, up);
    addTriangle({a.x, y, a.y}, {c.x, y, c.y}, {d.x, y, d.y}, a / tile, c / tile, d / tile, up);
}

void MeshBuilder::addFlatRing(const std::vector<glm::vec2>& outer, const std::vector<glm::vec2>& inner,
                              float y, float tile)
{
    const std::size_t count = std::min(outer.size(), inner.size());
    for (std::size_t index = 0; index < count; ++index)
    {
        const std::size_t next = (index + 1) % count;
        addFlatQuad(outer[index], outer[next], inner[next], inner[index], y, tile);
    }
}

void MeshBuilder::addRibbon(const std::vector<glm::vec2>& centreLine, float width, float y)
{
    if (centreLine.size() < 2)
        return;
    const float half = width * 0.5f;
    for (std::size_t index = 0; index + 1 < centreLine.size(); ++index)
    {
        const glm::vec2 a = centreLine[index];
        const glm::vec2 b = centreLine[index + 1];
        const glm::vec2 along = b - a;
        const float length = glm::length(along);
        if (length < 1.0e-5f)
            continue;
        const glm::vec2 side = glm::vec2{along.y, -along.x} / length * half;
        addFlatQuad(a - side, b - side, b + side, a + side, y, 1.0f);
    }
}

void MeshBuilder::addPaintRectangle(glm::vec2 centre, glm::vec2 direction, float length, float width, float y)
{
    direction = glm::normalize(direction);
    const glm::vec2 along = direction * (length * 0.5f);
    const glm::vec2 side = glm::vec2{direction.y, -direction.x} * (width * 0.5f);
    addFlatQuad(centre - along - side, centre + along - side, centre + along + side, centre - along + side, y, 1.0f);
}

void MeshBuilder::addWall(const std::vector<glm::vec2>& outline, float y0, float y1, bool closed)
{
    if (outline.size() < 2)
        return;

    // Which way the outline winds decides which side of each edge is out:
    // for a counter-clockwise outline (x right, z up) the inside is on the
    // left of every edge, so the outside is on the right.
    float twiceArea = 0.0f;
    for (std::size_t index = 0; index < outline.size(); ++index)
    {
        const glm::vec2 a = outline[index];
        const glm::vec2 b = outline[(index + 1) % outline.size()];
        twiceArea += a.x * b.y - b.x * a.y;
    }
    const float outwardSign = twiceArea >= 0.0f ? 1.0f : -1.0f;

    const std::size_t segments = closed ? outline.size() : outline.size() - 1;
    for (std::size_t index = 0; index < segments; ++index)
    {
        const glm::vec2 a = outline[index];
        const glm::vec2 b = outline[(index + 1) % outline.size()];
        const glm::vec2 edge = b - a;
        const glm::vec3 outward = glm::vec3 {edge.y, 0.0f, -edge.x} * outwardSign;
        const float length = glm::length(b - a);
        const glm::vec3 a0 {a.x, y0, a.y};
        const glm::vec3 b0 {b.x, y0, b.y};
        const glm::vec3 a1 {a.x, y1, a.y};
        const glm::vec3 b1 {b.x, y1, b.y};
        addTriangle(a0, b0, b1, {0.0f, 0.0f}, {length, 0.0f}, {length, 1.0f}, outward);
        addTriangle(a0, b1, a1, {0.0f, 0.0f}, {length, 1.0f}, {0.0f, 1.0f}, outward);
    }
}

void MeshBuilder::append(const MeshData& data, const glm::mat4& transform)
{
    const glm::mat3 normalMatrix = glm::inverseTranspose(glm::mat3(transform));
    const unsigned int base = static_cast<unsigned int>(data_.vertices.size());
    data_.vertices.reserve(data_.vertices.size() + data.vertices.size());
    for (const Vertex& vertex : data.vertices)
    {
        const glm::vec4 position = transform * glm::vec4(vertex.position, 1.0f);
        data_.vertices.push_back({glm::vec3(position), glm::normalize(normalMatrix * vertex.normal), vertex.texCoord});
    }
    for (unsigned int index : data.indices)
        data_.indices.push_back(base + index);
}
