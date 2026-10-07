#include "Play.hpp"
#include "PlayDetail.hpp"

#include "DemoScene.hpp"
#include "EngineSettings.hpp"
#include "JoltPlay.hpp"
#include "MeshGeometry.hpp"
#include "Stream.hpp"
#include "TransformMath.hpp"
#include <tracy/Tracy.hpp>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

#include <glm/gtc/quaternion.hpp>

namespace
{

using namespace play_detail;

bool overlapsShape(const Object &object, const Vec3 &center, double radius)
{
    return object.overlapsSphere(center, radius);
}

// Tags the player walks through. They must not steal the look ray from a use or goal.
bool passThroughLook(const Object &object)
{
    object.ensureRole();
    return object.role() && object.role()->passThroughLook();
}

void worldQuat(const Object &object, double &qx, double &qy, double &qz, double &qw)
{
    Vec3 axisX;
    Vec3 axisY;
    Vec3 axisZ;
    object.worldAxes(axisX, axisY, axisZ);
    const glm::dmat3 axes(glm::dvec3(axisX.x, axisX.y, axisX.z), glm::dvec3(axisY.x, axisY.y, axisY.z),
        glm::dvec3(axisZ.x, axisZ.y, axisZ.z));
    const glm::dquat q = glm::normalize(glm::quat_cast(axes));
    qx = q.x;
    qy = q.y;
    qz = q.z;
    qw = q.w;
}

bool isTweenTarget(const PlayState &state, EntityId id)
{
    for (const Tween &tween : state.tweens)
    {
        if (tween.targetId == id)
            return true;
    }
    return false;
}

void syncJoltSolids(jolt_play::World &world, Scene &scene, EntityId playerId, const PlayState &state)
{
    world.beginSolidSync();
    std::unordered_set<EntityId> synced;
    auto syncOne = [&](Object &object) {
        if (!synced.insert(object.id()).second)
            return;
        const bool kinematic = object.motion().active() || isTweenTarget(state, object.id());
        double qx = 0;
        double qy = 0;
        double qz = 0;
        double qw = 1;
        worldQuat(object, qx, qy, qz, qw);
        if (object.isSphere())
            world.upsertSphere(object.id(), object.worldPosition(), object.worldRadius(), kinematic);
        else if (object.isPlane())
            world.upsertPlane(object.id(), object.point(), object.worldNormal());
        else if (object.isTerrain())
        {
            if (const auto &tile = object.terrainTile())
            {
                world.upsertHeightField(object.id(), tile.get(), object.worldPosition(), qx, qy, qz, qw,
                    object.worldScale());
            }
        }
        else if (object.isMesh())
        {
            if (const auto &geometry = object.geometry())
            {
                world.upsertMesh(object.id(), geometry.get(), object.worldPosition(), qx, qy, qz, qw,
                    object.worldScaleVec(), kinematic);
            }
        }
    };

    const bool rebuild = world.solidCount() == 0;
    if (rebuild)
    {
        for (const auto &object : scene.objects())
        {
            if (isSolid(*object, playerId))
                syncOne(*object);
        }
    }
    else
    {
        for (EntityId id : scene.physicsDirty())
        {
            Object *object = scene.find(id);
            if (object != nullptr && isSolid(*object, playerId))
                syncOne(*object);
        }
        for (const auto &object : scene.objects())
        {
            if (!isSolid(*object, playerId))
                continue;
            const bool force = object->motion().active() || isTweenTarget(state, object->id());
            if (force)
                syncOne(*object);
            else if (!synced.contains(object->id()))
                world.touchSolid(object->id());
        }
    }
    world.endSolidSync();
    scene.clearPhysicsDirty();
}

double pingpongTicks(int time, int period)
{
    if (period < 1)
        period = 1;
    const int span = period * 2;
    int wrapped = time % span;
    if (wrapped < 0)
        wrapped += span;
    if (wrapped <= period)
        return static_cast<double>(wrapped) / static_cast<double>(period);
    return static_cast<double>(span - wrapped) / static_cast<double>(period);
}

Object *findPlayerBody(Scene &scene, EntityId &playerId)
{
    playerId = kInvalidEntityId;
    for (const auto &object : scene.objects())
    {
        object->ensureRole();
        if (!object->role() || object->role()->kind() != RoleKind::Player || object->bodyRadius() <= 0)
            continue;
        playerId = object->id();
        return object;
    }
    return nullptr;
}

void advanceMotions(Scene &scene, PlayState &state)
{
    ++state.motionTime;

    std::vector<Object *> spawners;
    for (const auto &object : scene.objects())
    {
        object->ensureRole();
        if (object->motion().active() && !(object->role() && object->role()->kind() == RoleKind::Player))
        {
            size_t poseIndex = state.rests.size();
            for (size_t i = 0; i < state.rests.size(); ++i)
            {
                if (state.rests[i].id == object->id())
                {
                    poseIndex = i;
                    break;
                }
            }
            if (poseIndex == state.rests.size())
            {
                RestPose captured;
                captured.id = object->id();
                captured.position = object->localPosition();
                captured.rotation = object->localRotationQuat();
                captured.scale = object->localScaleVec();
                state.rests.push_back(captured);
            }
            RestPose &pose = state.rests[poseIndex];
            const double wave = pingpongTicks(state.motionTime, playTicksFromSeconds(object->motion().period < 0.05 ? 0.05 : object->motion().period));
            object->setLocalPosition(pose.position + object->motion().move * wave);
            object->setLocalRotationQuat(pose.rotation * quatFromEulerDegrees(object->motion().rotate * wave));
            object->setLocalScaleVec(pose.scale + glm::dvec3(object->motion().scale * wave));
        }
        object->ensureRole();
        if (object->role() && object->role()->kind() == RoleKind::Spawner)
            spawners.push_back(object);
    }

    for (Object *object : spawners)
    {
        const int every = playTicksFromSeconds(object->spawnEvery() > 0 ? object->spawnEvery() : 3);
        ++object->spawnClock();
        if (object->spawnClock() < every)
            continue;
        object->spawnClock() -= every;
        if (scene.find(object->spawnedId()) != nullptr)
            continue;
        Object *gem = scene.addSphere(
            object->worldPosition() + Vec3(0, 0.22, 0),
            0.22,
            Material::makeDiffuse(Vec3(0.95, 0.85, 0.2)));
        gem->name() = "Spawned";
        gem->setTag("pickup");
        object->spawnedId() = gem->id();
    }
}

Object *findNamed(Scene &scene, const std::string &name)
{
    if (name.empty())
        return nullptr;
    for (const auto &object : scene.objects())
    {
        if (object->name() == name)
            return object;
    }
    return nullptr;
}

void fireAction(Scene &scene, Object &source, PlayState &state)
{
    if (source.action().fired || !source.action().armed())
        return;
    if (static_cast<int>(state.tweens.size()) >= maxTweens())
        return;
    Object *target = findNamed(scene, source.action().target);
    if (target == nullptr)
        return;
    Tween tween;
    tween.targetId = target->id();
    tween.origin = target->localPosition();
    tween.originRotation = target->localRotationQuat();
    tween.move = source.action().move;
    tween.rotate = source.action().rotate;
    state.tweens.push_back(tween);
    source.action().fired = true;
}

void advanceActions(Scene &scene, PlayState &state)
{
    const int durationTicks = actionDurationTicks();
    for (size_t index = 0; index < state.tweens.size();)
    {
        Tween &tween = state.tweens[index];
        Object *target = scene.find(tween.targetId);
        if (target == nullptr)
        {
            state.tweens.erase(state.tweens.begin() + static_cast<std::ptrdiff_t>(index));
            continue;
        }
        ++tween.time;
        double wave = static_cast<double>(tween.time) / static_cast<double>(durationTicks);
        if (wave > 1)
            wave = 1;
        const Vec3 next = tween.origin + tween.move * wave;
        target->setLocalPosition(next);
        target->setLocalRotationQuat(tween.originRotation * quatFromEulerDegrees(tween.rotate * wave));
        if (tween.time >= durationTicks)
            state.tweens.erase(state.tweens.begin() + static_cast<std::ptrdiff_t>(index));
        else
            ++index;
    }
}

void endRound(PlayState &state, const char *result, const char *text)
{
    state.result = result;
    state.message = text;
    state.verticalVelocity = 0;
}

void enterEastRoom(Scene &scene, PlayState &state)
{
    scene = createEastScene();
    state.room = 2;
    state.message = "Next room";
    state.verticalVelocity = 0;
    state.rests.clear();
    state.tweens.clear();
    state.physics.reset();
    EntityId playerId = kInvalidEntityId;
    findPlayerBody(scene, playerId);
    state.playerId = playerId;
}

void emitEvent(std::vector<SimEvent> *events, SimTick tick, SimEventKind kind, EntityId id, const Vec3 &position)
{
    if (events == nullptr)
        return;
    SimEvent event;
    event.kind = kind;
    event.id = id;
    event.position = position;
    event.tick = tick;
    events->push_back(event);
}

void applyAimedUse(Scene &scene, Object &object, const PlayInput &input, PlayState &state, bool &prompt, bool &won, bool &advance, std::string &overlapMessage)
{
    object.ensureRole();
    if (!object.role())
        return;
    if (object.role()->kind() == RoleKind::Goal)
    {
        if (input.use)
        {
            if (state.room == 1)
                advance = true;
            else
                won = true;
        }
        else
            prompt = true;
        return;
    }
    if (object.role()->kind() != RoleKind::Use)
        return;
    if (input.use)
    {
        overlapMessage = object.name().empty() ? "Used" : object.name();
        fireAction(scene, object, state);
    }
    else
        prompt = true;
}

void collectPlayEvents(Scene &scene, EntityId playerId, const Vec3 &center, double radius, const Vec3 &lookDirection, const PlayInput &input, PlayState &state,
    std::vector<SimEvent> *events, SimTick tick)
{
    std::vector<EntityId> removeIds;
    std::string overlapMessage;
    Vec3 messageAt = center;
    EntityId messageId = playerId;
    const std::string messageBefore = state.message;
    bool prompt = false;
    bool lost = false;
    bool won = false;
    bool advance = false;
    bool picked = false;
    for (const auto &object : scene.objects())
    {
        if (object->id() == playerId)
            continue;
        object->ensureRole();
        if (!object->role() || !object->role()->watchesOverlap())
            continue;
        if (!overlapsShape(*object, center, radius))
            continue;
        switch (object->role()->kind())
        {
        case RoleKind::Pickup:
            state.score += 1;
            if (state.health < 3)
                state.health += 1;
            overlapMessage = "Picked up";
            messageAt = object->worldPosition();
            messageId = object->id();
            picked = true;
            emitEvent(events, tick, SimEventKind::Pickup, object->id(), messageAt);
            removeIds.push_back(object->id());
            break;
        case RoleKind::Hazard:
            messageAt = object->worldPosition();
            messageId = object->id();
            lost = true;
            break;
        case RoleKind::Trigger:
            messageAt = object->worldPosition();
            messageId = object->id();
            overlapMessage = object->name().empty() ? "Triggered" : "Reached " + object->name();
            break;
        case RoleKind::Use:
        case RoleKind::Goal:
        {
            const std::string before = overlapMessage;
            const bool promptBefore = prompt;
            const bool wonBefore = won;
            const bool advanceBefore = advance;
            applyAimedUse(scene, *object, input, state, prompt, won, advance, overlapMessage);
            if (prompt != promptBefore || won != wonBefore || advance != advanceBefore || overlapMessage != before)
            {
                messageAt = object->worldPosition();
                messageId = object->id();
            }
            break;
        }
        default:
            break;
        }
    }
    PlayRayHit look;
    const Vec3 lookDir = length(lookDirection) > 1e-8 ? normalize(lookDirection) : Vec3(0, 0, -1);
    castPlayRay(scene, center, lookDir, playRayDistance(), playerId, look);
    state.lookId = look.hit ? look.id : kInvalidEntityId;
    state.lookName = look.hit ? look.name : std::string();
    state.lookTag = look.hit ? look.tag : std::string();
    state.lookPoint = look.hit ? look.point : Vec3();
    if (look.hit)
    {
        Object *aimed = scene.find(look.id);
        if (aimed != nullptr && !overlapsShape(*aimed, center, radius))
        {
            const std::string before = overlapMessage;
            const bool promptBefore = prompt;
            const bool wonBefore = won;
            const bool advanceBefore = advance;
            applyAimedUse(scene, *aimed, input, state, prompt, won, advance, overlapMessage);
            if (prompt != promptBefore || won != wonBefore || advance != advanceBefore || overlapMessage != before)
            {
                messageAt = aimed->worldPosition();
                messageId = aimed->id();
            }
        }
    }
    for (EntityId id : removeIds)
        scene.remove(id);
    if (lost || center.y < fallY())
    {
        state.eventAt = center;
        Object *spawn = nullptr;
        for (const auto &object : scene.objects())
        {
            object->ensureRole();
            if (object->role() && object->role()->kind() == RoleKind::SpawnPoint)
            {
                spawn = object;
                break;
            }
        }
        if (lost && spawn != nullptr && state.health > 0)
        {
            emitEvent(events, tick, SimEventKind::Effect, playerId, center);
            state.health -= 1;
        }
        const bool dead = lost && state.health <= 0;
        if (spawn != nullptr && !dead)
        {
            if (Object *body = scene.find(playerId))
                body->setWorldPosition(spawn->worldPosition());
            state.verticalVelocity = 0;
            state.eventAt = spawn->worldPosition();
            state.message = "Respawned";
            emitEvent(events, tick, SimEventKind::Sound, spawn->id(), state.eventAt);
            return;
        }
        if (lost)
        {
            if (spawn == nullptr)
            {
                emitEvent(events, tick, SimEventKind::Effect, playerId, center);
            }
            state.health = 0;
        }
        endRound(state, "lost", "You lose");
        emitEvent(events, tick, SimEventKind::Sound, playerId, state.eventAt);
        return;
    }
    if (advance)
    {
        enterEastRoom(scene, state);
        emitEvent(events, tick, SimEventKind::Sound, state.playerId, state.eventAt);
        return;
    }
    if (won)
    {
        endRound(state, "won", "You win");
        emitEvent(events, tick, SimEventKind::Win, messageId, messageAt);
        return;
    }
    if (prompt)
    {
        state.message = "Press F";
        state.eventAt = messageAt;
    }
    else if (!overlapMessage.empty())
    {
        state.message = overlapMessage;
        state.eventAt = messageAt;
    }
    if (state.message != messageBefore && !state.message.empty() && !picked)
        emitEvent(events, tick, SimEventKind::Sound, messageId, state.eventAt);
}

}

void syncPlayPlayerId(Scene &scene, PlayState &state)
{
    for (const auto &object : scene.objects())
    {
        object->ensureRole();
        if (!object->role() || object->role()->kind() != RoleKind::Player || object->bodyRadius() <= 0)
            continue;
        state.playerId = object->id();
        return;
    }
    state.playerId = kInvalidEntityId;
}

bool castPlayRay(const Scene &scene, const Vec3 &origin, const Vec3 &direction, double maxDistance, EntityId skipId, PlayRayHit &hit)
{
    hit = PlayRayHit();
    const double dirLen = length(direction);
    if (dirLen < 1e-8 || maxDistance <= 0)
        return false;
    const Ray ray(origin, direction / dirLen);
    double nearest = maxDistance;
    HitRecord record;
    bool found = false;
    for (const auto &object : scene.objects())
    {
        if (object->id() == skipId || passThroughLook(*object))
            continue;
        HitRecord candidate;
        if (!object->intersect(ray, 0.05, nearest, candidate))
            continue;
        nearest = candidate.t;
        record = candidate;
        hit.id = object->id();
        hit.name = object->name();
        hit.tag = object->tag();
        found = true;
    }
    if (!found)
        return false;
    hit.hit = true;
    hit.point = record.point;
    hit.normal = record.normal;
    hit.distance = record.t;
    return true;
}

Vec3 chaseCameraPosition(Scene &scene, EntityId playerId, const Vec3 &desired)
{
    Object *player = scene.find(playerId);
    if (player == nullptr || player->bodyRadius() <= 0)
        return desired;
    const Vec3 from = player->worldPosition();
    const Vec3 delta = desired - from;
    const double dist = length(delta);
    if (dist < 1e-4)
        return desired;

    const Vec3 direction = delta / dist;
    const Ray ray(from, direction);
    double hitDistance = dist;
    HitRecord hit;
    for (const auto &object : scene.objects())
    {
        if (!play_detail::isSolid(*object, playerId, true))
            continue;
        if (object->intersect(ray, 0.05, hitDistance, hit))
            hitDistance = hit.t;
    }
    if (hitDistance >= dist)
        return desired;

    constexpr double kMargin = 0.3;
    constexpr double kMinDistance = 0.5;
    double place = hitDistance - kMargin;
    if (place < kMinDistance)
        place = kMinDistance;
    if (place > dist)
        place = dist;
    return from + direction * place;
}

void stepPlay(Scene &scene, const PlayInput &input, const Vec3 &cameraForwardXZ, float dt, PlayState &state)
{
    std::vector<SimEvent> ignored;
    stepPlay(scene, input, cameraForwardXZ, dt, state, ignored, 0);
}

void stepPlay(Scene &scene, const PlayInput &input, const Vec3 &cameraForwardXZ, float dt, PlayState &state,
    std::vector<SimEvent> &events, SimTick tick)
{
    ZoneScopedN("stepPlay");
    events.clear();

    if (!state.result.empty())
        return;

    syncPrefabInstances(scene);
    advanceMotions(scene, state);

    EntityId playerId = kInvalidEntityId;
    Object *player = findPlayerBody(scene, playerId);
    state.playerId = playerId;
    if (!player)
        return;

    const double radius = player->bodyRadius();
    Vec3 center = player->worldPosition();
    if (state.stream)
    {
        state.stream->setFocus(center.x, center.z);
        state.stream->ensureReady(scene);
        center = player->worldPosition();
    }

    Vec3 forward = Vec3(cameraForwardXZ.x, 0, cameraForwardXZ.z);
    if (length(forward) < 1e-8)
        forward = Vec3(0, 0, -1);
    else
        forward = normalize(forward);
    const Vec3 right = normalize(cross(forward, Vec3(0, 1, 0)));
    Vec3 wish = right * static_cast<double>(input.moveX) + forward * static_cast<double>(input.moveZ);
    wish.y = 0;
    const double wishLen = length(wish);
    if (wishLen > 1e-8)
        wish = wish / wishLen;
    else
        wish = Vec3();

    if (!state.physics)
        state.physics = std::make_unique<jolt_play::World>();
    if (!state.physics->valid())
    {
        spdlog::error("physics world is invalid; call jobs::init() before play");
        return;
    }
    {
        syncJoltSolids(*state.physics, scene, player->id(), state);
        const Vec3 feet = state.restoreCapsule ? state.restoreFeet : (center - Vec3(0, radius, 0));
        const double spawnRadius = state.restoreCapsule ? state.restoreRadius : radius;
        const bool radiusChanged = std::abs(state.physics->capsuleRadius() - spawnRadius) > 1e-6;
        const bool lostCapsule = !state.physics->hasCapsule();
        const bool teleported =
            !state.restoreCapsule && state.physics->hasCapsule()
            && length(state.physics->capsuleFeet() - feet) > std::max(0.5, spawnRadius);
        if (lostCapsule || radiusChanged)
        {
            if (state.restoreCapsule && !state.restorePhysicsState.empty())
            {
                state.physics->restorePhysicsState(state.restorePhysicsState);
                state.restorePhysicsState.clear();
            }
            state.physics->spawnCapsule(feet, spawnRadius, jolt_play::kCapsuleCylinderHalfHeight);
            if (state.restoreCapsule)
            {
                if (!state.restoreCharacterState.empty()
                    && state.physics->restoreCharacterState(state.restoreCharacterState))
                    state.restoreCharacterState.clear();
                else
                {
                    state.physics->setCapsuleVelocity(state.restoreVelocity);
                    state.physics->refreshCapsuleContacts();
                    state.restoreCharacterState.clear();
                }
                state.restoreCapsule = false;
            }
        }
        else if (teleported)
            state.physics->setCapsuleFeet(feet);

        state.physics->setWishVelocityXZ(wish.x * moveSpeed(), wish.z * moveSpeed());
        if (input.jump)
            state.physics->requestJump();
        state.physics->step(dt > 0.f ? dt : 0.f);
        center = state.physics->capsuleFeet() + Vec3(0, radius, 0);
        state.verticalVelocity = static_cast<float>(state.physics->verticalVelocity());
    }

    player->setWorldPosition(center);
    collectPlayEvents(scene, player->id(), center, radius, cameraForwardXZ, input, state, &events, tick);
    if (state.result.empty())
        advanceActions(scene, state);
}

void primePlayPhysics(Scene &scene, PlayState &state)
{
    if (!state.result.empty())
        return;
    EntityId playerId = kInvalidEntityId;
    Object *player = findPlayerBody(scene, playerId);
    state.playerId = playerId;
    if (!player)
        return;
    if (!state.physics)
        state.physics = std::make_unique<jolt_play::World>();
    if (!state.physics->valid())
    {
        spdlog::error("physics world is invalid; call jobs::init() before play");
        return;
    }
    syncJoltSolids(*state.physics, scene, player->id(), state);
    if (!state.restoreCapsule)
        return;
    if (!state.restorePhysicsState.empty())
        state.physics->restorePhysicsState(state.restorePhysicsState);
    state.restorePhysicsState.clear();
    const double radius = state.restoreRadius > 1e-6 ? state.restoreRadius : player->bodyRadius();
    state.physics->spawnCapsule(state.restoreFeet, radius, jolt_play::kCapsuleCylinderHalfHeight);
    if (!state.restoreCharacterState.empty() && state.physics->restoreCharacterState(state.restoreCharacterState))
        state.restoreCharacterState.clear();
    else
    {
        state.physics->setCapsuleVelocity(state.restoreVelocity);
        state.physics->refreshCapsuleContacts();
        state.restoreCharacterState.clear();
    }
    state.restoreCapsule = false;
    state.verticalVelocity = static_cast<float>(state.physics->verticalVelocity());
}

