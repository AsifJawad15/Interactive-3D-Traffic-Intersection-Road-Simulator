#pragma once

#include "Mesh.h"

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <vector>

// Collects geometry in world space and uploads it as ONE mesh. Everything that
// shares a material (all asphalt, all kerbs, all white paint, every lamp post)
// is baked into a single vertex buffer, so the whole city costs a handful of
// draw calls instead of thousands.
//
// Flat pieces get world-planar texture coordinates (x and z divided by the
// tile size). Overlapping pieces of the same material therefore sample the
// same texel at the same place and cannot flicker against each other.
class MeshBuilder
{
public:
    // A flat polygon at height y, seen from above. With an `apex` it is fanned
    // from there, and the outline must be star-shaped around it (every point
    // visible from the apex). Without one, any simple outline works, however
    // concave (an L-shaped block): it is cut into triangles by ear clipping.
    void addFlatPolygon(const std::vector<glm::vec2>& outline, float y, float tile,
                        const glm::vec2* apex = nullptr);

    void addFlatTriangle(glm::vec2 a, glm::vec2 b, glm::vec2 c, float y);

    // A flat quadrilateral, corners in order around it.
    void addFlatQuad(glm::vec2 a, glm::vec2 b, glm::vec2 c, glm::vec2 d, float y, float tile);

    // The flat band between two outlines with matching points (same count,
    // point i of one beside point i of the other), closed into a loop.
    void addFlatRing(const std::vector<glm::vec2>& outer, const std::vector<glm::vec2>& inner, float y, float tile);

    // A flat strip of the given width along a polyline, for painted lines.
    void addRibbon(const std::vector<glm::vec2>& centreLine, float width, float y);

    // A rectangle of paint: `centre`, lying along `direction`.
    void addPaintRectangle(glm::vec2 centre, glm::vec2 direction, float length, float width, float y);

    // Vertical faces along a closed outline from y0 up to y1, facing away
    // from the outline's inside (for kerbs), or towards it with `inward`.
    void addWall(const std::vector<glm::vec2>& outline, float y0, float y1, bool closed = true, bool inward = false);

    // A copy of existing geometry, moved into place.
    void append(const MeshData& data, const glm::mat4& transform);

    bool empty() const { return data_.indices.empty(); }
    Mesh build() const { return Mesh(data_); }

private:
    MeshData data_;

    void addTriangle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
                     const glm::vec2& uvA, const glm::vec2& uvB, const glm::vec2& uvC,
                     const glm::vec3& expectedNormal);
};
