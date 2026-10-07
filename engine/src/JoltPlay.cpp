#include "JoltPlay.hpp"

#include "Jobs.hpp"
#include "MeshGeometry.hpp"
#include "Terrain.hpp"

#include <Jolt/Jolt.h>

#ifndef JPH_DOUBLE_PRECISION
#error Jolt must be built with JPH_DOUBLE_PRECISION
#endif

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystem.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/ShapeFilter.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/StateRecorderImpl.h>
#include <Jolt/RegisterTypes.h>

#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{

void traceImpl(const char *fmt, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    spdlog::debug("jolt: {}", buffer);
}

#ifdef JPH_ENABLE_ASSERTS
bool assertFailedImpl(const char *expression, const char *message, const char *file, unsigned int line)
{
    spdlog::error("jolt assert {}:{} {} {}", file, line, expression, message != nullptr ? message : "");
    return true;
}
#endif

void ensureJoltTypes()
{
    static std::once_flag once;
    std::call_once(once, []() {
        JPH::Trace = traceImpl;
#ifdef JPH_ENABLE_ASSERTS
        JPH::AssertFailed = assertFailedImpl;
#endif
        if (JPH::Factory::sInstance == nullptr)
            JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    });
}

JPH::RVec3 toRVec(const Vec3 &v)
{
    return JPH::RVec3(v.x, v.y, v.z);
}

Vec3 fromRVec(JPH::RVec3Arg v)
{
    return Vec3(v.GetX(), v.GetY(), v.GetZ());
}

JPH::Vec3 toVec(const Vec3 &v)
{
    return JPH::Vec3(static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z));
}

JPH::Quat toQuat(double x, double y, double z, double w)
{
    const float nx = static_cast<float>(x);
    const float ny = static_cast<float>(y);
    const float nz = static_cast<float>(z);
    const float nw = static_cast<float>(w);
    const float len = std::sqrt(nx * nx + ny * ny + nz * nz + nw * nw);
    if (len < 1e-8f)
        return JPH::Quat::sIdentity();
    return JPH::Quat(nx / len, ny / len, nz / len, nw / len);
}

namespace Layers
{
constexpr JPH::ObjectLayer kNonMoving = 0;
constexpr JPH::ObjectLayer kMoving = 1;
constexpr JPH::ObjectLayer kNum = 2;
} // namespace Layers

namespace BpLayers
{
constexpr JPH::BroadPhaseLayer kNonMoving(0);
constexpr JPH::BroadPhaseLayer kMoving(1);
constexpr JPH::uint kNum = 2;
} // namespace BpLayers

class BPLayerInterface final : public JPH::BroadPhaseLayerInterface
{
public:
    BPLayerInterface()
    {
        objectToBp_[Layers::kNonMoving] = BpLayers::kNonMoving;
        objectToBp_[Layers::kMoving] = BpLayers::kMoving;
    }

    JPH::uint GetNumBroadPhaseLayers() const override { return BpLayers::kNum; }

    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
    {
        JPH_ASSERT(layer < Layers::kNum);
        return objectToBp_[layer];
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char *GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
    {
        switch ((JPH::BroadPhaseLayer::Type)layer)
        {
        case (JPH::BroadPhaseLayer::Type)BpLayers::kNonMoving:
            return "NON_MOVING";
        case (JPH::BroadPhaseLayer::Type)BpLayers::kMoving:
            return "MOVING";
        default:
            return "INVALID";
        }
    }
#endif

private:
    JPH::BroadPhaseLayer objectToBp_[Layers::kNum];
};

class ObjectVsBpFilter final : public JPH::ObjectVsBroadPhaseLayerFilter
{
public:
    bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer bp) const override
    {
        switch (layer)
        {
        case Layers::kNonMoving:
            return bp == BpLayers::kMoving;
        case Layers::kMoving:
            return true;
        default:
            return false;
        }
    }
};

class ObjectPairFilter final : public JPH::ObjectLayerPairFilter
{
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override
    {
        switch (a)
        {
        case Layers::kNonMoving:
            return b == Layers::kMoving;
        case Layers::kMoving:
            return true;
        default:
            return false;
        }
    }
};

float boxConvexRadius(const Vec3 &halfExtent)
{
    const double smallest = std::min({halfExtent.x, halfExtent.y, halfExtent.z});
    return static_cast<float>(std::min(0.02, smallest * 0.25));
}

enum class SolidKind
{
    Box,
    Sphere,
    Mesh,
    Plane,
    HeightField,
};

bool quatNear(JPH::QuatArg a, JPH::QuatArg b)
{
    return std::abs(a.GetX() - b.GetX()) <= 1e-5f && std::abs(a.GetY() - b.GetY()) <= 1e-5f
        && std::abs(a.GetZ() - b.GetZ()) <= 1e-5f && std::abs(a.GetW() - b.GetW()) <= 1e-5f;
}

std::uint64_t scaleBits(double scale)
{
    std::uint64_t bits = 0;
    std::memcpy(&bits, &scale, sizeof(bits));
    return bits;
}

struct MeshShapeKey
{
    std::uint64_t contentHash = 0;
    std::uint64_t sx = 0;
    std::uint64_t sy = 0;
    std::uint64_t sz = 0;

    bool operator==(const MeshShapeKey &other) const
    {
        return contentHash == other.contentHash && sx == other.sx && sy == other.sy && sz == other.sz;
    }
};

struct HeightFieldKey
{
    const TerrainTile *tile = nullptr;
    std::uint64_t scale = 0;

    bool operator==(const HeightFieldKey &other) const
    {
        return tile == other.tile && scale == other.scale;
    }
};

struct HeightFieldKeyHash
{
    std::size_t operator()(const HeightFieldKey &key) const
    {
        return std::hash<const void *>()(key.tile) ^ std::hash<std::uint64_t>()(key.scale);
    }
};

struct MeshShapeKeyHash
{
    std::size_t operator()(const MeshShapeKey &key) const
    {
        return std::hash<std::uint64_t>{}(key.contentHash) ^ (std::hash<std::uint64_t>{}(key.sx) << 1)
            ^ (std::hash<std::uint64_t>{}(key.sy) << 2) ^ (std::hash<std::uint64_t>{}(key.sz) << 3);
    }
};

} // namespace

struct jolt_play::World::Impl
{
    struct Slot
    {
        JPH::BodyID id;
        Vec3 target;
        JPH::Quat targetRot = JPH::Quat::sIdentity();
        std::uint64_t meshContentHash = 0;
        Vec3 scale{1, 1, 1};
        double radius = 0;
        SolidKind kind = SolidKind::Box;
        bool kinematic = false;
        bool seen = true;
    };

    Impl()
    {
        if (!jobs::ready() || jobs::joltJobSystem() == nullptr)
            return;

        ensureJoltTypes();

        physics_.Init(1024, 0, 4096, 1024, bpLayers_, objectVsBp_, objectPair_);
        physics_.SetGravity(JPH::Vec3(0, static_cast<float>(kGravity), 0));
        valid_ = true;
    }

    ~Impl()
    {
        character_ = nullptr;
        if (!valid_)
            return;
        for (Slot &slot : anonymous_)
            destroyBody(slot.id);
        for (auto &pair : solids_)
            destroyBody(pair.second.id);
        anonymous_.clear();
        solids_.clear();
    }

    JPH::BodyInterface &bodies() { return physics_.GetBodyInterface(); }

    void destroyBody(JPH::BodyID id)
    {
        if (id.IsInvalid())
            return;
        bodies().RemoveBody(id);
        bodies().DestroyBody(id);
    }

    JPH::RefConst<JPH::Shape> makeBox(const Vec3 &halfExtent)
    {
        JPH::BoxShapeSettings settings(toVec(halfExtent), boxConvexRadius(halfExtent));
        settings.SetEmbedded();
        const JPH::Shape::ShapeResult result = settings.Create();
        if (result.HasError())
            return nullptr;
        return result.Get();
    }

    JPH::RefConst<JPH::Shape> makeSphere(double radius)
    {
        if (radius <= 1e-6)
            return nullptr;
        JPH::SphereShapeSettings settings(static_cast<float>(radius));
        settings.SetEmbedded();
        const JPH::Shape::ShapeResult result = settings.Create();
        if (result.HasError())
            return nullptr;
        return result.Get();
    }

    JPH::RefConst<JPH::Shape> makeMesh(const std::vector<Vec3> &vertices, const std::vector<std::uint32_t> &indices,
        const Vec3 &scale)
    {
        if (vertices.empty() || indices.size() < 3)
            return nullptr;

        JPH::VertexList verts;
        verts.reserve(vertices.size());
        for (const Vec3 &v : vertices)
            verts.push_back(JPH::Float3(static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)));

        JPH::IndexedTriangleList tris;
        tris.reserve(indices.size() / 3);
        for (size_t i = 0; i + 2 < indices.size(); i += 3)
            tris.push_back(JPH::IndexedTriangle(indices[i], indices[i + 1], indices[i + 2]));

        JPH::MeshShapeSettings settings(std::move(verts), std::move(tris));
        settings.SetEmbedded();
        const JPH::Shape::ShapeResult result = settings.Create();
        if (result.HasError())
        {
            spdlog::warn("jolt mesh shape: {}", result.GetError());
            return nullptr;
        }
        JPH::RefConst<JPH::Shape> mesh = result.Get();
        const float sx = static_cast<float>(scale.x > 1e-8 ? scale.x : 1);
        const float sy = static_cast<float>(scale.y > 1e-8 ? scale.y : 1);
        const float sz = static_cast<float>(scale.z > 1e-8 ? scale.z : 1);
        if (std::abs(sx - 1.f) <= 1e-5f && std::abs(sy - 1.f) <= 1e-5f && std::abs(sz - 1.f) <= 1e-5f)
            return mesh;
        JPH::ScaledShapeSettings scaled(mesh, JPH::Vec3(sx, sy, sz));
        scaled.SetEmbedded();
        const JPH::Shape::ShapeResult scaledResult = scaled.Create();
        if (scaledResult.HasError())
            return nullptr;
        return scaledResult.Get();
    }

    JPH::RefConst<JPH::Shape> meshShapeCached(const MeshGeometry *geometry, const Vec3 &scale)
    {
        if (geometry == nullptr || geometry->positions.empty() || geometry->indices.size() < 3)
            return nullptr;
        const Vec3 safeScale(
            scale.x > 1e-8 ? scale.x : 1,
            scale.y > 1e-8 ? scale.y : 1,
            scale.z > 1e-8 ? scale.z : 1);
        const MeshShapeKey key{
            geometry->contentHash == 0 ? 1 : geometry->contentHash,
            scaleBits(safeScale.x),
            scaleBits(safeScale.y),
            scaleBits(safeScale.z)};
        auto found = meshShapes_.find(key);
        if (found != meshShapes_.end())
            return found->second;

        const JPH::RefConst<JPH::Shape> shape = makeMesh(geometry->positions, geometry->indices, safeScale);
        if (shape != nullptr)
            meshShapes_.emplace(key, shape);
        return shape;
    }

    JPH::RefConst<JPH::Shape> makeHeightField(const TerrainTile *tile, double scale)
    {
        if (tile == nullptr || tile->heights.size() != tile->sampleCount * tile->sampleCount || tile->sampleCount < 4)
            return nullptr;
        const float s = static_cast<float>(scale > 1e-8 ? scale : 1);
        const float cell = terrainCellSize(*tile) * s;
        std::vector<float> samples = tile->heights;
        if (!tile->holes.empty() && tile->holes.size() == samples.size())
        {
            for (std::size_t i = 0; i < samples.size(); ++i)
                if (tile->holes[i] != 0)
                    samples[i] = JPH::HeightFieldShapeConstants::cNoCollisionValue;
        }
        JPH::HeightFieldShapeSettings settings(samples.data(), JPH::Vec3::sZero(), JPH::Vec3(cell, s, cell),
            tile->sampleCount);
        settings.mBlockSize = 2;
        settings.SetEmbedded();
        const JPH::Shape::ShapeResult result = settings.Create();
        if (result.HasError())
        {
            spdlog::warn("jolt heightfield: {}", result.GetError());
            return nullptr;
        }
        return result.Get();
    }

    JPH::RefConst<JPH::Shape> heightFieldCached(const TerrainTile *tile, double scale)
    {
        const HeightFieldKey key{tile, scaleBits(scale > 1e-8 ? scale : 1)};
        auto found = heightFields_.find(key);
        if (found != heightFields_.end())
            return found->second;
        const JPH::RefConst<JPH::Shape> shape = makeHeightField(tile, scale);
        if (shape != nullptr)
            heightFields_.emplace(key, shape);
        return shape;
    }

    JPH::BodyID createBody(const JPH::RefConst<JPH::Shape> &shape, const Vec3 &center, JPH::QuatArg rot,
        JPH::EMotionType motion, JPH::ObjectLayer layer)
    {
        if (shape == nullptr)
            return JPH::BodyID();
        JPH::BodyCreationSettings settings(shape, toRVec(center), rot, motion, layer);
        settings.mAllowSleeping = false;
        if (motion != JPH::EMotionType::Static)
        {
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::MassAndInertiaProvided;
            settings.mMassPropertiesOverride.mMass = 1.0f;
            settings.mMassPropertiesOverride.mInertia = JPH::Mat44::sIdentity();
        }
        const JPH::EActivation activate =
            motion == JPH::EMotionType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate;
        return bodies().CreateAndAddBody(settings, activate);
    }

    int addAnonymousBox(const Vec3 &center, const Vec3 &halfExtent, JPH::EMotionType motion, JPH::ObjectLayer layer)
    {
        if (!valid_)
            return 0;
        const JPH::RefConst<JPH::Shape> shape = makeBox(halfExtent);
        const JPH::BodyID id = createBody(shape, center, JPH::Quat::sIdentity(), motion, layer);
        if (id.IsInvalid())
            return 0;
        Slot slot;
        slot.id = id;
        slot.target = center;
        slot.kind = SolidKind::Box;
        slot.kinematic = motion == JPH::EMotionType::Kinematic;
        anonymous_.push_back(slot);
        dirtyBroadphase_ = true;
        return static_cast<int>(anonymous_.size());
    }

    bool sameKind(const Slot &slot, SolidKind kind, bool kinematic) const
    {
        return slot.kind == kind && slot.kinematic == kinematic;
    }

    void upsertSlot(EntityId id, SolidKind kind, bool kinematic, const JPH::RefConst<JPH::Shape> &shape,
        const Vec3 &center, JPH::QuatArg rot)
    {
        if (!valid_ || id == kInvalidEntityId || shape == nullptr)
            return;

        auto found = solids_.find(id);
        if (found != solids_.end() && sameKind(found->second, kind, kinematic))
        {
            Slot &slot = found->second;
            slot.seen = true;
            const Vec3 delta = slot.target - center;
            const bool moved = dot(delta, delta) > 1e-10;
            slot.target = center;
            const bool rotated = !quatNear(slot.targetRot, rot);
            slot.targetRot = rot;
            if (slot.kinematic)
                return;
            if (moved || rotated)
                bodies().SetPositionAndRotation(slot.id, toRVec(center), rot, JPH::EActivation::DontActivate);
            return;
        }

        if (found != solids_.end())
        {
            destroyBody(found->second.id);
            solids_.erase(found);
        }

        const JPH::EMotionType motion = kinematic ? JPH::EMotionType::Kinematic : JPH::EMotionType::Static;
        const JPH::ObjectLayer layer = kinematic ? Layers::kMoving : Layers::kNonMoving;
        const JPH::BodyID body = createBody(shape, center, rot, motion, layer);
        if (body.IsInvalid())
            return;
        Slot slot;
        slot.id = body;
        slot.target = center;
        slot.targetRot = rot;
        slot.kind = kind;
        slot.kinematic = kinematic;
        slot.seen = true;
        solids_[id] = slot;
        dirtyBroadphase_ = true;
    }

    void optimizeIfNeeded()
    {
        if (!dirtyBroadphase_)
            return;
        physics_.OptimizeBroadPhase();
        dirtyBroadphase_ = false;
    }

    void stepKinematics(float dt)
    {
        auto stepOne = [&](Slot &slot) {
            if (!slot.kinematic)
                return;
            const JPH::RVec3 current = bodies().GetPosition(slot.id);
            const JPH::RVec3 target = toRVec(slot.target);
            JPH::Vec3 velocity = JPH::Vec3::sZero();
            if (dt > 0.f)
            {
                const JPH::RVec3 delta = target - current;
                velocity = JPH::Vec3(static_cast<float>(delta.GetX() / dt), static_cast<float>(delta.GetY() / dt),
                    static_cast<float>(delta.GetZ() / dt));
            }
            bodies().SetLinearAndAngularVelocity(slot.id, velocity, JPH::Vec3::sZero());
            if (!quatNear(bodies().GetRotation(slot.id), slot.targetRot))
                bodies().SetRotation(slot.id, slot.targetRot, JPH::EActivation::Activate);
            bodies().ActivateBody(slot.id);
        };
        for (Slot &slot : anonymous_)
            stepOne(slot);
        for (auto &pair : solids_)
            stepOne(pair.second);
    }

    void stepCharacter(float dt)
    {
        if (character_ == nullptr)
            return;

        const JPH::Vec3 wish(static_cast<float>(wishX_), 0, static_cast<float>(wishZ_));
        const JPH::Vec3 gravity = physics_.GetGravity();
        JPH::Vec3 velocity = character_->GetLinearVelocity();
        const bool onGround = character_->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;

        if (onGround)
        {
            velocity = character_->GetGroundVelocity() + wish;
            if (jump_)
                velocity.SetY(static_cast<float>(kJumpSpeed));
            else
                velocity += gravity * dt;
        }
        else
        {
            velocity = JPH::Vec3(wish.GetX(), velocity.GetY(), wish.GetZ()) + gravity * dt;
        }
        jump_ = false;

        character_->SetLinearVelocity(character_->CancelVelocityTowardsSteepSlopes(velocity));

        JPH::CharacterVirtual::ExtendedUpdateSettings update;
        // Padding-aware: play's 0.35 curb still clears when the capsule keeps 0.02 of skin.
        // Stick-down must stay shorter than that curb, or a 1/30 step snaps back onto the plane.
        update.mWalkStairsStepUp = JPH::Vec3(0, static_cast<float>(kStepHeight) + 0.05f, 0);
        update.mStickToFloorStepDown = JPH::Vec3(0, -0.5f, 0);

        const JPH::BodyFilter bodyFilter;
        const JPH::ShapeFilter shapeFilter;
        character_->ExtendedUpdate(dt, gravity, update, physics_.GetDefaultBroadPhaseLayerFilter(Layers::kMoving),
            physics_.GetDefaultLayerFilter(Layers::kMoving), bodyFilter, shapeFilter, temp_);
    }

    bool valid_ = false;
    BPLayerInterface bpLayers_;
    ObjectVsBpFilter objectVsBp_;
    ObjectPairFilter objectPair_;
    JPH::TempAllocatorImpl temp_{10 * 1024 * 1024};
    JPH::PhysicsSystem physics_;
    JPH::Ref<JPH::CharacterVirtual> character_;
    std::vector<Slot> anonymous_;
    std::unordered_map<EntityId, Slot> solids_;
    std::unordered_map<MeshShapeKey, JPH::RefConst<JPH::Shape>, MeshShapeKeyHash> meshShapes_;
    std::unordered_map<HeightFieldKey, JPH::RefConst<JPH::Shape>, HeightFieldKeyHash> heightFields_;
    double wishX_ = 0;
    double wishZ_ = 0;
    double capsuleRadius_ = 0;
    bool jump_ = false;
    bool dirtyBroadphase_ = false;
};

namespace jolt_play
{

World::World() : impl_(std::make_unique<Impl>()) {}

World::~World() = default;

World::World(World &&) noexcept = default;

World &World::operator=(World &&) noexcept = default;

bool World::valid() const
{
    return impl_ && impl_->valid_;
}

int World::solidCount() const
{
    if (!valid())
        return 0;
    return static_cast<int>(impl_->solids_.size());
}

void World::addStaticBox(const Vec3 &center, const Vec3 &halfExtent)
{
    if (!valid())
        return;
    impl_->addAnonymousBox(center, halfExtent, JPH::EMotionType::Static, Layers::kNonMoving);
}

void World::addStaticMesh(const std::vector<Vec3> &vertices, const std::vector<std::uint32_t> &indices)
{
    if (!valid() || vertices.empty() || indices.size() < 3)
        return;

    const JPH::RefConst<JPH::Shape> shape = impl_->makeMesh(vertices, indices, Vec3(1, 1, 1));
    const JPH::BodyID id =
        impl_->createBody(shape, Vec3(), JPH::Quat::sIdentity(), JPH::EMotionType::Static, Layers::kNonMoving);
    if (id.IsInvalid())
        return;
    Impl::Slot slot;
    slot.id = id;
    slot.kind = SolidKind::Mesh;
    slot.kinematic = false;
    impl_->anonymous_.push_back(slot);
    impl_->dirtyBroadphase_ = true;
}

void World::addStaticHeightField(const TerrainTile &tile)
{
    if (!valid())
        return;
    const JPH::RefConst<JPH::Shape> shape = impl_->makeHeightField(&tile, 1);
    const JPH::BodyID id =
        impl_->createBody(shape, Vec3(), JPH::Quat::sIdentity(), JPH::EMotionType::Static, Layers::kNonMoving);
    if (id.IsInvalid())
        return;
    Impl::Slot slot;
    slot.id = id;
    slot.kind = SolidKind::HeightField;
    slot.kinematic = false;
    impl_->anonymous_.push_back(slot);
    impl_->dirtyBroadphase_ = true;
}

int World::addKinematicBox(const Vec3 &center, const Vec3 &halfExtent)
{
    if (!valid())
        return 0;
    return impl_->addAnonymousBox(center, halfExtent, JPH::EMotionType::Kinematic, Layers::kMoving);
}

void World::moveKinematicBox(int handle, const Vec3 &center)
{
    if (!valid() || handle < 1 || static_cast<size_t>(handle) > impl_->anonymous_.size())
        return;
    Impl::Slot &slot = impl_->anonymous_[static_cast<size_t>(handle) - 1];
    if (!slot.kinematic)
        return;
    slot.target = center;
}

void World::beginSolidSync()
{
    if (!valid())
        return;
    for (auto &pair : impl_->solids_)
        pair.second.seen = false;
}

void World::touchSolid(EntityId id)
{
    if (!valid() || id == kInvalidEntityId)
        return;
    auto found = impl_->solids_.find(id);
    if (found != impl_->solids_.end())
        found->second.seen = true;
}

void World::upsertBox(EntityId id, const Vec3 &center, const Vec3 &halfExtent, double qx, double qy, double qz,
    double qw, bool kinematic)
{
    if (!valid())
        return;
    impl_->upsertSlot(id, SolidKind::Box, kinematic, impl_->makeBox(halfExtent), center, toQuat(qx, qy, qz, qw));
}

void World::upsertSphere(EntityId id, const Vec3 &center, double radius, bool kinematic)
{
    if (!valid())
        return;
    auto found = impl_->solids_.find(id);
    if (found != impl_->solids_.end() && found->second.kind == SolidKind::Sphere && found->second.kinematic == kinematic
        && std::abs(found->second.radius - radius) <= 1e-8)
    {
        Impl::Slot &slot = found->second;
        slot.seen = true;
        const Vec3 delta = slot.target - center;
        const bool moved = dot(delta, delta) > 1e-10;
        slot.target = center;
        if (!kinematic && moved)
            impl_->bodies().SetPositionAndRotation(slot.id, toRVec(center), JPH::Quat::sIdentity(),
                JPH::EActivation::DontActivate);
        return;
    }
    impl_->upsertSlot(id, SolidKind::Sphere, kinematic, impl_->makeSphere(radius), center, JPH::Quat::sIdentity());
    auto slot = impl_->solids_.find(id);
    if (slot != impl_->solids_.end())
        slot->second.radius = radius;
}

void World::upsertPlane(EntityId id, const Vec3 &point, const Vec3 &normal)
{
    if (!valid())
        return;
    const double len = length(normal);
    if (len <= 1e-8)
        return;
    const Vec3 n = normal / len;
    const JPH::Quat rot = JPH::Quat::sFromTo(JPH::Vec3::sAxisY(), toVec(n));
    const Vec3 half(512, 0.5, 512);
    const Vec3 center = point - n * half.y;
    auto found = impl_->solids_.find(id);
    if (found != impl_->solids_.end() && found->second.kind == SolidKind::Plane)
    {
        Impl::Slot &slot = found->second;
        slot.seen = true;
        const Vec3 delta = slot.target - center;
        const bool moved = dot(delta, delta) > 1e-10;
        const bool rotated = !quatNear(slot.targetRot, rot);
        slot.target = center;
        slot.targetRot = rot;
        if (moved || rotated)
            impl_->bodies().SetPositionAndRotation(slot.id, toRVec(center), rot, JPH::EActivation::DontActivate);
        return;
    }
    impl_->upsertSlot(id, SolidKind::Plane, false, impl_->makeBox(half), center, rot);
}

void World::upsertMesh(EntityId id, const MeshGeometry *geometry, const Vec3 &position, double qx, double qy, double qz,
    double qw, const Vec3 &scale, bool kinematic)
{
    if (!valid() || geometry == nullptr || geometry->positions.empty() || geometry->indices.size() < 3)
        return;
    const JPH::Quat rot = toQuat(qx, qy, qz, qw);
    const Vec3 safeScale(
        scale.x > 1e-8 ? scale.x : 1,
        scale.y > 1e-8 ? scale.y : 1,
        scale.z > 1e-8 ? scale.z : 1);
    const std::uint64_t contentHash = geometry->contentHash == 0 ? 1 : geometry->contentHash;
    auto found = impl_->solids_.find(id);
    if (found != impl_->solids_.end() && found->second.kind == SolidKind::Mesh && found->second.kinematic == kinematic
        && found->second.meshContentHash == contentHash
        && std::abs(found->second.scale.x - safeScale.x) <= 1e-8
        && std::abs(found->second.scale.y - safeScale.y) <= 1e-8
        && std::abs(found->second.scale.z - safeScale.z) <= 1e-8)
    {
        Impl::Slot &slot = found->second;
        slot.seen = true;
        const Vec3 delta = slot.target - position;
        const bool moved = dot(delta, delta) > 1e-10;
        const bool rotated = !quatNear(slot.targetRot, rot);
        slot.target = position;
        slot.targetRot = rot;
        if (!kinematic && (moved || rotated))
            impl_->bodies().SetPositionAndRotation(slot.id, toRVec(position), rot, JPH::EActivation::DontActivate);
        return;
    }

    const JPH::RefConst<JPH::Shape> shape = impl_->meshShapeCached(geometry, safeScale);
    impl_->upsertSlot(id, SolidKind::Mesh, kinematic, shape, position, rot);
    auto slot = impl_->solids_.find(id);
    if (slot != impl_->solids_.end())
    {
        slot->second.meshContentHash = contentHash;
        slot->second.scale = safeScale;
    }
}

void World::upsertHeightField(EntityId id, const TerrainTile *tile, const Vec3 &origin, double qx, double qy, double qz,
    double qw, double scale)
{
    if (!valid() || tile == nullptr)
        return;
    const JPH::Quat rot = toQuat(qx, qy, qz, qw);
    const double safe = scale > 1e-8 ? scale : 1;
    auto found = impl_->solids_.find(id);
    if (found != impl_->solids_.end() && found->second.kind == SolidKind::HeightField
        && std::abs(found->second.scale.x - safe) <= 1e-8
        && std::abs(found->second.scale.y - safe) <= 1e-8
        && std::abs(found->second.scale.z - safe) <= 1e-8)
    {
        Impl::Slot &slot = found->second;
        slot.seen = true;
        const Vec3 delta = slot.target - origin;
        const bool moved = dot(delta, delta) > 1e-10;
        const bool rotated = !quatNear(slot.targetRot, rot);
        slot.target = origin;
        slot.targetRot = rot;
        if (moved || rotated)
            impl_->bodies().SetPositionAndRotation(slot.id, toRVec(origin), rot, JPH::EActivation::DontActivate);
        return;
    }
    const JPH::RefConst<JPH::Shape> shape = impl_->heightFieldCached(tile, safe);
    impl_->upsertSlot(id, SolidKind::HeightField, false, shape, origin, rot);
    auto slot = impl_->solids_.find(id);
    if (slot != impl_->solids_.end())
        slot->second.scale = Vec3(safe, safe, safe);
}

void World::endSolidSync()
{
    if (!valid())
        return;
    for (auto it = impl_->solids_.begin(); it != impl_->solids_.end();)
    {
        if (it->second.seen)
        {
            ++it;
            continue;
        }
        impl_->destroyBody(it->second.id);
        it = impl_->solids_.erase(it);
        impl_->dirtyBroadphase_ = true;
    }
}

void World::spawnCapsule(const Vec3 &feet, double radius, double cylinderHalfHeight)
{
    if (!valid())
        return;

    const float r = static_cast<float>(radius);
    const float half = static_cast<float>(cylinderHalfHeight);
    if (r <= 0.f || half < 0.f)
        return;

    JPH::RefConst<JPH::Shape> capsule = new JPH::CapsuleShape(half, r);
    JPH::RotatedTranslatedShapeSettings offset(JPH::Vec3(0, half + r, 0), JPH::Quat::sIdentity(), capsule);
    offset.SetEmbedded();
    const JPH::Shape::ShapeResult shaped = offset.Create();
    if (shaped.HasError())
        return;

    JPH::CharacterVirtualSettings settings;
    settings.mID = JPH::CharacterID(1);
    settings.mShape = shaped.Get();
    settings.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -r);
    settings.mMaxSlopeAngle = JPH::DegreesToRadians(50.0f);
    settings.mCharacterPadding = 0.02f;
    settings.mPredictiveContactDistance = 0.25f;

    impl_->character_ = new JPH::CharacterVirtual(&settings, toRVec(feet), JPH::Quat::sIdentity(), &impl_->physics_);
    impl_->capsuleRadius_ = radius;
}

void World::refreshCapsuleContacts()
{
    if (!hasCapsule())
        return;
    impl_->optimizeIfNeeded();
    const JPH::BodyFilter bodyFilter;
    const JPH::ShapeFilter shapeFilter;
    impl_->character_->RefreshContacts(impl_->physics_.GetDefaultBroadPhaseLayerFilter(Layers::kMoving),
        impl_->physics_.GetDefaultLayerFilter(Layers::kMoving), bodyFilter, shapeFilter, impl_->temp_);
    impl_->character_->UpdateGroundVelocity();
}

void World::setCapsuleFeet(const Vec3 &feet)
{
    if (!hasCapsule())
        return;
    impl_->character_->SetPosition(toRVec(feet));
    impl_->character_->SetLinearVelocity(JPH::Vec3::sZero());
}

void World::setWishVelocityXZ(double vx, double vz)
{
    if (!valid())
        return;
    impl_->wishX_ = vx;
    impl_->wishZ_ = vz;
}

void World::requestJump()
{
    if (!valid())
        return;
    impl_->jump_ = true;
}

void World::step(float dt)
{
    if (!valid() || dt <= 0.f)
        return;
    impl_->optimizeIfNeeded();
    impl_->stepKinematics(dt);
    if (jobs::joltJobSystem() != nullptr)
        impl_->physics_.Update(dt, 1, &impl_->temp_, jobs::joltJobSystem());
    if (impl_->character_ != nullptr)
        impl_->character_->UpdateGroundVelocity();
    constexpr float kMaxSub = 1.f / 60.f;
    int subs = 1;
    if (dt > kMaxSub)
        subs = std::min(4, static_cast<int>(std::ceil(dt / kMaxSub)));
    const float subDt = dt / static_cast<float>(subs);
    for (int i = 0; i < subs; ++i)
        impl_->stepCharacter(subDt);
}

bool World::hasCapsule() const
{
    return valid() && impl_->character_ != nullptr;
}

bool World::grounded() const
{
    return hasCapsule() && impl_->character_->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
}

Vec3 World::capsuleFeet() const
{
    if (!hasCapsule())
        return {};
    return fromRVec(impl_->character_->GetPosition());
}

double World::capsuleRadius() const
{
    if (!hasCapsule())
        return 0;
    return impl_->capsuleRadius_;
}

double World::verticalVelocity() const
{
    if (!hasCapsule())
        return 0;
    return impl_->character_->GetLinearVelocity().GetY();
}

Vec3 World::capsuleVelocity() const
{
    if (!hasCapsule())
        return {};
    const JPH::Vec3 v = impl_->character_->GetLinearVelocity();
    return Vec3(v.GetX(), v.GetY(), v.GetZ());
}

void World::setCapsuleVelocity(const Vec3 &velocity)
{
    if (!hasCapsule())
        return;
    impl_->character_->SetLinearVelocity(toVec(velocity));
}

std::vector<std::uint8_t> World::captureCharacterState() const
{
    if (!hasCapsule())
        return {};
    JPH::StateRecorderImpl recorder;
    impl_->character_->SaveState(recorder);
    const std::string data = recorder.GetData();
    return std::vector<std::uint8_t>(data.begin(), data.end());
}

bool World::restoreCharacterState(const std::vector<std::uint8_t> &bytes)
{
    if (!hasCapsule() || bytes.empty())
        return false;
    JPH::StateRecorderImpl recorder;
    recorder.WriteBytes(bytes.data(), bytes.size());
    recorder.Rewind();
    impl_->character_->RestoreState(recorder);
    return !recorder.IsFailed();
}

std::vector<std::uint8_t> World::capturePhysicsState() const
{
    if (!valid())
        return {};
    JPH::StateRecorderImpl recorder;
    impl_->physics_.SaveState(recorder);
    const std::string data = recorder.GetData();
    return std::vector<std::uint8_t>(data.begin(), data.end());
}

bool World::restorePhysicsState(const std::vector<std::uint8_t> &bytes)
{
    if (!valid() || bytes.empty())
        return false;
    JPH::StateRecorderImpl recorder;
    recorder.WriteBytes(bytes.data(), bytes.size());
    recorder.Rewind();
    if (!impl_->physics_.RestoreState(recorder) || recorder.IsFailed())
        return false;
    impl_->dirtyBroadphase_ = true;
    impl_->optimizeIfNeeded();
    return true;
}

} // namespace jolt_play
