#pragma once

#include "Collision.hpp"
#include "GpuContribute.hpp"
#include "Material.hpp"
#include "Ray.hpp"
#include "Role.hpp"

#include <cmath>
#include <cstdint>
#include <memory>
#include <ostream>
#include <string>

class Scene;

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

// origin is the world position of local (0, 0, 0). axis* are unit columns of the
// parent rotation. scale multiplies local offsets and a mesh or sphere size.
struct ParentFrame
{
    Vec3 origin;
    Vec3 axisX{1, 0, 0};
    Vec3 axisY{0, 1, 0};
    Vec3 axisZ{0, 0, 1};
    double scale = 1;

    Vec3 point(const Vec3 &local) const
    {
        const Vec3 scaled = local * scale;
        return origin + axisX * scaled.x + axisY * scaled.y + axisZ * scaled.z;
    }

    Vec3 direction(const Vec3 &local) const
    {
        return axisX * local.x + axisY * local.y + axisZ * local.z;
    }

    Vec3 toLocal(const Vec3 &world) const
    {
        const Vec3 delta = world - origin;
        const Vec3 unrotated(dot(delta, axisX), dot(delta, axisY), dot(delta, axisZ));
        return scale == 0 ? unrotated : unrotated / scale;
    }
};

struct HitRecord
{
    double t = 0;
    Vec3 point;
    Vec3 normal;
    Material material;
    int objectId = -1;
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

class Hittable
{
public:
    int id = 0;
    int parentId = 0;
    std::string name;
    std::string tag;
    mutable std::shared_ptr<Role> role = makeRole("");
    int layer = 0;
    std::string prefab;
    std::string instanceOf;
    Motion motion;
    Action action;
    double spawnEvery = 0;
    double spawnClock = 0;
    int spawnedId = -1;

    virtual ~Hittable() = default;
    virtual const char *kind() const = 0;
    virtual std::unique_ptr<Hittable> clone() const = 0;
    virtual Material material() const = 0;
    virtual void setMaterial(const Material &material) = 0;
    virtual bool intersect(const Ray &ray, double tMin, double tMax, HitRecord &hit) const = 0;
    virtual Vec3 localPosition() const = 0;
    virtual void setLocalPosition(const Vec3 &position) = 0;

    virtual Vec3 worldPosition() const { return parentFrame().point(localPosition()); }
    virtual void setWorldPosition(const Vec3 &world)
    {
        setLocalPosition(parentFrame().toLocal(world));
        notifyTransformChanged();
    }

    virtual Vec3 localRotation() const { return Vec3(); }
    virtual void setLocalRotation(const Vec3 &) {}
    virtual double localScale() const { return 1; }
    virtual void setLocalScale(double) {}
    virtual double bodyRadius() const { return 0; }

    virtual Hit contactSphere(const Vec3 &center, double radius) const = 0;
    virtual bool overlapsSphere(const Vec3 &center, double radius) const { return contactSphere(center, radius).hit; }
    virtual bool blocksPlayer(int playerId) const = 0;
    virtual bool blocksCamera(int playerId) const
    {
        if (layer != 0)
            return false;
        return blocksPlayer(playerId);
    }

    virtual ColliderSketch colliderSketch() const = 0;
    virtual void writeScene(std::ostream &out) const = 0;
    virtual void contributeGpu(gpu_detail::GpuContribute &sink) const = 0;
    virtual bool copyShapeFrom(const Hittable &source) = 0;
    virtual void mixShapeHash(std::uint64_t &hash) const = 0;

    // Mesh parents rotate and scale the frame. Sphere and plane parents only translate.
    virtual void applyParentAxes(ParentFrame &) const {}

    // Ancestor chain, root first. A parent id of 0, a missing parent, or a
    // repeated id (including a parent of itself) stops the walk. A mesh ancestor
    // rotates and scales what follows. A sphere or plane ancestor only translates.
    // Cached until Scene::bumpParentFrames().
    ParentFrame parentFrame() const;
    void bindScene(Scene *scene) { owner_ = scene; parentFrameStamp_ = 0; }
    void invalidateParentFrame() const { parentFrameStamp_ = 0; }
    void notifyTransformChanged() const;

    void setTag(const std::string &value)
    {
        tag = value;
        role = makeRole(value);
    }

    // Rebuild role when tag was assigned directly (demo, tests, loaders).
    void ensureRole() const
    {
        if (!role || tag != role->fileTag())
            role = makeRole(tag);
    }

protected:
    bool rolePassThrough(int playerId) const
    {
        if (id == playerId)
            return true;
        ensureRole();
        return role && role->passThroughSolid();
    }

    // Shared by Sphere::clone, Plane::clone, and Mesh::clone. Does not copy the scene pointer.
    void copyBaseTo(Hittable &copy) const
    {
        copy.id = id;
        copy.parentId = parentId;
        copy.name = name;
        copy.setTag(tag);
        copy.layer = layer;
        copy.prefab = prefab;
        copy.instanceOf = instanceOf;
        copy.motion = motion;
        copy.action = action;
        copy.spawnEvery = spawnEvery;
        copy.spawnClock = spawnClock;
        copy.spawnedId = spawnedId;
        copy.parentFrameStamp_ = 0;
    }

private:
    Scene *owner_ = nullptr;
    mutable ParentFrame parentFrameCache_;
    mutable std::uint32_t parentFrameStamp_ = 0;
};
