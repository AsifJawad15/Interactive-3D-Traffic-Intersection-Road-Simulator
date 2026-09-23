#pragma once

#include "Mesh.h"

class TrafficSystem;

// The whole road network as a handful of static meshes, one per material:
// asphalt, kerb faces, sidewalks, lawns, white paint and yellow paint.
// Built once from the RoadNetwork (and from where the traffic actually stops,
// so the painted stop lines are exactly where the cars wait).
class RoadRenderer
{
public:
    explicit RoadRenderer(const TrafficSystem& traffic);

    const Mesh& asphalt() const { return asphalt_; }
    const Mesh& kerbs() const { return kerbs_; }
    const Mesh& sidewalks() const { return sidewalks_; }
    const Mesh& lawns() const { return lawns_; }
    const Mesh& whitePaint() const { return whitePaint_; }
    const Mesh& yellowPaint() const { return yellowPaint_; }

private:
    Mesh asphalt_;
    Mesh kerbs_;
    Mesh sidewalks_;
    Mesh lawns_;
    Mesh whitePaint_;
    Mesh yellowPaint_;
};
