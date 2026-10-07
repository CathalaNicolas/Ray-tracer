#pragma once

#include "EntityId.hpp"
#include "MeshGeometry.hpp"
#include "Terrain.hpp"
#include "Vec3.hpp"

#include <cstdint>
#include <memory>
#include <vector>

// Play physics: PhysicsSystem + CharacterVirtual. Jobs use Jolt's JobSystemThreadPool.
namespace jolt_play
{

inline constexpr double kGravity = -12;
inline constexpr double kJumpSpeed = 5;
inline constexpr double kMoveSpeed = 4;
inline constexpr double kStepHeight = 0.35;
inline constexpr double kCapsuleRadius = 0.35;
inline constexpr double kCapsuleCylinderHalfHeight = 0.05;

class World
{
public:
    World();
    ~World();

    World(const World &) = delete;
    World &operator=(const World &) = delete;
    World(World &&) noexcept;
    World &operator=(World &&) noexcept;

    bool valid() const;
    int solidCount() const;

    void addStaticBox(const Vec3 &center, const Vec3 &halfExtent);
    void addStaticMesh(const std::vector<Vec3> &vertices, const std::vector<std::uint32_t> &indices);
    void addStaticHeightField(const TerrainTile &tile);

    int addKinematicBox(const Vec3 &center, const Vec3 &halfExtent);
    void moveKinematicBox(int handle, const Vec3 &center);

    void beginSolidSync();
    void touchSolid(EntityId id);
    void upsertBox(EntityId id, const Vec3 &center, const Vec3 &halfExtent, double qx, double qy, double qz,
        double qw, bool kinematic);
    void upsertSphere(EntityId id, const Vec3 &center, double radius, bool kinematic);
    void upsertPlane(EntityId id, const Vec3 &point, const Vec3 &normal);
    void upsertMesh(EntityId id, const MeshGeometry *geometry, const Vec3 &position, double qx, double qy, double qz,
        double qw, const Vec3 &scale, bool kinematic);
    void upsertHeightField(EntityId id, const TerrainTile *tile, const Vec3 &origin, double qx, double qy, double qz,
        double qw, double scale);
    void endSolidSync();

    void spawnCapsule(const Vec3 &feet, double radius = kCapsuleRadius,
        double cylinderHalfHeight = kCapsuleCylinderHalfHeight);
    void refreshCapsuleContacts();
    void setCapsuleFeet(const Vec3 &feet);

    void setWishVelocityXZ(double vx, double vz);
    void requestJump();

    void step(float dt);

    bool hasCapsule() const;
    bool grounded() const;
    Vec3 capsuleFeet() const;
    double capsuleRadius() const;
    double verticalVelocity() const;
    Vec3 capsuleVelocity() const;
    void setCapsuleVelocity(const Vec3 &velocity);
    std::vector<std::uint8_t> captureCharacterState() const;
    bool restoreCharacterState(const std::vector<std::uint8_t> &bytes);
    std::vector<std::uint8_t> capturePhysicsState() const;
    bool restorePhysicsState(const std::vector<std::uint8_t> &bytes);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace jolt_play
