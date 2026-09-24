#include "PropRenderer.h"

#include "MeshBuilder.h"
#include "NeonText.h"
#include "TreeGenerator.h"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>

namespace
{
    constexpr float ground = RoadNetwork::kerbTopY;

    glm::vec2 facingVector(float degrees)
    {
        const float a = glm::radians(degrees);
        return {std::sin(a), std::cos(a)};
    }

    // Local frame of something standing at `foot` (height `y`), turned to
    // face along the facing angle: local +z points that way, local +x is on
    // the right of someone looking at its front.
    glm::mat4 frameAt(glm::vec2 foot, float y, float facingDegrees)
    {
        return glm::rotate(glm::translate(glm::mat4(1.0f), {foot.x, y, foot.y}), glm::radians(facingDegrees), {0.0f, 1.0f, 0.0f});
    }

    glm::vec3 place(const glm::mat4& frame, const glm::vec3& local)
    {
        return glm::vec3(frame * glm::vec4(local, 1.0f));
    }

    // A box `size` big centred at `centre` in `frame`, in `color`.
    void box(MeshBuilder& builder, const glm::mat4& frame, const glm::vec3& centre, const glm::vec3& size,
             const glm::vec3& color = glm::vec3{1.0f}, float bevel = 0.04f)
    {
        static const MeshData sharp = Mesh::beveledCubeData(0.02f);
        static const MeshData soft = Mesh::beveledCubeData(0.08f);
        builder.setColor(color);
        builder.append(bevel > 0.05f ? soft : sharp, glm::scale(glm::translate(frame, centre), size));
        builder.setColor(glm::vec3{1.0f});
    }

    // A vertical wall from `left` to `right` (as seen from outside) between
    // two heights; texture coordinates run from uv0 to uv1.
    void wall(MeshBuilder& builder, glm::vec2 left, glm::vec2 right, float y0, float y1, glm::vec2 uv0, glm::vec2 uv1)
    {
        builder.addQuad({left.x, y0, left.y}, {right.x, y0, right.y}, {right.x, y1, right.y}, {left.x, y1, left.y},
                        {uv0.x, uv0.y}, {uv1.x, uv0.y}, {uv1.x, uv1.y}, {uv0.x, uv1.y});
    }

    std::uint32_t hashString(const std::string& text)
    {
        std::uint32_t value = 2166136261u;
        for (char character : text)
            value = (value ^ static_cast<unsigned char>(character)) * 16777619u;
        return value;
    }

    // Bezier profiles (radius, height) for the Lab 5 surfaces of revolution
    // among the props.
    const std::vector<glm::vec2>& tankRoofProfile()
    {
        static const std::vector<glm::vec2> profile = {{1.08f, 0.0f}, {0.6f, 0.28f}, {0.0f, 0.5f}};
        return profile;
    }
    const std::vector<glm::vec2>& shrubProfile()
    {
        static const std::vector<glm::vec2> profile = {{0.0f, 0.0f}, {0.78f, 0.08f}, {0.86f, 0.6f}, {0.42f, 1.0f}, {0.0f, 1.08f}};
        return profile;
    }
    const std::vector<glm::vec2>& binProfile()
    {
        static const std::vector<glm::vec2> profile = {{0.22f, 0.0f}, {0.27f, 0.5f}, {0.3f, 0.92f}, {0.32f, 0.98f}, {0.0f, 1.0f}};
        return profile;
    }
    const std::vector<glm::vec2>& bollardProfile()
    {
        static const std::vector<glm::vec2> profile = {{0.11f, 0.0f}, {0.1f, 0.7f}, {0.11f, 0.85f}, {0.07f, 0.98f}, {0.0f, 1.0f}};
        return profile;
    }
    const std::vector<glm::vec2>& statueProfile()
    {
        // An abstract bronze flame on the plaza's pedestal.
        static const std::vector<glm::vec2> profile = {
            {0.55f, 0.0f}, {0.22f, 0.6f}, {0.2f, 1.4f}, {0.75f, 2.0f}, {0.5f, 2.7f}, {0.05f, 3.4f}};
        return profile;
    }
    const std::vector<glm::vec2>& discProfile()
    {
        static const std::vector<glm::vec2> profile = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
        return profile;
    }

    // Materials being collected, one builder each.
    struct Builders
    {
        std::array<MeshBuilder, buildingStyleCount> facades;
        MeshBuilder plainWalls, roofs, tiledRoofs, shopGlass, darkMetal, painted, signBoards, letters;
        MeshBuilder canopyLights, concrete, shrubs, bronze, water, paving, asphalt, bayPaint, bark, leaves;
        MeshBuilder parkedBodies, parkedGlass, parkedDark, parkedLenses, skylineRoofs;
    };

    // ---- Buildings ------------------------------------------------------------

    void buildBuilding(Builders& out, const Building& building)
    {
        const FacadeLook& look = PropRenderer::facadeLook(building.style);
        const glm::vec2 centre {building.position.x, building.position.z};
        const glm::vec2 half {0.5f * building.size.x, 0.5f * building.size.z};
        const bool house = building.style == BuildingStyle::House;
        const bool far = building.style == BuildingStyle::Skyline;
        const float floorsTop = building.groundFloor + static_cast<float>(building.floors) * building.storey;
        const float top = building.size.y;
        const glm::vec3 tint = building.tint;
        MeshBuilder& facade = out.facades[static_cast<std::size_t>(building.style)];

        for (const glm::vec2 normal : {glm::vec2{1.0f, 0.0f}, glm::vec2{0.0f, 1.0f}, glm::vec2{-1.0f, 0.0f}, glm::vec2{0.0f, -1.0f}})
        {
            const glm::vec2 right {normal.y, -normal.x};
            const bool alongX = std::abs(normal.x) > 0.5f;
            const float reach = alongX ? half.x : half.y;
            const float across = alongX ? half.y : half.x;
            const glm::vec2 left = centre + normal * reach - right * across;
            const glm::vec2 rightEnd = centre + normal * reach + right * across;
            const float width = 2.0f * across;

            // The windows: whole bays across, whole storeys up.
            const float bays = std::max(1.0f, std::round(width / look.bayWidth));
            facade.setColor(tint, building.seed);
            wall(facade, left, rightEnd, building.groundFloor, floorsTop, {0.0f, 0.0f}, {bays, static_cast<float>(building.floors)});
            facade.setColor(glm::vec3{1.0f});

            // The plain band of a glazed ground floor (the shop fronts are
            // drawn over it on the front) and the parapet round the roof.
            out.plainWalls.setColor(tint * 0.86f);
            if (building.groundFloor > 0.0f)
                wall(out.plainWalls, left, rightEnd, 0.0f, building.groundFloor, {0.0f, 0.0f}, {width, building.groundFloor});
            if (!house && top > floorsTop)
            {
                wall(out.plainWalls, left, rightEnd, floorsTop, top, {0.0f, 0.0f}, {width, top - floorsTop});
                if (!far)
                {
                    // Its inside face and its top, round a recessed roof.
                    const glm::vec2 innerLeft = centre + normal * (reach - 0.3f) - right * (across - 0.3f);
                    const glm::vec2 innerRight = centre + normal * (reach - 0.3f) + right * (across - 0.3f);
                    out.plainWalls.setColor(tint * 0.7f);
                    wall(out.plainWalls, innerRight, innerLeft, floorsTop + 0.1f, top, {0.0f, 0.0f}, {width, 0.5f});
                    out.plainWalls.setColor(tint * 0.95f);
                    out.plainWalls.addQuad({left.x, top, left.y}, {rightEnd.x, top, rightEnd.y}, {innerRight.x, top, innerRight.y},
                                           {innerLeft.x, top, innerLeft.y}, {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f});
                }
            }
            // A dark plinth round the foot, as the first buildings had.
            if (!far)
            {
                const glm::vec2 plinthLeft = left + normal * 0.12f - right * 0.12f;
                const glm::vec2 plinthRight = rightEnd + normal * 0.12f + right * 0.12f;
                out.plainWalls.setColor(glm::vec3{0.24f, 0.25f, 0.27f});
                wall(out.plainWalls, plinthLeft, plinthRight, 0.0f, ground + 0.34f, {0.0f, 0.0f}, {width, 0.5f});
                out.plainWalls.addQuad({plinthLeft.x, ground + 0.34f, plinthLeft.y}, {plinthRight.x, ground + 0.34f, plinthRight.y},
                                       {rightEnd.x, ground + 0.34f, rightEnd.y}, {left.x, ground + 0.34f, left.y},
                                       {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f});
            }
            out.plainWalls.setColor(glm::vec3{1.0f});
        }

        const glm::mat4 frame = frameAt(centre, 0.0f, building.facingDegrees);
        const float frontWidth = building.frontWidth();
        const float depth = building.depth();

        if (house)
        {
            // A pitched roof, its ridge parallel to the street, and gables.
            const float overhang = 0.4f;
            const float ridge = top + (0.5f * depth + overhang) * std::tan(glm::radians(32.0f));
            const float x = 0.5f * frontWidth + overhang;
            const float z = 0.5f * depth + overhang;
            static const std::array<glm::vec3, 3> roofColors = {glm::vec3{0.62f, 0.26f, 0.18f}, glm::vec3{0.30f, 0.32f, 0.36f},
                                                               glm::vec3{0.46f, 0.30f, 0.22f}};
            out.tiledRoofs.setColor(roofColors[static_cast<std::size_t>(building.seed * 2.99f)]);
            for (float side : {1.0f, -1.0f})
            {
                out.tiledRoofs.addQuad(place(frame, {-x * side, top - 0.08f, z * side}), place(frame, {x * side, top - 0.08f, z * side}),
                                       place(frame, {x * side, ridge, 0.0f}), place(frame, {-x * side, ridge, 0.0f}),
                                       {0.0f, 0.0f}, {2.0f * x, 0.0f}, {2.0f * x, z}, {0.0f, z});
            }
            out.tiledRoofs.setColor(glm::vec3{1.0f});
            out.plainWalls.setColor(tint);
            for (float side : {1.0f, -1.0f})
            {
                const float gx = side * 0.5f * frontWidth;
                const glm::vec3 a = place(frame, {gx, top, side * 0.5f * depth});
                const glm::vec3 b = place(frame, {gx, top, -side * 0.5f * depth});
                const glm::vec3 c = place(frame, {gx, ridge - 0.05f, 0.0f});
                out.plainWalls.addQuad(a, b, c, c, {0.0f, 0.0f}, {1.0f, 0.0f}, {0.5f, 1.0f}, {0.5f, 1.0f});
            }
            out.plainWalls.setColor(glm::vec3{1.0f});
            if (building.seed > 0.4f)
                box(out.plainWalls, frame, {0.25f * frontWidth, ridge - 0.2f, -0.2f * depth}, {0.7f, 1.6f, 0.7f}, {0.55f, 0.36f, 0.30f});
            // The front door, off to one side, under a small porch roof.
            const float doorX = -0.22f * frontWidth;
            box(out.darkMetal, frame, {doorX, ground + 1.05f, 0.5f * depth + 0.05f}, {1.0f, 2.1f, 0.1f}, {0.36f, 0.22f, 0.14f});
            box(out.painted, frame, {doorX, 2.45f, 0.5f * depth + 0.55f}, {1.8f, 0.1f, 1.1f}, {0.9f, 0.9f, 0.88f});
            return;
        }
        if (far)
        {
            // Skyline towers: a plain roof, no detail anyone could see.
            out.skylineRoofs.setColor(tint * 0.7f);
            out.skylineRoofs.addFlatQuad(centre - half, {centre.x + half.x, centre.y - half.y}, centre + half,
                                         {centre.x - half.x, centre.y + half.y}, top, 4.0f);
            out.skylineRoofs.setColor(glm::vec3{1.0f});
            return;
        }

        // The recessed roof, gravel grey.
        {
            const glm::vec2 low = centre - half + 0.3f;
            const glm::vec2 high = centre + half - 0.3f;
            out.roofs.setColor(glm::vec3{0.8f + 0.2f * building.seed});
            out.roofs.addFlatQuad(low, {high.x, low.y}, high, {low.x, high.y}, floorsTop + 0.1f, 3.0f);
            out.roofs.setColor(glm::vec3{1.0f});
        }

        // The entrance of a block of flats: a door and a canopy over it.
        if (building.style == BuildingStyle::Apartment)
        {
            box(out.darkMetal, frame, {0.0f, ground + 1.2f, 0.5f * depth + 0.06f}, {1.7f, 2.4f, 0.12f}, {0.30f, 0.24f, 0.20f});
            box(out.painted, frame, {0.0f, 2.85f, 0.5f * depth + 0.65f}, {2.6f, 0.14f, 1.3f}, {0.82f, 0.83f, 0.85f});
        }
        // The roller doors of a warehouse.
        if (building.style == BuildingStyle::Warehouse)
        {
            for (float side : {-0.25f, 0.25f})
            {
                box(out.painted, frame, {side * frontWidth, ground + 2.3f, 0.5f * depth + 0.06f}, {4.2f, 4.6f, 0.12f},
                    {0.58f, 0.62f, 0.66f});
                box(out.darkMetal, frame, {side * frontWidth, ground + 4.7f, 0.5f * depth + 0.12f}, {4.5f, 0.3f, 0.2f});
            }
        }

        // Rooftop plant: a water tank on legs, air-conditioning units.
        const float roof = floorsTop + 0.1f;
        if ((building.roof & RoofTank) != 0u)
        {
            static const MeshData tank = Mesh::cylinderData(16);
            static const MeshData tankRoof = Mesh::bezierRevolutionData(tankRoofProfile(), 3, 16);
            const glm::vec3 spot {-0.5f * frontWidth + 2.2f, roof, -0.5f * depth + 2.2f};
            for (float lx : {-0.7f, 0.7f})
            {
                for (float lz : {-0.7f, 0.7f})
                    box(out.darkMetal, frame, spot + glm::vec3{lx, 0.5f, lz}, {0.12f, 1.0f, 0.12f});
            }
            out.painted.setColor(glm::vec3{0.72f, 0.74f, 0.76f});
            out.painted.append(tank, glm::scale(glm::translate(frame, spot + glm::vec3{0.0f, 1.8f, 0.0f}), {1.7f, 1.6f, 1.7f}));
            out.painted.append(tankRoof, glm::scale(glm::translate(frame, spot + glm::vec3{0.0f, 2.6f, 0.0f}), glm::vec3{0.8f}));
            out.painted.setColor(glm::vec3{1.0f});
        }
        if ((building.roof & RoofAirCon) != 0u)
        {
            const int units = frontWidth > 16.0f ? 3 : 2;
            for (int unit = 0; unit < units; ++unit)
            {
                const glm::vec3 spot {0.5f * frontWidth - 2.0f - 1.9f * static_cast<float>(unit), roof, -0.5f * depth + 2.0f};
                box(out.painted, frame, spot + glm::vec3{0.0f, 0.5f, 0.0f}, {1.5f, 1.0f, 1.1f}, {0.78f, 0.79f, 0.8f});
                box(out.darkMetal, frame, spot + glm::vec3{0.0f, 1.02f, 0.0f}, {0.9f, 0.06f, 0.9f});
            }
        }
    }

    // ---- Shop fronts ------------------------------------------------------------

    void buildShop(Builders& out, const Shop& shop, float groundFloor)
    {
        const glm::mat4 frame = frameAt(shop.centre, 0.0f, shop.facingDegrees);
        const float width = shop.width;
        const bool lobby = !shop.awning;
        const float glassTop = lobby ? std::min(groundFloor - 0.7f, 3.8f) : 2.9f;
        const float glassHeight = glassTop - (ground + 0.32f);

        // A warm, neutral or cool light inside, told apart at night.
        static const std::array<glm::vec3, 3> interiors = {glm::vec3{1.0f, 0.80f, 0.52f}, glm::vec3{1.0f, 0.94f, 0.82f},
                                                          glm::vec3{0.82f, 0.9f, 1.0f}};
        const glm::vec3 interior = interiors[hashString(shop.name + std::to_string(static_cast<int>(shop.centre.x))) % 3u];

        box(out.darkMetal, frame, {0.0f, ground + 0.16f, 0.07f}, {width - 0.3f, 0.32f, 0.14f}, glm::vec3{0.8f});
        box(out.shopGlass, frame, {0.0f, ground + 0.32f + 0.5f * glassHeight, 0.05f}, {width - 0.5f, glassHeight, 0.06f}, interior);
        box(out.darkMetal, frame, {0.0f, glassTop + 0.06f, 0.08f}, {width - 0.3f, 0.12f, 0.16f});
        for (float side : {-1.0f, 1.0f})
            box(out.darkMetal, frame, {side * (0.5f * width - 0.2f), 0.5f * (glassTop + ground), 0.08f}, {0.16f, glassTop - ground, 0.16f});
        // Mullions, and the door frame in the right-hand third.
        const int panes = std::max(1, static_cast<int>((width - 0.5f) / 1.9f));
        for (int pane = 1; pane < panes; ++pane)
        {
            const float x = -0.5f * (width - 0.5f) + (width - 0.5f) * static_cast<float>(pane) / static_cast<float>(panes);
            box(out.darkMetal, frame, {x, 0.5f * (glassTop + ground), 0.09f}, {0.07f, glassTop - ground, 0.1f});
        }
        const float doorX = 0.5f * width - 1.3f;
        for (float side : {-0.55f, 0.55f})
            box(out.darkMetal, frame, {doorX + side, ground + 1.15f, 0.11f}, {0.09f, 2.3f, 0.1f});
        box(out.darkMetal, frame, {doorX, ground + 2.3f, 0.11f}, {1.2f, 0.09f, 0.1f});
        box(out.darkMetal, frame, {doorX - 0.4f, ground + 1.1f, 0.16f}, {0.05f, 0.5f, 0.05f}, glm::vec3{2.5f});

        if (shop.awning)
        {
            // A striped awning sloping out over the forecourt, with a valance.
            const float awningWidth = width - 0.3f;
            const int stripes = std::max(4, static_cast<int>(std::round(awningWidth / 0.55f)));
            const float stripe = awningWidth / static_cast<float>(stripes);
            const float y0 = 3.2f;
            const float y1 = 2.72f;
            const float reach = 1.6f;
            const glm::vec3 cream {0.95f, 0.93f, 0.86f};
            for (int index = 0; index < stripes; ++index)
            {
                const float x0 = -0.5f * awningWidth + stripe * static_cast<float>(index);
                const float x1 = x0 + stripe;
                out.painted.setColor(index % 2 == 0 ? shop.color : cream);
                out.painted.addQuad(place(frame, {x0, y1, reach}), place(frame, {x1, y1, reach}), place(frame, {x1, y0, 0.12f}),
                                    place(frame, {x0, y0, 0.12f}), {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f});
                out.painted.addQuad(place(frame, {x0, y1 - 0.26f, reach}), place(frame, {x1, y1 - 0.26f, reach}),
                                    place(frame, {x1, y1, reach}), place(frame, {x0, y1, reach}),
                                    {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f});
            }
            out.painted.setColor(glm::vec3{1.0f});
        }
        else if (shop.name.empty())
        {
            // A hotel's lobby: a deep canopy over the door.
            box(out.painted, frame, {0.0f, glassTop + 0.35f, 1.4f}, {width - 1.0f, 0.22f, 2.8f}, shop.color);
        }

        if (!shop.neon && !shop.name.empty())
        {
            // A lit sign box with the shop's name in white.
            const float letterHeight = lobby ? 0.46f : 0.4f;
            const std::vector<TextStroke> strokes = textStrokes(shop.name);
            const glm::vec2 extent = textExtent(strokes);
            const float textWidth = extent.x * letterHeight / std::max(extent.y, 1.0f);
            const float boardWidth = std::min(width - 0.5f, textWidth + 0.9f);
            const float y = lobby ? glassTop + 0.6f : 3.72f;
            box(out.signBoards, frame, {0.0f, y, 0.1f}, {boardWidth, letterHeight + 0.34f, 0.16f}, shop.color);
            appendText(out.letters, shop.name, glm::translate(frame, {0.0f, y, 0.18f}), letterHeight, 0.03f);
        }
    }

    // ---- Street furniture -----------------------------------------------------

    void buildProp(Builders& out, const Prop& prop)
    {
        static const MeshData bin = Mesh::bezierRevolutionData(binProfile(), 6, 12);
        static const MeshData bollard = Mesh::bezierRevolutionData(bollardProfile(), 6, 10);
        static const MeshData shrub = Mesh::bezierRevolutionData(shrubProfile(), 8, 12);
        static const MeshData disc = Mesh::bezierRevolutionData(discProfile(), 2, 24);
        const glm::mat4 frame = frameAt(prop.position, ground, prop.facingDegrees);
        const glm::vec3 wood {0.62f, 0.42f, 0.26f};
        switch (prop.kind)
        {
        case PropKind::Bench:
            for (float z : {-0.16f, 0.0f, 0.16f})
                box(out.painted, frame, {0.0f, 0.45f, z}, {1.8f, 0.05f, 0.13f}, wood);
            for (float y : {0.66f, 0.84f})
                box(out.painted, frame, {0.0f, y, -0.27f}, {1.8f, 0.12f, 0.04f}, wood);
            for (float x : {-0.75f, 0.75f})
            {
                box(out.darkMetal, frame, {x, 0.22f, 0.0f}, {0.06f, 0.44f, 0.5f});
                box(out.darkMetal, frame, {x, 0.62f, -0.27f}, {0.06f, 0.4f, 0.05f});
            }
            break;
        case PropKind::Bin:
            out.painted.setColor(glm::vec3{0.16f, 0.32f, 0.22f});
            out.painted.append(bin, frame);
            out.painted.setColor(glm::vec3{1.0f});
            break;
        case PropKind::Bollard:
            out.painted.setColor(glm::vec3{0.22f, 0.23f, 0.25f});
            out.painted.append(bollard, frame);
            out.painted.setColor(glm::vec3{1.0f});
            break;
        case PropKind::Planter:
            box(out.concrete, frame, {0.0f, 0.3f, 0.0f}, {1.7f, 0.6f, 0.8f}, glm::vec3{1.0f}, 0.08f);
            out.shrubs.append(shrub, glm::scale(glm::translate(frame, {-0.42f, 0.55f, 0.0f}), glm::vec3{0.48f, 0.7f, 0.48f}));
            out.shrubs.append(shrub, glm::scale(glm::translate(frame, {0.42f, 0.55f, 0.0f}), glm::vec3{0.48f, 0.62f, 0.48f}));
            break;
        case PropKind::PicnicTable:
            box(out.painted, frame, {0.0f, 0.75f, 0.0f}, {1.8f, 0.06f, 0.8f}, wood);
            for (float z : {-0.62f, 0.62f})
                box(out.painted, frame, {0.0f, 0.45f, z}, {1.8f, 0.05f, 0.3f}, wood);
            for (float x : {-0.7f, 0.7f})
                box(out.painted, frame, {x, 0.38f, 0.0f}, {0.07f, 0.76f, 1.5f}, wood * 0.8f);
            break;
        case PropKind::SpeedSign:
        {
            box(out.darkMetal, frame, {0.0f, 1.2f, 0.0f}, {0.07f, 2.4f, 0.07f}, glm::vec3{2.2f});
            // A red ring round a white face, and 50 in black.
            const glm::mat4 plate = glm::rotate(glm::translate(frame, {0.0f, 2.25f, 0.05f}), glm::radians(90.0f), {1.0f, 0.0f, 0.0f});
            out.painted.setColor(glm::vec3{0.82f, 0.08f, 0.07f});
            out.painted.append(disc, glm::scale(plate, {0.4f, 0.04f, 0.4f}));
            out.painted.setColor(glm::vec3{0.96f});
            out.painted.append(disc, glm::scale(glm::translate(plate, {0.0f, 0.012f, 0.0f}), {0.31f, 0.04f, 0.31f}));
            out.painted.setColor(glm::vec3{1.0f});
            appendText(out.darkMetal, "50", glm::translate(frame, {0.0f, 2.25f, 0.1f}), 0.26f, 0.02f, 0.9f);
            break;
        }
        }
    }

    void buildGasStation(Builders& out, const GasStation& station)
    {
        const glm::mat4 frame = frameAt(station.centre, ground, station.facingDegrees);
        const float height = GasStation::canopyHeight;
        const float width = GasStation::canopyWidth;
        const float depth = GasStation::canopyDepth;
        box(out.painted, frame, {0.0f, height + 0.4f, 0.0f}, {width, 0.8f, depth}, glm::vec3{0.95f});
        box(out.painted, frame, {0.0f, height + 0.42f, 0.0f}, {width + 0.08f, 0.32f, depth + 0.08f}, {0.80f, 0.10f, 0.08f}, 0.02f);
        for (float x : {-4.0f, 4.0f})
        {
            for (float z : {-2.5f, 2.5f})
                box(out.canopyLights, frame, {x, height - 0.03f, z}, {3.0f, 0.06f, 1.2f});
            box(out.concrete, frame, {x, 0.13f, 0.0f}, {1.2f, 0.26f, 4.6f}, glm::vec3{1.0f}, 0.08f);
            for (float z : {-1.7f, 1.7f})
                box(out.darkMetal, frame, {x, 0.5f * height, z}, {0.36f, height, 0.36f}, glm::vec3{1.6f});
            for (float z : {-0.6f, 0.6f})
            {
                box(out.painted, frame, {x, 0.26f + 0.8f, z}, {0.7f, 1.6f, 0.5f}, {0.82f, 0.12f, 0.1f});
                box(out.painted, frame, {x, 0.26f + 1.7f, z}, {0.72f, 0.22f, 0.52f}, glm::vec3{0.95f});
                box(out.darkMetal, frame, {x + 0.36f, 0.26f + 1.25f, z}, {0.02f, 0.3f, 0.34f}, {0.3f, 0.5f, 0.6f});
                box(out.darkMetal, frame, {x - 0.36f, 0.26f + 1.25f, z}, {0.02f, 0.3f, 0.34f}, {0.3f, 0.5f, 0.6f});
            }
        }

        // The price sign by the road: a white column with a lit panel.
        const glm::mat4 sign = frameAt(station.priceSign, ground, station.facingDegrees);
        box(out.painted, sign, {0.0f, 2.9f, 0.0f}, {1.7f, 5.8f, 0.45f}, glm::vec3{0.95f});
        box(out.painted, sign, {0.0f, 5.55f, 0.0f}, {1.75f, 0.5f, 0.5f}, {0.80f, 0.10f, 0.08f}, 0.02f);
        box(out.signBoards, sign, {0.0f, 3.6f, 0.0f}, {1.4f, 2.6f, 0.5f}, {1.0f, 0.82f, 0.2f});
        for (float face : {0.0f, 180.0f})
        {
            const glm::mat4 side = glm::rotate(sign, glm::radians(face), {0.0f, 1.0f, 0.0f});
            appendText(out.darkMetal, "FUEL", glm::translate(side, {0.0f, 4.45f, 0.26f}), 0.36f, 0.03f);
            appendText(out.darkMetal, "1.29", glm::translate(side, {0.0f, 3.65f, 0.26f}), 0.42f, 0.03f);
            appendText(out.darkMetal, "1.35", glm::translate(side, {0.0f, 2.85f, 0.26f}), 0.42f, 0.03f);
        }
    }
}

// ---------------------------------------------------------------------------

const FacadeLook& PropRenderer::facadeLook(BuildingStyle style)
{
    static const std::array<FacadeLook, buildingStyleCount> looks = {{
        {{0.46f, 0.52f, 0.52f, 1.0f}, {0.16f, 0.2f, 0.24f}, 0.42f, 3.3f, 40.0f},   // shop row, upper floors
        {{0.42f, 0.48f, 0.54f, 1.0f}, {0.18f, 0.21f, 0.25f}, 0.48f, 3.2f, 36.0f},  // apartment
        {{0.86f, 0.66f, 0.55f, 1.0f}, {0.22f, 0.34f, 0.44f}, 0.30f, 2.4f, 90.0f},  // office: curtain wall
        {{0.50f, 0.55f, 0.55f, 1.0f}, {0.2f, 0.22f, 0.26f}, 0.50f, 3.4f, 48.0f},   // hotel
        {{0.30f, 0.42f, 0.56f, 1.0f}, {0.2f, 0.22f, 0.25f}, 0.55f, 3.0f, 24.0f},   // house
        {{0.78f, 0.12f, 0.84f, 1.0f}, {0.3f, 0.36f, 0.4f}, 0.12f, 4.0f, 30.0f},    // warehouse: high strip
        {{0.6f, 0.5f, 0.5f, 1.0f}, {0.2f, 0.25f, 0.3f}, 0.35f, 4.0f, 30.0f}        // skyline
    }};
    return looks[static_cast<std::size_t>(style)];
}

PropRenderer::PropRenderer(const World& world, const VehicleRenderer& vehicles)
{
    Builders out;

    for (const ParkedCar& car : world.parkedCars())
    {
        VehiclePose pose;
        pose.kind = car.kind;
        pose.position = {car.position.x, Route::rideHeight + RoadNetwork::kerbTopY - RoadNetwork::roadY, car.position.y};
        pose.yawDegrees = car.yawDegrees;
        pose.color = car.color;
        vehicles.bakeParked(pose, out.parkedBodies, out.parkedGlass, out.parkedDark, out.parkedLenses);
    }

    for (const Building& building : world.buildings())
        buildBuilding(out, building);
    for (const Building& building : world.skyline())
        buildBuilding(out, building);

    // Each shop belongs to the building whose front it is on; its glass
    // reaches up to that building's ground floor band.
    for (const Shop& shop : world.shops())
    {
        float groundFloor = 4.2f;
        for (const Building& building : world.buildings())
        {
            const glm::vec2 centre {building.position.x, building.position.z};
            if (std::abs(shop.centre.x - centre.x) <= 0.5f * building.size.x + 0.5f &&
                std::abs(shop.centre.y - centre.y) <= 0.5f * building.size.z + 0.5f)
                groundFloor = std::max(building.groundFloor, 3.6f);
        }
        buildShop(out, shop, groundFloor);
    }

    for (const Prop& prop : world.props())
        buildProp(out, prop);
    for (const GasStation& station : world.gasStations())
        buildGasStation(out, station);

    // Paving and asphalt laid on the lawn, and the car park's white lines.
    for (const PavedArea& area : world.pavedAreas())
        (area.asphalt ? out.asphalt : out.paving).addFlatPolygon(area.outline, ground + 0.02f, area.asphalt ? 1.0f : 2.2f);
    for (const glm::vec4& line : world.bayLines())
    {
        const glm::vec2 a {line.x, line.y};
        const glm::vec2 b {line.z, line.w};
        out.bayPaint.addPaintRectangle(0.5f * (a + b), b - a, glm::length(b - a), 0.12f, ground + 0.035f);
    }

    // The pond: water inside a stone edge.
    for (const Pond& pond : world.ponds())
    {
        std::vector<glm::vec2> edge;
        for (const glm::vec2& point : pond.outline)
            edge.push_back(pond.centre + (point - pond.centre) * (1.0f + 0.5f / glm::length(point - pond.centre)));
        out.water.addFlatPolygon(pond.outline, ground + 0.16f, 4.0f);
        out.concrete.addWall(edge, ground, ground + 0.34f);
        out.concrete.addWall(pond.outline, ground + 0.1f, ground + 0.34f, true, true);
        out.concrete.addFlatRing(edge, pond.outline, ground + 0.34f, 1.0f);
    }

    // The monument on the plaza: a stepped pedestal and a bronze flame.
    if (world.hasPlaza())
    {
        static const MeshData statue = Mesh::bezierRevolutionData(statueProfile(), 14, 18);
        const glm::mat4 frame = frameAt(world.plazaCentre(), ground, 0.0f);
        box(out.concrete, frame, {0.0f, 0.5f, 0.0f}, {2.6f, 1.0f, 2.6f}, glm::vec3{1.0f}, 0.08f);
        box(out.concrete, frame, {0.0f, 1.3f, 0.0f}, {1.8f, 0.6f, 1.8f}, glm::vec3{1.0f}, 0.08f);
        out.bronze.append(statue, glm::translate(frame, {0.0f, 1.6f, 0.0f}));
    }

    // Trees: every tree is one of a few prebuilt shapes per species, turned
    // and scaled, with a slightly different green.
    const TreeSpecies species[3] = {TreeSpecies::Broadleaf, TreeSpecies::Conifer, TreeSpecies::Palm};
    std::vector<TreeModel> models;
    for (TreeSpecies kind : species)
    {
        for (int variant = 0; variant < TreeGenerator::variantsPerSpecies; ++variant)
            models.push_back(TreeGenerator::make(kind, variant));
    }
    for (const Tree& tree : world.trees())
    {
        const std::size_t kind = static_cast<std::size_t>(tree.species);
        const TreeModel& model = models[kind * TreeGenerator::variantsPerSpecies +
                                        static_cast<std::size_t>(tree.variant % TreeGenerator::variantsPerSpecies)];
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), {tree.position.x, ground + 0.01f, tree.position.y});
        transform = glm::rotate(transform, glm::radians(tree.twistDegrees), {0.0f, 1.0f, 0.0f});
        transform = glm::scale(transform, glm::vec3{tree.scale});
        out.bark.append(model.bark, transform);
        const float shade = 0.92f + 0.16f * std::fmod(std::abs(tree.twistDegrees) * 0.013f, 1.0f);
        out.leaves.setColor(glm::vec4(shade, shade * 1.02f, shade * 0.95f, 1.0f));
        out.leaves.append(model.leaves, transform);
        out.leaves.setColor(glm::vec3{1.0f});
        ++treeCount_;
    }

    const auto finish = [this](const MeshBuilder& builder)
    {
        vertexCount_ += builder.data().vertices.size();
        return builder.build();
    };
    for (std::size_t style = 0; style < facades_.size(); ++style)
        facades_[style] = finish(out.facades[style]);
    plainWalls_ = finish(out.plainWalls);
    roofs_ = finish(out.roofs);
    tiledRoofs_ = finish(out.tiledRoofs);
    shopGlass_ = finish(out.shopGlass);
    darkMetal_ = finish(out.darkMetal);
    painted_ = finish(out.painted);
    signBoards_ = finish(out.signBoards);
    letters_ = finish(out.letters);
    canopyLights_ = finish(out.canopyLights);
    concrete_ = finish(out.concrete);
    shrubs_ = finish(out.shrubs);
    bronze_ = finish(out.bronze);
    water_ = finish(out.water);
    paving_ = finish(out.paving);
    asphalt_ = finish(out.asphalt);
    bayPaint_ = finish(out.bayPaint);
    bark_ = finish(out.bark);
    leaves_ = finish(out.leaves);
    parkedBodies_ = finish(out.parkedBodies);
    parkedGlass_ = finish(out.parkedGlass);
    parkedDark_ = finish(out.parkedDark);
    parkedLenses_ = finish(out.parkedLenses);
    skylineRoofs_ = finish(out.skylineRoofs);

    constexpr int atlasSize = 512;
    leafAtlas_ = Texture::fromRgba(atlasSize, atlasSize, TreeGenerator::leafAtlas(atlasSize), 0.5f, true);
}
