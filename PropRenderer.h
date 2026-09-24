#pragma once

#include "Mesh.h"
#include "Texture.h"
#include "VehicleRenderer.h"
#include "World.h"

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <array>

// The city dressing, baked once at start-up: every building, shop front,
// roof, tree, bench and paving stone in the city goes into one mesh per
// material, so the whole lot costs a few dozen draw calls. Colours that
// differ from piece to piece (a building's paint, an awning's stripes) ride
// in the vertex colour.

// How a style's walls are glazed. The walls' texture coordinates count bays
// across and storeys up; the fragment shader draws a window in every cell.
struct FacadeLook
{
    glm::vec4 window {0.45f, 0.5f, 0.55f, 1.0f};   // width, height (share of a cell), centre height in the cell, on
    glm::vec3 glass {0.2f};                         // the glass by day
    float litShare = 0.4f;                          // share of rooms lit at night
    float bayWidth = 3.2f;                          // metres per window bay
    float shininess = 40.0f;
};

class PropRenderer
{
public:
    // Parked cars are baked with the vehicles' own bodies.
    PropRenderer(const World& world, const VehicleRenderer& vehicles);

    static const FacadeLook& facadeLook(BuildingStyle style);

    // Walls with windows, one mesh per style (the window pattern is a
    // uniform, so it is set per style when drawn). Vertex alpha is the
    // building's seed for which rooms are lit.
    const Mesh& facade(BuildingStyle style) const { return facades_[static_cast<std::size_t>(style)]; }

    const Mesh& plainWalls() const { return plainWalls_; }   // ground-floor bands, parapets, gables
    const Mesh& roofs() const { return roofs_; }             // flat roofs
    const Mesh& tiledRoofs() const { return tiledRoofs_; }   // the houses' pitched roofs
    const Mesh& shopGlass() const { return shopGlass_; }     // shop windows, lit inside at night
    const Mesh& darkMetal() const { return darkMetal_; }     // frames, doors, rooftop plant, columns
    const Mesh& painted() const { return painted_; }         // awnings, benches, bins, pumps, tanks, signs
    const Mesh& signBoards() const { return signBoards_; }   // shop signs, lit from inside at night
    const Mesh& letters() const { return letters_; }         // white lettering
    const Mesh& canopyLights() const { return canopyLights_; }
    const Mesh& concrete() const { return concrete_; }       // planters, pump islands, coping, pedestal
    const Mesh& shrubs() const { return shrubs_; }           // clipped shrubs in the planters (Lab 5)
    const Mesh& bronze() const { return bronze_; }           // the monument (Lab 5)
    const Mesh& water() const { return water_; }
    const Mesh& paving() const { return paving_; }
    const Mesh& asphalt() const { return asphalt_; }
    const Mesh& bayPaint() const { return bayPaint_; }
    const Mesh& bark() const { return bark_; }
    const Mesh& leaves() const { return leaves_; }
    const Texture& leafAtlas() const { return leafAtlas_; }

    // The parked cars: paint and hubs, glass, trim and tyres, lamp lenses.
    const Mesh& parkedBodies() const { return parkedBodies_; }
    const Mesh& parkedGlass() const { return parkedGlass_; }
    const Mesh& parkedDark() const { return parkedDark_; }
    const Mesh& parkedLenses() const { return parkedLenses_; }

    // The far skyline, drawn with extra haze.
    const Mesh& skylineRoofs() const { return skylineRoofs_; }

    // What was baked, for the HUD and the report.
    std::size_t treeCount() const { return treeCount_; }
    std::size_t vertexCount() const { return vertexCount_; }

private:
    std::array<Mesh, buildingStyleCount> facades_;
    Mesh plainWalls_;
    Mesh roofs_;
    Mesh tiledRoofs_;
    Mesh shopGlass_;
    Mesh darkMetal_;
    Mesh painted_;
    Mesh signBoards_;
    Mesh letters_;
    Mesh canopyLights_;
    Mesh concrete_;
    Mesh shrubs_;
    Mesh bronze_;
    Mesh water_;
    Mesh paving_;
    Mesh asphalt_;
    Mesh bayPaint_;
    Mesh bark_;
    Mesh leaves_;
    Texture leafAtlas_;
    Mesh parkedBodies_;
    Mesh parkedGlass_;
    Mesh parkedDark_;
    Mesh parkedLenses_;
    Mesh skylineRoofs_;
    std::size_t treeCount_ = 0;
    std::size_t vertexCount_ = 0;
};
