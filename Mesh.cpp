#include "Mesh.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <cmath>
#include <numbers>
#include <utility>

namespace
{
    void addTriangle(
        std::vector<Vertex>& vertices,
        std::vector<unsigned int>& indices,
        glm::vec3 a,
        glm::vec3 b,
        glm::vec3 c,
        const glm::vec3& expectedNormal,
        const glm::vec2& uvA = {0.0f, 0.0f},
        const glm::vec2& uvB = {1.0f, 0.0f},
        const glm::vec2& uvC = {0.5f, 1.0f})
    {
        glm::vec3 normal = glm::normalize(glm::cross(b - a, c - a));
        if (glm::dot(normal, expectedNormal) < 0.0f)
        {
            std::swap(b, c);
            normal = -normal;
        }

        const unsigned int base = static_cast<unsigned int>(vertices.size());
        vertices.push_back({a, normal, uvA});
        vertices.push_back({b, normal, uvB});
        vertices.push_back({c, normal, uvC});
        indices.insert(indices.end(), {base, base + 1, base + 2});
    }

    void addQuad(
        std::vector<Vertex>& vertices,
        std::vector<unsigned int>& indices,
        glm::vec3 a,
        glm::vec3 b,
        glm::vec3 c,
        glm::vec3 d,
        const glm::vec3& expectedNormal)
    {
        glm::vec3 normal = glm::normalize(glm::cross(b - a, c - a));
        if (glm::dot(normal, expectedNormal) < 0.0f)
        {
            std::swap(b, d);
            normal = -normal;
        }

        const unsigned int base = static_cast<unsigned int>(vertices.size());
        vertices.push_back({a, normal, {0.0f, 0.0f}});
        vertices.push_back({b, normal, {1.0f, 0.0f}});
        vertices.push_back({c, normal, {1.0f, 1.0f}});
        vertices.push_back({d, normal, {0.0f, 1.0f}});
        indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
}

Mesh::Mesh(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices)
    : indexCount_(static_cast<GLsizei>(indices.size()))
{
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);

    glBindVertexArray(vao_);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)),
        vertices.data(),
        GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(indices.size() * sizeof(unsigned int)),
        indices.data(),
        GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, normal)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, texCoord)));

    glBindVertexArray(0);
}

Mesh::~Mesh()
{
    release();
}

Mesh::Mesh(Mesh&& other) noexcept
    : vao_(std::exchange(other.vao_, 0)),
      vbo_(std::exchange(other.vbo_, 0)),
      ebo_(std::exchange(other.ebo_, 0)),
      indexCount_(std::exchange(other.indexCount_, 0))
{
}

Mesh& Mesh::operator=(Mesh&& other) noexcept
{
    if (this != &other)
    {
        release();
        vao_ = std::exchange(other.vao_, 0);
        vbo_ = std::exchange(other.vbo_, 0);
        ebo_ = std::exchange(other.ebo_, 0);
        indexCount_ = std::exchange(other.indexCount_, 0);
    }
    return *this;
}

void Mesh::draw() const
{
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

Mesh Mesh::makeCube()
{
    const std::vector<Vertex> vertices = {
        {{-0.5f,-0.5f, 0.5f},{ 0, 0, 1},{0,0}}, {{ 0.5f,-0.5f, 0.5f},{ 0, 0, 1},{1,0}},
        {{ 0.5f, 0.5f, 0.5f},{ 0, 0, 1},{1,1}}, {{-0.5f, 0.5f, 0.5f},{ 0, 0, 1},{0,1}},
        {{ 0.5f,-0.5f,-0.5f},{ 0, 0,-1},{0,0}}, {{-0.5f,-0.5f,-0.5f},{ 0, 0,-1},{1,0}},
        {{-0.5f, 0.5f,-0.5f},{ 0, 0,-1},{1,1}}, {{ 0.5f, 0.5f,-0.5f},{ 0, 0,-1},{0,1}},
        {{-0.5f,-0.5f,-0.5f},{-1, 0, 0},{0,0}}, {{-0.5f,-0.5f, 0.5f},{-1, 0, 0},{1,0}},
        {{-0.5f, 0.5f, 0.5f},{-1, 0, 0},{1,1}}, {{-0.5f, 0.5f,-0.5f},{-1, 0, 0},{0,1}},
        {{ 0.5f,-0.5f, 0.5f},{ 1, 0, 0},{0,0}}, {{ 0.5f,-0.5f,-0.5f},{ 1, 0, 0},{1,0}},
        {{ 0.5f, 0.5f,-0.5f},{ 1, 0, 0},{1,1}}, {{ 0.5f, 0.5f, 0.5f},{ 1, 0, 0},{0,1}},
        {{-0.5f, 0.5f, 0.5f},{ 0, 1, 0},{0,0}}, {{ 0.5f, 0.5f, 0.5f},{ 0, 1, 0},{1,0}},
        {{ 0.5f, 0.5f,-0.5f},{ 0, 1, 0},{1,1}}, {{-0.5f, 0.5f,-0.5f},{ 0, 1, 0},{0,1}},
        {{-0.5f,-0.5f,-0.5f},{ 0,-1, 0},{0,0}}, {{ 0.5f,-0.5f,-0.5f},{ 0,-1, 0},{1,0}},
        {{ 0.5f,-0.5f, 0.5f},{ 0,-1, 0},{1,1}}, {{-0.5f,-0.5f, 0.5f},{ 0,-1, 0},{0,1}}
    };

    std::vector<unsigned int> indices;
    indices.reserve(36);
    for (unsigned int face = 0; face < 6; ++face)
    {
        const unsigned int base = face * 4;
        indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }

    return Mesh(vertices, indices);
}

Mesh Mesh::makeBeveledCube(float bevel)
{
    bevel = glm::clamp(bevel, 0.001f, 0.24f);
    const float h = 0.5f - bevel;
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    vertices.reserve(120);
    indices.reserve(180);

    addQuad(vertices, indices, { 0.5f,-h,-h}, { 0.5f, h,-h}, { 0.5f, h, h}, { 0.5f,-h, h}, { 1, 0, 0});
    addQuad(vertices, indices, {-0.5f,-h, h}, {-0.5f, h, h}, {-0.5f, h,-h}, {-0.5f,-h,-h}, {-1, 0, 0});
    addQuad(vertices, indices, {-h, 0.5f, h}, { h, 0.5f, h}, { h, 0.5f,-h}, {-h, 0.5f,-h}, { 0, 1, 0});
    addQuad(vertices, indices, {-h,-0.5f,-h}, { h,-0.5f,-h}, { h,-0.5f, h}, {-h,-0.5f, h}, { 0,-1, 0});
    addQuad(vertices, indices, {-h,-h, 0.5f}, { h,-h, 0.5f}, { h, h, 0.5f}, {-h, h, 0.5f}, { 0, 0, 1});
    addQuad(vertices, indices, { h,-h,-0.5f}, {-h,-h,-0.5f}, {-h, h,-0.5f}, { h, h,-0.5f}, { 0, 0,-1});

    for (float sx : {-1.0f, 1.0f})
    {
        for (float sy : {-1.0f, 1.0f})
        {
            addQuad(vertices, indices,
                {sx * 0.5f, sy * h, -h}, {sx * 0.5f, sy * h, h},
                {sx * h, sy * 0.5f, h}, {sx * h, sy * 0.5f, -h},
                glm::normalize(glm::vec3{sx, sy, 0.0f}));
        }
    }

    for (float sx : {-1.0f, 1.0f})
    {
        for (float sz : {-1.0f, 1.0f})
        {
            addQuad(vertices, indices,
                {sx * 0.5f, -h, sz * h}, {sx * h, -h, sz * 0.5f},
                {sx * h, h, sz * 0.5f}, {sx * 0.5f, h, sz * h},
                glm::normalize(glm::vec3{sx, 0.0f, sz}));
        }
    }

    for (float sy : {-1.0f, 1.0f})
    {
        for (float sz : {-1.0f, 1.0f})
        {
            addQuad(vertices, indices,
                {-h, sy * 0.5f, sz * h}, {h, sy * 0.5f, sz * h},
                {h, sy * h, sz * 0.5f}, {-h, sy * h, sz * 0.5f},
                glm::normalize(glm::vec3{0.0f, sy, sz}));
        }
    }

    for (float sx : {-1.0f, 1.0f})
    {
        for (float sy : {-1.0f, 1.0f})
        {
            for (float sz : {-1.0f, 1.0f})
            {
                addTriangle(vertices, indices,
                    {sx * 0.5f, sy * h, sz * h},
                    {sx * h, sy * 0.5f, sz * h},
                    {sx * h, sy * h, sz * 0.5f},
                    glm::normalize(glm::vec3{sx, sy, sz}));
            }
        }
    }

    return Mesh(vertices, indices);
}

Mesh Mesh::makeCarCabin()
{
    const float bottomX = 0.5f;
    const float topX = 0.38f;
    const float bottomFront = 0.5f;
    const float bottomRear = -0.5f;
    const float topFront = 0.27f;
    const float topRear = -0.33f;

    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    vertices.reserve(24);
    indices.reserve(36);

    addQuad(vertices, indices,
        {-bottomX, -0.5f, bottomFront}, {bottomX, -0.5f, bottomFront},
        {topX, 0.5f, topFront}, {-topX, 0.5f, topFront}, {0, 0.25f, 1});
    addQuad(vertices, indices,
        {bottomX, -0.5f, bottomRear}, {-bottomX, -0.5f, bottomRear},
        {-topX, 0.5f, topRear}, {topX, 0.5f, topRear}, {0, 0.2f, -1});
    addQuad(vertices, indices,
        {bottomX, -0.5f, bottomFront}, {bottomX, -0.5f, bottomRear},
        {topX, 0.5f, topRear}, {topX, 0.5f, topFront}, {1, 0.15f, 0});
    addQuad(vertices, indices,
        {-bottomX, -0.5f, bottomRear}, {-bottomX, -0.5f, bottomFront},
        {-topX, 0.5f, topFront}, {-topX, 0.5f, topRear}, {-1, 0.15f, 0});
    addQuad(vertices, indices,
        {-topX, 0.5f, topFront}, {topX, 0.5f, topFront},
        {topX, 0.5f, topRear}, {-topX, 0.5f, topRear}, {0, 1, 0});
    addQuad(vertices, indices,
        {-bottomX, -0.5f, bottomRear}, {bottomX, -0.5f, bottomRear},
        {bottomX, -0.5f, bottomFront}, {-bottomX, -0.5f, bottomFront}, {0, -1, 0});

    return Mesh(vertices, indices);
}

Mesh Mesh::makeCylinder(unsigned int segments)
{
    segments = segments < 3 ? 3 : segments;

    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    vertices.reserve((segments + 1) * 4 + 2);

    for (unsigned int i = 0; i <= segments; ++i)
    {
        const float u = static_cast<float>(i) / static_cast<float>(segments);
        const float angle = u * 2.0f * std::numbers::pi_v<float>;
        const float x = std::cos(angle) * 0.5f;
        const float z = std::sin(angle) * 0.5f;
        const glm::vec3 normal = glm::normalize(glm::vec3(x, 0.0f, z));
        vertices.push_back({{x, -0.5f, z}, normal, {u, 0.0f}});
        vertices.push_back({{x,  0.5f, z}, normal, {u, 1.0f}});
    }

    for (unsigned int i = 0; i < segments; ++i)
    {
        const unsigned int base = i * 2;
        indices.insert(indices.end(), {base, base + 1, base + 3, base, base + 3, base + 2});
    }

    const unsigned int topCenter = static_cast<unsigned int>(vertices.size());
    vertices.push_back({{0, 0.5f, 0}, {0, 1, 0}, {0.5f, 0.5f}});
    const unsigned int topRing = static_cast<unsigned int>(vertices.size());
    for (unsigned int i = 0; i <= segments; ++i)
    {
        const float angle = static_cast<float>(i) / segments * 2.0f * std::numbers::pi_v<float>;
        const float x = std::cos(angle) * 0.5f;
        const float z = std::sin(angle) * 0.5f;
        vertices.push_back({{x, 0.5f, z}, {0, 1, 0}, {x + 0.5f, z + 0.5f}});
    }

    const unsigned int bottomCenter = static_cast<unsigned int>(vertices.size());
    vertices.push_back({{0, -0.5f, 0}, {0, -1, 0}, {0.5f, 0.5f}});
    const unsigned int bottomRing = static_cast<unsigned int>(vertices.size());
    for (unsigned int i = 0; i <= segments; ++i)
    {
        const float angle = static_cast<float>(i) / segments * 2.0f * std::numbers::pi_v<float>;
        const float x = std::cos(angle) * 0.5f;
        const float z = std::sin(angle) * 0.5f;
        vertices.push_back({{x, -0.5f, z}, {0, -1, 0}, {x + 0.5f, z + 0.5f}});
    }

    for (unsigned int i = 0; i < segments; ++i)
    {
        indices.insert(indices.end(), {topCenter, topRing + i, topRing + i + 1});
        indices.insert(indices.end(), {bottomCenter, bottomRing + i + 1, bottomRing + i});
    }

    return Mesh(vertices, indices);
}

void Mesh::release()
{
    if (ebo_ != 0)
        glDeleteBuffers(1, &ebo_);
    if (vbo_ != 0)
        glDeleteBuffers(1, &vbo_);
    if (vao_ != 0)
        glDeleteVertexArrays(1, &vao_);
    vao_ = vbo_ = ebo_ = 0;
    indexCount_ = 0;
}
