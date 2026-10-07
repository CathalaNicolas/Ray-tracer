#pragma once

#include "Constants.hpp"
#include "Vec3.hpp"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

inline bool meshRotationActive(const Vec3 &degrees)
{
    return std::abs(degrees.x) > 1e-8 || std::abs(degrees.y) > 1e-8 || std::abs(degrees.z) > 1e-8;
}

inline Vec3 meshRotate(const Vec3 &value, const Vec3 &degrees)
{
    if (!meshRotationActive(degrees))
        return value;
    const double yaw = degrees.y * kPi / 180.0;
    const double pitch = degrees.x * kPi / 180.0;
    const double roll = degrees.z * kPi / 180.0;
    const double cy = std::cos(yaw);
    const double sy = std::sin(yaw);
    const double cp = std::cos(pitch);
    const double sp = std::sin(pitch);
    const double cr = std::cos(roll);
    const double sr = std::sin(roll);
    const Vec3 yawed(cy * value.x + sy * value.z, value.y, -sy * value.x + cy * value.z);
    const Vec3 pitched(yawed.x, cp * yawed.y - sp * yawed.z, sp * yawed.y + cp * yawed.z);
    return Vec3(cr * pitched.x - sr * pitched.y, sr * pitched.x + cr * pitched.y, pitched.z);
}

inline Vec3 meshRotateInverse(const Vec3 &value, const Vec3 &degrees)
{
    if (!meshRotationActive(degrees))
        return value;
    const double yaw = degrees.y * kPi / 180.0;
    const double pitch = degrees.x * kPi / 180.0;
    const double roll = degrees.z * kPi / 180.0;
    const double cy = std::cos(yaw);
    const double sy = std::sin(yaw);
    const double cp = std::cos(pitch);
    const double sp = std::sin(pitch);
    const double cr = std::cos(roll);
    const double sr = std::sin(roll);
    const Vec3 rolled(cr * value.x + sr * value.y, -sr * value.x + cr * value.y, value.z);
    const Vec3 pitched(rolled.x, cp * rolled.y + sp * rolled.z, -sp * rolled.y + cp * rolled.z);
    return Vec3(cy * pitched.x - sy * pitched.z, pitched.y, sy * pitched.x + cy * pitched.z);
}

struct MeshTri
{
    Vec3 position[3];
    Vec3 normal[3];
    float u[3]{};
    float v[3]{};
};

struct BvhNode
{
    Vec3 boundsMin;
    Vec3 boundsMax;
    int left = 0;
    int right = 0;
};

int buildBvh(std::vector<MeshTri> &triangles, std::vector<BvhNode> &nodes);

// One LOD's indices. Empty positions means the indices refer to MeshGeometry's vertex arrays.
struct MeshLod
{
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<float> u;
    std::vector<float> v;
    std::vector<std::uint32_t> indices;
};

// Cooked material reference (paths relative to the content root when possible).
struct MeshMaterialRef
{
    std::string name;
    std::string albedoMap;
    std::string normalMap;
    Vec3 albedo{0.8, 0.8, 0.8};
};

// Range into MeshGeometry::indices (lod 0). GPU still draws the whole mesh with the placement material.
struct MeshSubmesh
{
    std::uint32_t materialIndex = 0;
    std::uint32_t indexOffset = 0;
    std::uint32_t indexCount = 0;
};

struct MeshGeometry
{
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<float> u;
    std::vector<float> v;
    std::vector<std::uint32_t> indices;
    std::vector<MeshLod> extraLods;
    std::vector<MeshMaterialRef> materials;
    std::vector<MeshSubmesh> submeshes;
    // Built from indices for the CPU BVH and the GPU ray-mesh path.
    std::vector<MeshTri> triangles;
    std::vector<BvhNode> nodes;
    int root = -1;
    Vec3 boundsMin;
    Vec3 boundsMax;
    std::string sourcePath;
    // Stable across reloads that rebuild the same mesh (not a pointer).
    std::uint64_t contentHash = 0;
};

void finishGeometry(MeshGeometry &geometry);
MeshLod meshLodFromTriangles(const std::vector<MeshTri> &triangles);
void meshAssignTriangles(MeshGeometry &geometry, std::vector<MeshTri> triangles);
