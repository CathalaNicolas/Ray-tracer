#pragma once

#include "EntityId.hpp"

#include <vector>

struct MeshRasterGeom
{
    // Index range into the shared element buffer (uint32 indices).
    int indexOffset = 0;
    int indexCount = 0;
};

struct MeshRasterInstance
{
    int geom = 0;
    EntityId objectId = kInvalidEntityId;
    float meshId = 0;
    float px = 0;
    float py = 0;
    float pz = 0;
    float scale = 1;
    float ax = 1;
    float ay = 0;
    float az = 0;
    float bx = 0;
    float by = 1;
    float bz = 0;
    float cx = 0;
    float cy = 0;
    float cz = 1;
    bool transparent = false;
    bool inFrustum = true;
    double sortKey = 0;
};
