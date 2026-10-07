#pragma once

#include "Components.hpp"
#include "Play.hpp"
#include "SimChannel.hpp"

#include <cstdint>
#include <string>
#include <vector>

// Versioned bitsery blobs for commands, snapshots, events, and the sim
// components needed to restore a play session. SQLite (SimSave.hpp) stores
// these same bytes; this layer does not open a database.

inline constexpr std::size_t kBitseryMaxString = 65535;
inline constexpr std::size_t kBitseryMaxPoses = 1u << 16;
inline constexpr std::size_t kBitseryMaxCommands = 1u << 20;
inline constexpr std::size_t kBitseryMaxEntities = 1u << 16;
inline constexpr std::size_t kBitseryMaxEvents = 1u << 16;
inline constexpr std::size_t kBitseryMaxRests = 1u << 16;
inline constexpr std::size_t kBitseryMaxTweens = 1024;
inline constexpr std::size_t kBitseryMaxCharacterState = 1u << 16;
inline constexpr std::size_t kBitseryMaxPhysicsState = 1u << 20;

enum class SimShapeKind : std::uint8_t
{
    None = 0,
    Sphere = 1,
    Plane = 2,
    Mesh = 3,
    Terrain = 4,
};

struct SimEntityRecord
{
    EntityId id = kInvalidEntityId;
    EntityId parent = kInvalidEntityId;
    Vec3 position;
    double qw = 1;
    double qx = 0;
    double qy = 0;
    double qz = 0;
    Vec3 scale{1, 1, 1};
    SimShapeKind shape = SimShapeKind::None;
    double radius = 1;
    Vec3 planeNormal{0, 1, 0};
    bool checker = false;
    Vec3 checkerAlbedo{0.2, 0.2, 0.22};
    double checkerScale = 1;
    std::string meshSourcePath;
    std::int64_t meshSourceStamp = 0;
    std::uint64_t meshSourceBytes = 0;
    Motion motion;
    Action action;
    double spawnEvery = 0;
    int spawnClock = 0;
    EntityId spawnedId = kInvalidEntityId;
    std::string name;
    std::string tag;
    int layer = 0;
    Material material;
    int tileX = 0;
    int tileZ = 0;
};

struct SimPlayBlob
{
    std::uint16_t version = kSimPlayBitseryVersion;
    SimTick tick = 0;
    EntityId nextId = 1;
    Vec3 lastLook{0, 0, -1};
    bool paused = false;
    int score = 0;
    int health = 3;
    std::string message;
    EntityId playerId = kInvalidEntityId;
    float verticalVelocity = 0;
    std::string result;
    int room = 0;
    int motionTime = 0;
    std::vector<RestPose> rests;
    std::vector<Tween> tweens;
    EntityId lookId = kInvalidEntityId;
    std::string lookName;
    std::string lookTag;
    Vec3 lookPoint;
    Vec3 eventAt;
    std::uint32_t simSeed = 1;
    std::string rngState;
    std::vector<SimEntityRecord> entities;
    Snapshot snapshot;
    bool hasCapsule = false;
    Vec3 capsuleFeet;
    Vec3 capsuleVelocity;
    double capsuleRadius = 0.35;
    bool capsuleGrounded = false;
    std::vector<std::uint8_t> capsuleState;
    std::vector<std::uint8_t> physicsState;
};

bool encodeCommand(const Command &value, std::vector<std::uint8_t> &out);
bool decodeCommand(const std::vector<std::uint8_t> &in, Command &value);

bool encodeSnapshot(const Snapshot &value, std::vector<std::uint8_t> &out);
bool decodeSnapshot(const std::vector<std::uint8_t> &in, Snapshot &value);

bool encodeSimEvent(const SimEvent &value, std::vector<std::uint8_t> &out);
bool decodeSimEvent(const std::vector<std::uint8_t> &in, SimEvent &value);

bool encodeCommandList(const std::vector<Command> &value, std::vector<std::uint8_t> &out);
bool decodeCommandList(const std::vector<std::uint8_t> &in, std::vector<Command> &value);

bool encodeSimEntityRecord(const SimEntityRecord &value, std::vector<std::uint8_t> &out);
bool decodeSimEntityRecord(const std::vector<std::uint8_t> &in, SimEntityRecord &value);

bool encodeSimPlayBlob(const SimPlayBlob &value, std::vector<std::uint8_t> &out);
bool decodeSimPlayBlob(const std::vector<std::uint8_t> &in, SimPlayBlob &value);

SimPlayBlob captureSimPlay(const Scene &scene, const PlayState &state, SimTick tick, const Vec3 &lastLook);
bool applySimPlay(Scene &scene, PlayState &state, const SimPlayBlob &blob);

bool playStateFieldsEqual(const PlayState &a, const PlayState &b);
bool sceneTransformsEqual(const Scene &a, const Scene &b);
bool simPlayBlobsEqual(const SimPlayBlob &a, const SimPlayBlob &b);
