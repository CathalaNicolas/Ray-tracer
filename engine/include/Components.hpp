#pragma once

#include "EntityId.hpp"
#include "Material.hpp"
#include "MeshGeometry.hpp"
#include "Role.hpp"
#include "Terrain.hpp"
#include "Vec3.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

struct Motion
{
    Vec3 move;
    Vec3 rotate;
    double scale = 0;
    double period = 4;

    bool active() const
    {
        return std::abs(move.x) + std::abs(move.y) + std::abs(move.z) > 1e-8
            || std::abs(rotate.x) + std::abs(rotate.y) + std::abs(rotate.z) > 1e-8
            || std::abs(scale) > 1e-8;
    }
};

// One Use press plays `move` and `rotate` on `target`, then leaves it there.
// fired is runtime. The file stores target, move, and rotate when rotate is set.
struct Action
{
    std::string target;
    Vec3 move;
    Vec3 rotate;
    bool fired = false;

    bool armed() const
    {
        const double moveLength = std::abs(move.x) + std::abs(move.y) + std::abs(move.z);
        const double rotateLength = std::abs(rotate.x) + std::abs(rotate.y) + std::abs(rotate.z);
        return !target.empty() && (moveLength > 1e-8 || rotateLength > 1e-8);
    }
};

struct Transform
{
    EntityId parent = kInvalidEntityId;
    glm::dvec3 position{0, 0, 0};
    glm::dquat rotation{1, 0, 0, 0}; // w, x, y, z
    glm::dvec3 scale{1, 1, 1};

    mutable glm::dmat4 localMatrix{1};
    mutable glm::dmat4 worldMatrix{1};
    mutable bool matricesDirty = true;
    mutable std::uint32_t worldEpoch = 0;
};

// View-only interpolated pose. Simulation never reads this.
struct DisplayTransform
{
    glm::dvec3 position{0, 0, 0};
    glm::dquat rotation{1, 0, 0, 0};
    glm::dvec3 scale{1, 1, 1};
    bool active = false;
};

struct SphereShape
{
    double radius = 1;
};

struct PlaneShape
{
    Vec3 normal{0, 1, 0};
    bool checker = false;
    Vec3 checkerAlbedo{0.2, 0.2, 0.22};
    double checkerScale = 1;
};

struct MeshShape
{
    std::shared_ptr<MeshGeometry> geometry;
    std::string sourcePath;
    std::int64_t sourceStamp = 0;
    std::uintmax_t sourceBytes = 0;
};

struct TerrainTileComponent
{
    std::shared_ptr<TerrainTile> tile;
};

// Named attachment points. Empty stub for Phase 0 exit.
struct MeshSockets
{
    std::vector<std::string> names;
};

struct MaterialComponent
{
    Material material;
};

struct Name
{
    std::string value;
};

struct Tag
{
    std::string value;
    mutable std::shared_ptr<Role> role = makeRole("");
};

struct Layer
{
    int value = 0;
};

struct PrefabSource
{
    std::string name;
};

struct PrefabInstance
{
    std::string source;
};

struct Spawner
{
    double every = 0; // seconds in the scene file / inspector
    int clock = 0;    // play ticks at kPlayStep
    EntityId spawnedId = kInvalidEntityId;
};

struct AudioEmitter
{
};

struct EntityIdComponent
{
    EntityId id = kInvalidEntityId;
};

// Collider overlay payload. DebugDraw renders this without casting the shape.
struct ColliderSketch
{
    enum class Kind
    {
        None,
        Sphere,
        PlanePatch,
        MeshBox,
        MeshPoint,
    };

    Kind kind = Kind::None;
    Vec3 center;
    double radius = 0;
    Vec3 normal{0, 1, 0};
    Vec3 axisX{1, 0, 0};
    Vec3 axisY{0, 1, 0};
    Vec3 axisZ{0, 0, 1};
    Vec3 localMin;
    Vec3 localMax;
    bool player = false;
};

struct HitRecord
{
    double t = 0;
    Vec3 point;
    Vec3 normal;
    Material material;
    EntityId objectId = kInvalidEntityId;
};
