#pragma once

#include "Mesh.h"
#include "World.h"

#include <vector>

// Trees, grown once at start-up. A trunk and its branches are tubes swept
// along Bezier curves (the Lab 5 curve, in 3D here); the foliage is leaf
// cards: small quads carrying a picture of a leafy twig with a transparent
// background, alpha-tested so only the leaves are drawn. The cards' normals
// are bent outward from the middle of the crown, so a crown is lit like one
// round shape rather than like a heap of flat cards.
//
// Every vertex's colour alpha holds how far it sways in the wind (0 at the
// foot of the trunk, growing towards the tips); the vertex shader moves it.

struct TreeModel
{
    MeshData bark;
    MeshData leaves;
    float height = 7.0f;
    float crownRadius = 3.0f;
};

namespace TreeGenerator
{
    constexpr int variantsPerSpecies = 3;

    // One of the prebuilt shapes of a species, at its natural size.
    TreeModel make(TreeSpecies species, int variant);

    // The leaf picture every card samples, `size` x `size` RGBA. Its four
    // quarters hold two broadleaf clusters, a conifer spray and a palm frond.
    std::vector<unsigned char> leafAtlas(int size);
}
