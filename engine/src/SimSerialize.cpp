#include "SimSerialize.hpp"

#include "Content.hpp"
#include "Object.hpp"
#include "Scene.hpp"
#include "Stream.hpp"
#include "TransformMath.hpp"

#include <glm/gtc/quaternion.hpp>

#include <bitsery/adapter/buffer.h>
#include <bitsery/bitsery.h>
#include <bitsery/ext/growable.h>
#include <bitsery/traits/string.h>
#include <bitsery/traits/vector.h>

#include <cstring>
#include <filesystem>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace
{

constexpr std::size_t kMaxString = kBitseryMaxString;

template <typename T>
bool bitsEqual(const T &a, const T &b)
{
    return std::memcmp(&a, &b, sizeof(T)) == 0;
}

bool vecEqual(const Vec3 &a, const Vec3 &b)
{
    return bitsEqual(a.x, b.x) && bitsEqual(a.y, b.y) && bitsEqual(a.z, b.z);
}

std::string rngStateOf(const std::mt19937 &rng)
{
    std::ostringstream out;
    out << rng;
    return out.str();
}

bool restoreRng(std::mt19937 &rng, const std::string &text)
{
    if (text.empty())
    {
        rng.seed(1);
        return false;
    }
    std::istringstream in(text);
    in >> rng;
    return static_cast<bool>(in);
}

template <typename S>
void serVersion(S &s, std::uint16_t expected)
{
    std::uint16_t version = expected;
    s.value2b(version);
    if (version != expected)
    {
        if constexpr (requires { s.adapter().error(bitsery::ReaderError::InvalidData); })
            s.adapter().error(bitsery::ReaderError::InvalidData);
    }
}

template <typename T>
bool encodeObject(const T &value, std::vector<std::uint8_t> &out)
{
    out.clear();
    using OutputAdapter = bitsery::OutputBufferAdapter<std::vector<std::uint8_t>>;
    const std::size_t written = bitsery::quickSerialization<OutputAdapter>(OutputAdapter{out}, value);
    out.resize(written);
    return true;
}

template <typename T>
bool decodeObject(const std::vector<std::uint8_t> &in, T &value)
{
    if (in.empty())
        return false;
    using InputAdapter = bitsery::InputBufferAdapter<std::vector<std::uint8_t>>;
    const auto result = bitsery::quickDeserialization<InputAdapter>(InputAdapter{in.begin(), in.size()}, value);
    return result.first == bitsery::ReaderError::NoError && result.second;
}

struct CommandList
{
    std::vector<Command> commands;
};

template <typename S>
void serialize(S &s, CommandList &value)
{
    serVersion(s, kCommandBitseryVersion);
    s.container(value.commands, kBitseryMaxCommands);
}

} // namespace

template <typename S>
void serialize(S &s, Vec3 &value)
{
    s.value8b(value.x);
    s.value8b(value.y);
    s.value8b(value.z);
}

template <typename S>
void serialize(S &s, Command &value)
{
    serVersion(s, kCommandBitseryVersion);
    s.ext(value, bitsery::ext::Growable{}, [](S &s, Command &value) {
        s.value4b(value.tick);
        s.value4b(value.moveX);
        s.value4b(value.moveZ);
        s.boolValue(value.jump);
        s.boolValue(value.use);
        s.object(value.look);
    });
}

namespace glm
{

template <typename S>
void serialize(S &s, dquat &value)
{
    s.value8b(value.w);
    s.value8b(value.x);
    s.value8b(value.y);
    s.value8b(value.z);
}

template <typename S>
void serialize(S &s, dvec3 &value)
{
    s.value8b(value.x);
    s.value8b(value.y);
    s.value8b(value.z);
}

} // namespace glm

template <typename S>
void serialize(S &s, SnapshotPose &value)
{
    s.value8b(value.id);
    s.object(value.position);
    s.object(value.rotation);
    s.object(value.scale);
}

template <typename S>
void serialize(S &s, Snapshot &value)
{
    serVersion(s, kSnapshotBitseryVersion);
    s.ext(value, bitsery::ext::Growable{}, [](S &s, Snapshot &value) {
        s.value4b(value.tick);
        s.value8b(value.playerId);
        s.container(value.poses, kBitseryMaxPoses);
        s.value4b(value.score);
        s.value4b(value.health);
        s.boolValue(value.paused);
        s.text1b(value.message, kMaxString);
        s.text1b(value.result, kMaxString);
        s.value8b(value.lookId);
        s.text1b(value.lookName, kMaxString);
        s.text1b(value.lookTag, kMaxString);
        s.object(value.lookPoint);
    });
}

template <typename S>
void serialize(S &s, SimEvent &value)
{
    serVersion(s, kSimEventBitseryVersion);
    s.ext(value, bitsery::ext::Growable{}, [](S &s, SimEvent &value) {
        s.value1b(value.kind);
        s.value8b(value.id);
        s.object(value.position);
        s.value4b(value.tick);
    });
}

template <typename S>
void serialize(S &s, Motion &value)
{
    s.object(value.move);
    s.object(value.rotate);
    s.value8b(value.scale);
    s.value8b(value.period);
}

template <typename S>
void serialize(S &s, Action &value)
{
    s.text1b(value.target, kMaxString);
    s.object(value.move);
    s.object(value.rotate);
    s.boolValue(value.fired);
}

template <typename S>
void serialize(S &s, RestPose &value)
{
    s.value8b(value.id);
    s.object(value.position);
    s.object(value.rotation);
    s.object(value.scale);
}

template <typename S>
void serialize(S &s, Tween &value)
{
    s.value8b(value.targetId);
    s.object(value.origin);
    s.object(value.originRotation);
    s.object(value.move);
    s.object(value.rotate);
    s.value4b(value.time);
}

template <typename S>
void serialize(S &s, Material &value)
{
    s.object(value.albedo);
    s.value8b(value.ambient);
    s.value8b(value.diffuse);
    s.value8b(value.specular);
    s.value8b(value.shininess);
    s.value8b(value.reflectivity);
    s.value8b(value.transmission);
    s.value8b(value.ior);
    s.value8b(value.uvScale);
    s.value8b(value.uvScrollU);
    s.value8b(value.uvScrollV);
    s.value8b(value.roughness);
    s.value8b(value.emission);
    s.text1b(value.albedoMap, kMaxString);
    s.text1b(value.normalMap, kMaxString);
}

template <typename S>
void serialize(S &s, SimEntityRecord &value)
{
    s.ext(value, bitsery::ext::Growable{}, [](S &s, SimEntityRecord &value) {
        s.value8b(value.id);
        s.value8b(value.parent);
        s.object(value.position);
        s.value8b(value.qw);
        s.value8b(value.qx);
        s.value8b(value.qy);
        s.value8b(value.qz);
        s.object(value.scale);
        s.value1b(value.shape);
        s.value8b(value.radius);
        s.object(value.planeNormal);
        s.boolValue(value.checker);
        s.object(value.checkerAlbedo);
        s.value8b(value.checkerScale);
        s.text1b(value.meshSourcePath, kMaxString);
        s.value8b(value.meshSourceStamp);
        s.value8b(value.meshSourceBytes);
        s.object(value.motion);
        s.object(value.action);
        s.value8b(value.spawnEvery);
        s.value4b(value.spawnClock);
        s.value8b(value.spawnedId);
        s.text1b(value.name, kMaxString);
        s.text1b(value.tag, kMaxString);
        s.value4b(value.layer);
        s.object(value.material);
        s.value4b(value.tileX);
        s.value4b(value.tileZ);
    });
}

template <typename S>
void serialize(S &s, SimPlayBlob &value)
{
    serVersion(s, kSimPlayBitseryVersion);
    s.ext(value, bitsery::ext::Growable{}, [](S &s, SimPlayBlob &value) {
        s.value2b(value.version);
        s.value4b(value.tick);
        s.value8b(value.nextId);
        s.object(value.lastLook);
        s.boolValue(value.paused);
        s.value4b(value.score);
        s.value4b(value.health);
        s.text1b(value.message, kMaxString);
        s.value8b(value.playerId);
        s.value4b(value.verticalVelocity);
        s.text1b(value.result, kMaxString);
        s.value4b(value.room);
        s.value4b(value.motionTime);
        s.container(value.rests, kBitseryMaxRests);
        s.container(value.tweens, kBitseryMaxTweens);
        s.value8b(value.lookId);
        s.text1b(value.lookName, kMaxString);
        s.text1b(value.lookTag, kMaxString);
        s.object(value.lookPoint);
        s.object(value.eventAt);
        s.value4b(value.simSeed);
        s.text1b(value.rngState, kMaxString);
        s.container(value.entities, kBitseryMaxEntities);
        s.object(value.snapshot);
        s.boolValue(value.hasCapsule);
        s.object(value.capsuleFeet);
        s.object(value.capsuleVelocity);
        s.value8b(value.capsuleRadius);
        s.boolValue(value.capsuleGrounded);
        s.container1b(value.capsuleState, kBitseryMaxCharacterState);
        s.container1b(value.physicsState, kBitseryMaxPhysicsState);
    });
}

bool encodeCommand(const Command &value, std::vector<std::uint8_t> &out)
{
    return encodeObject(value, out);
}

bool decodeCommand(const std::vector<std::uint8_t> &in, Command &value)
{
    return decodeObject(in, value);
}

bool encodeSnapshot(const Snapshot &value, std::vector<std::uint8_t> &out)
{
    return encodeObject(value, out);
}

bool decodeSnapshot(const std::vector<std::uint8_t> &in, Snapshot &value)
{
    return decodeObject(in, value);
}

bool encodeSimEvent(const SimEvent &value, std::vector<std::uint8_t> &out)
{
    return encodeObject(value, out);
}

bool decodeSimEvent(const std::vector<std::uint8_t> &in, SimEvent &value)
{
    return decodeObject(in, value);
}

bool encodeCommandList(const std::vector<Command> &value, std::vector<std::uint8_t> &out)
{
    CommandList list;
    list.commands = value;
    return encodeObject(list, out);
}

bool decodeCommandList(const std::vector<std::uint8_t> &in, std::vector<Command> &value)
{
    CommandList list;
    if (!decodeObject(in, list))
        return false;
    value = std::move(list.commands);
    return true;
}

bool encodeSimEntityRecord(const SimEntityRecord &value, std::vector<std::uint8_t> &out)
{
    return encodeObject(value, out);
}

bool decodeSimEntityRecord(const std::vector<std::uint8_t> &in, SimEntityRecord &value)
{
    return decodeObject(in, value);
}

bool encodeSimPlayBlob(const SimPlayBlob &value, std::vector<std::uint8_t> &out)
{
    return encodeObject(value, out);
}

bool decodeSimPlayBlob(const std::vector<std::uint8_t> &in, SimPlayBlob &value)
{
    return decodeObject(in, value);
}

SimPlayBlob captureSimPlay(const Scene &scene, const PlayState &state, SimTick tick, const Vec3 &lastLook)
{
    SimPlayBlob blob;
    blob.version = kSimPlayBitseryVersion;
    blob.tick = tick;
    blob.nextId = scene.nextId();
    blob.lastLook = lastLook;
    blob.paused = state.paused;
    blob.score = state.score;
    blob.health = state.health;
    blob.message = state.message;
    blob.playerId = state.playerId;
    blob.verticalVelocity = state.verticalVelocity;
    blob.result = state.result;
    blob.room = state.room;
    blob.motionTime = state.motionTime;
    blob.rests = state.rests;
    blob.tweens = state.tweens;
    blob.lookId = state.lookId;
    blob.lookName = state.lookName;
    blob.lookTag = state.lookTag;
    blob.lookPoint = state.lookPoint;
    blob.eventAt = state.eventAt;
    blob.simSeed = state.simSeed;
    blob.rngState = rngStateOf(state.simRng);
    blob.snapshot = captureSnapshot(scene, state, tick);
    blob.entities.reserve(scene.objects().size());
    for (const Object *object : scene.objects())
    {
        SimEntityRecord record;
        record.id = object->id();
        record.parent = object->parentId();
        const Transform &transform = scene.registry().get<Transform>(scene.entity(object->id()));
        record.position = toVec3(transform.position);
        record.qw = transform.rotation.w;
        record.qx = transform.rotation.x;
        record.qy = transform.rotation.y;
        record.qz = transform.rotation.z;
        record.scale = toVec3(transform.scale);
        if (object->isSphere())
        {
            record.shape = SimShapeKind::Sphere;
            record.radius = object->radius();
        }
        else if (object->isPlane())
        {
            record.shape = SimShapeKind::Plane;
            record.planeNormal = object->normal();
            record.checker = object->checker();
            record.checkerAlbedo = object->checkerAlbedo();
            record.checkerScale = object->checkerScale();
        }
        else if (object->isTerrain())
        {
            record.shape = SimShapeKind::Terrain;
            if (const auto &tile = object->terrainTile())
            {
                record.tileX = tile->tileX;
                record.tileZ = tile->tileZ;
            }
            const MeshShape *mesh = scene.registry().try_get<MeshShape>(scene.entity(object->id()));
            if (mesh != nullptr)
            {
                record.meshSourcePath = mesh->sourcePath;
                record.meshSourceStamp = mesh->sourceStamp;
                record.meshSourceBytes = static_cast<std::uint64_t>(mesh->sourceBytes);
            }
        }
        else if (object->isMesh())
        {
            record.shape = SimShapeKind::Mesh;
            const MeshShape *mesh = scene.registry().try_get<MeshShape>(scene.entity(object->id()));
            if (mesh != nullptr)
            {
                record.meshSourcePath = mesh->sourcePath;
                record.meshSourceStamp = mesh->sourceStamp;
                record.meshSourceBytes = static_cast<std::uint64_t>(mesh->sourceBytes);
            }
        }
        record.motion = object->motion();
        record.action = object->action();
        record.spawnEvery = object->spawnEvery();
        record.spawnClock = object->spawnClock();
        record.spawnedId = object->spawnedId();
        record.name = object->name();
        record.tag = object->tag();
        record.layer = object->layer();
        record.material = object->material();
        blob.entities.push_back(std::move(record));
    }
    if (state.physics && state.physics->hasCapsule())
    {
        blob.hasCapsule = true;
        blob.capsuleFeet = state.physics->capsuleFeet();
        blob.capsuleVelocity = state.physics->capsuleVelocity();
        blob.capsuleRadius = state.physics->capsuleRadius();
        blob.capsuleGrounded = state.physics->grounded();
        blob.capsuleState = state.physics->captureCharacterState();
        blob.physicsState = state.physics->capturePhysicsState();
    }
    return blob;
}

bool applySimPlay(Scene &scene, PlayState &state, const SimPlayBlob &blob)
{
    auto terrainSourceOk = [&](const SimEntityRecord &record) {
        Object *existing = scene.find(record.id);
        if (existing != nullptr && existing->isTerrain())
            return true;
        const std::string virtualPath = streamTileVirtualPath(record.tileX, record.tileZ);
        if (contentExists(virtualPath))
            return true;
        if (record.meshSourcePath.empty())
            return false;
        if (contentExists(record.meshSourcePath))
            return true;
        std::error_code error;
        return std::filesystem::exists(std::filesystem::path(record.meshSourcePath), error) && !error;
    };

    for (const SimEntityRecord &record : blob.entities)
    {
        if (record.shape == SimShapeKind::Mesh)
        {
            if (record.meshSourcePath.empty())
                return false;
            Object *existing = scene.find(record.id);
            if (existing != nullptr && existing->isMesh())
                continue;
            std::filesystem::path path(record.meshSourcePath);
            std::error_code error;
            if (!std::filesystem::exists(path, error) || error)
                return false;
        }
        else if (record.shape == SimShapeKind::Terrain)
        {
            if (!terrainSourceOk(record))
                return false;
        }
    }

    std::unordered_set<EntityId> keep;
    keep.reserve(blob.entities.size());
    for (const SimEntityRecord &record : blob.entities)
        keep.insert(record.id);

    std::vector<EntityId> extras;
    for (const Object *object : scene.objects())
    {
        if (!keep.contains(object->id()))
            extras.push_back(object->id());
    }
    for (EntityId id : extras)
        scene.remove(id);

    for (const SimEntityRecord &record : blob.entities)
    {
        Object *object = scene.find(record.id);
        if (object == nullptr)
        {
            if (record.shape == SimShapeKind::Sphere)
                object = scene.addSphere(record.position, record.radius, record.material, record.id);
            else if (record.shape == SimShapeKind::Plane)
                object = scene.addPlane(record.position, record.planeNormal, record.material, record.id);
            else if (record.shape == SimShapeKind::Terrain)
            {
                object = scene.addTerrain(record.id);
                std::string terrainError;
                const std::string virtualPath = streamTileVirtualPath(record.tileX, record.tileZ);
                bool loaded = false;
                if (contentExists(virtualPath))
                    loaded = object != nullptr && object->loadTerrain(virtualPath, terrainError);
                else if (!record.meshSourcePath.empty())
                    loaded = object != nullptr && object->loadTerrain(record.meshSourcePath, terrainError);
                if (!loaded)
                    return false;
            }
            else if (record.shape == SimShapeKind::Mesh)
            {
                object = scene.addMesh(record.id);
                std::string meshError;
                if (object == nullptr || !object->loadMesh(record.meshSourcePath, meshError))
                    return false;
            }
            else
                continue;
        }
        Transform &transform = scene.registry().get<Transform>(scene.entity(object->id()));
        transform.parent = record.parent;
        transform.position = toGlm(record.position);
        transform.rotation = glm::dquat(record.qw, record.qx, record.qy, record.qz);
        transform.scale = toGlm(record.scale);
        markTransformDirty(transform);
        scene.markPhysicsDirty(object->id());
        if (object->isSphere())
            object->setRadius(record.radius);
        if (object->isPlane())
        {
            object->setNormal(record.planeNormal);
            object->setCheckerEnabled(record.checker);
            object->setCheckerAlbedo(record.checkerAlbedo);
            object->setCheckerScale(record.checkerScale);
        }
        object->motion() = record.motion;
        object->action() = record.action;
        object->spawnEvery() = record.spawnEvery;
        object->spawnClock() = record.spawnClock;
        object->spawnedId() = record.spawnedId;
        object->name() = record.name;
        object->setTag(record.tag);
        object->layer() = record.layer;
        object->setMaterial(record.material);
    }
    scene.restoreIdState(blob.nextId);
    scene.bumpParentFrames();

    state.paused = blob.paused;
    state.score = blob.score;
    state.health = blob.health;
    state.message = blob.message;
    state.playerId = blob.playerId;
    state.verticalVelocity = blob.verticalVelocity;
    state.result = blob.result;
    state.room = blob.room;
    state.motionTime = blob.motionTime;
    state.rests = blob.rests;
    state.tweens = blob.tweens;
    state.lookId = blob.lookId;
    state.lookName = blob.lookName;
    state.lookTag = blob.lookTag;
    state.lookPoint = blob.lookPoint;
    state.eventAt = blob.eventAt;
    state.simSeed = blob.simSeed;
    if (!restoreRng(state.simRng, blob.rngState))
        seedPlayRng(state);
    state.physics.reset();
    state.restoreCapsule = blob.hasCapsule;
    state.restoreVelocity = blob.capsuleVelocity;
    state.restoreFeet = blob.capsuleFeet;
    state.restoreRadius = blob.capsuleRadius;
    state.restoreCharacterState = blob.capsuleState;
    state.restorePhysicsState = blob.physicsState;
    primePlayPhysics(scene, state);
    return true;
}

bool playStateFieldsEqual(const PlayState &a, const PlayState &b)
{
    return a.paused == b.paused && a.score == b.score && a.health == b.health && a.message == b.message
        && a.playerId == b.playerId && bitsEqual(a.verticalVelocity, b.verticalVelocity) && a.result == b.result
        && a.room == b.room && a.motionTime == b.motionTime && a.lookId == b.lookId && a.lookName == b.lookName
        && a.lookTag == b.lookTag && vecEqual(a.lookPoint, b.lookPoint) && vecEqual(a.eventAt, b.eventAt)
        && a.simSeed == b.simSeed && rngStateOf(a.simRng) == rngStateOf(b.simRng) && a.rests.size() == b.rests.size()
        && a.tweens.size() == b.tweens.size();
}

bool sceneTransformsEqual(const Scene &a, const Scene &b)
{
    if (a.objects().size() != b.objects().size())
        return false;
    for (const Object *object : a.objects())
    {
        const Object *other = b.find(object->id());
        if (other == nullptr)
            return false;
        const Transform &ta = a.registry().get<Transform>(a.entity(object->id()));
        const Transform &tb = b.registry().get<Transform>(b.entity(other->id()));
        if (ta.parent != tb.parent)
            return false;
        if (!vecEqual(toVec3(ta.position), toVec3(tb.position)))
            return false;
        if (!bitsEqual(ta.rotation.w, tb.rotation.w) || !bitsEqual(ta.rotation.x, tb.rotation.x)
            || !bitsEqual(ta.rotation.y, tb.rotation.y) || !bitsEqual(ta.rotation.z, tb.rotation.z))
            return false;
        if (!vecEqual(toVec3(ta.scale), toVec3(tb.scale)))
            return false;
    }
    return true;
}

bool simPlayBlobsEqual(const SimPlayBlob &a, const SimPlayBlob &b)
{
    if (a.tick != b.tick || a.nextId != b.nextId || a.score != b.score || a.health != b.health
        || a.playerId != b.playerId || a.simSeed != b.simSeed || a.rngState != b.rngState
        || a.entities.size() != b.entities.size() || a.message != b.message || a.result != b.result)
        return false;
    if (!vecEqual(a.lastLook, b.lastLook) || !bitsEqual(a.verticalVelocity, b.verticalVelocity))
        return false;
    if (a.hasCapsule != b.hasCapsule || !vecEqual(a.capsuleFeet, b.capsuleFeet)
        || !vecEqual(a.capsuleVelocity, b.capsuleVelocity) || !bitsEqual(a.capsuleRadius, b.capsuleRadius)
        || a.capsuleGrounded != b.capsuleGrounded || a.capsuleState != b.capsuleState
        || a.physicsState != b.physicsState)
        return false;
    for (std::size_t i = 0; i < a.entities.size(); ++i)
    {
        const SimEntityRecord &ea = a.entities[i];
        const SimEntityRecord &eb = b.entities[i];
        if (ea.id != eb.id || ea.parent != eb.parent || !vecEqual(ea.position, eb.position)
            || !bitsEqual(ea.qw, eb.qw) || !bitsEqual(ea.qx, eb.qx) || !bitsEqual(ea.qy, eb.qy)
            || !bitsEqual(ea.qz, eb.qz) || !vecEqual(ea.scale, eb.scale))
            return false;
    }
    return true;
}
