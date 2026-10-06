#include "Play.hpp"
#include "PlayDetail.hpp"

#include "Collision.hpp"
#include "EngineSettings.hpp"
#include "DemoScene.hpp"
#include "Sphere.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace
{

using namespace play_detail;

bool overlapsShape(const Hittable &object, const Vec3 &center, double radius)
{
    return object.overlapsSphere(center, radius);
}

// Tags the player walks through. They must not steal the look ray from a use or goal.
bool passThroughLook(const Hittable &object)
{
    object.ensureRole();
    return object.role && object.role->passThroughLook();
}

Vec3 unitNormal(const Vec3 &normal)
{
    const double len = length(normal);
    if (len <= 1e-8)
        return Vec3(0, 1, 0);
    return normal / len;
}

void slideAlong(Vec3 &velocity, const Vec3 &normal)
{
    const double into = dot(velocity, normal);
    if (into < 0)
        velocity = velocity - normal * into;
}

bool onGround(const Scene &scene, int playerId, const Vec3 &center, double radius)
{
    const Vec3 probe = center - Vec3(0, groundProbe(), 0);
    for (const auto &object : scene.objects())
    {
        if (!isSolid(*object, playerId))
            continue;
        const Hit hit = contact(*object, probe, radius);
        if (hit.hit && hit.normal.y > 0.5)
            return true;
    }
    return false;
}

void solveSolids(Scene &scene, int playerId, Vec3 &center, Vec3 &velocity, double radius, double &stepSurface)
{
    const double feet = center.y - radius;
    for (int pass = 0; pass < engineSettings().resolvePasses; ++pass)
    {
        bool any = false;
        for (const auto &object : scene.objects())
        {
            if (!isSolid(*object, playerId))
                continue;
            const Hit hit = contact(*object, center, radius);
            if (!hit.hit || hit.penetration <= 0.f)
                continue;
            const Vec3 normal = unitNormal(hit.normal);
            if (normal.y < 0.45 && hit.point.y <= feet + stepHeight() + 1e-3 && hit.point.y >= feet - 0.05)
                stepSurface = std::max(stepSurface, hit.point.y);
            resolveSphere(center, hit);
            slideAlong(velocity, normal);
            any = true;
        }
        if (!any)
            break;
    }
}

double pingpong(double time, double period)
{
    if (period < 0.05)
        period = 0.05;
    const double span = period * 2;
    double wrapped = std::fmod(time, span);
    if (wrapped < 0)
        wrapped += span;
    if (wrapped <= period)
        return wrapped / period;
    return (span - wrapped) / period;
}

Hittable *findPlayerBody(Scene &scene, int &playerId)
{
    playerId = -1;
    for (const auto &object : scene.objects())
    {
        object->ensureRole();
        if (!object->role || object->role->kind() != RoleKind::Player || object->bodyRadius() <= 0)
            continue;
        playerId = object->id;
        return object.get();
    }
    return nullptr;
}

void advanceMotions(Scene &scene, PlayState &state, double dt)
{
    state.motionTime += dt;
    int playerId = -1;
    Hittable *player = findPlayerBody(scene, playerId);

    std::vector<Hittable *> spawners;
    for (const auto &object : scene.objects())
    {
        object->ensureRole();
        if (object->motion.active() && !(object->role && object->role->kind() == RoleKind::Player))
        {
            size_t poseIndex = state.rests.size();
            for (size_t i = 0; i < state.rests.size(); ++i)
            {
                if (state.rests[i].id == object->id)
                {
                    poseIndex = i;
                    break;
                }
            }
            if (poseIndex == state.rests.size())
            {
                RestPose captured;
                captured.id = object->id;
                captured.position = object->localPosition();
                captured.rotation = object->localRotation();
                captured.scale = object->localScale();
                state.rests.push_back(captured);
            }
            RestPose &pose = state.rests[poseIndex];
            const double radius = player != nullptr ? player->bodyRadius() : 0;
            const Vec3 playerCenter = player != nullptr ? player->worldPosition() : Vec3();
            const bool carry = player != nullptr && isSolid(*object, playerId) && onGround(scene, playerId, playerCenter, radius) && contact(*object, playerCenter - Vec3(0, groundProbe(), 0), radius).hit;
            const Vec3 beforeWorld = object->worldPosition();
            const double wave = pingpong(state.motionTime, object->motion.period);
            object->setLocalPosition(pose.position + object->motion.move * wave);
            object->setLocalRotation(pose.rotation + object->motion.rotate * wave);
            object->setLocalScale(pose.scale + object->motion.scale * wave);
            if (carry)
                player->setWorldPosition(player->worldPosition() + (object->worldPosition() - beforeWorld));
        }
        object->ensureRole();
        if (object->role && object->role->kind() == RoleKind::Spawner)
            spawners.push_back(object.get());
    }

    for (Hittable *object : spawners)
    {
        const double every = object->spawnEvery > 0 ? object->spawnEvery : 3;
        object->spawnClock += dt;
        if (object->spawnClock < every)
            continue;
        object->spawnClock -= every;
        if (scene.find(object->spawnedId) != nullptr)
            continue;
        auto gem = std::make_unique<Sphere>(
            object->worldPosition() + Vec3(0, 0.22, 0),
            0.22,
            Material::makeDiffuse(Vec3(0.95, 0.85, 0.2)));
        gem->name = "Spawned";
        gem->setTag("pickup");
        object->spawnedId = scene.add(std::move(gem));
    }
}

Hittable *findNamed(Scene &scene, const std::string &name)
{
    if (name.empty())
        return nullptr;
    for (const auto &object : scene.objects())
    {
        if (object->name == name)
            return object.get();
    }
    return nullptr;
}

void fireAction(Scene &scene, Hittable &source, PlayState &state)
{
    if (source.action.fired || !source.action.armed())
        return;
    if (static_cast<int>(state.tweens.size()) >= maxTweens())
        return;
    Hittable *target = findNamed(scene, source.action.target);
    if (target == nullptr)
        return;
    Tween tween;
    tween.targetId = target->id;
    tween.origin = target->localPosition();
    tween.originRotation = target->localRotation();
    tween.move = source.action.move;
    tween.rotate = source.action.rotate;
    state.tweens.push_back(tween);
    source.action.fired = true;
}

void advanceActions(Scene &scene, PlayState &state, double dt)
{
    int playerId = -1;
    Hittable *player = findPlayerBody(scene, playerId);

    for (size_t index = 0; index < state.tweens.size();)
    {
        Tween &tween = state.tweens[index];
        Hittable *target = scene.find(tween.targetId);
        if (target == nullptr)
        {
            state.tweens.erase(state.tweens.begin() + static_cast<std::ptrdiff_t>(index));
            continue;
        }
        tween.time += dt;
        double wave = tween.time / actionDuration();
        if (wave > 1)
            wave = 1;
        const Vec3 beforeWorld = target->worldPosition();
        const Vec3 next = tween.origin + tween.move * wave;
        const double radius = player != nullptr ? player->bodyRadius() : 0;
        const Vec3 playerCenter = player != nullptr ? player->worldPosition() : Vec3();
        const bool carry = player != nullptr && isSolid(*target, playerId) && onGround(scene, playerId, playerCenter, radius) && contact(*target, playerCenter - Vec3(0, groundProbe(), 0), radius).hit;
        target->setLocalPosition(next);
        target->setLocalRotation(tween.originRotation + tween.rotate * wave);
        if (carry)
            player->setWorldPosition(player->worldPosition() + (target->worldPosition() - beforeWorld));
        if (tween.time >= actionDuration())
            state.tweens.erase(state.tweens.begin() + static_cast<std::ptrdiff_t>(index));
        else
            ++index;
    }
}

void endRound(PlayState &state, const char *result, const char *text)
{
    state.result = result;
    state.message = text;
    state.paused = true;
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
    int playerId = -1;
    findPlayerBody(scene, playerId);
    state.playerId = playerId;
}

void burstParticles(Scene &scene, const Vec3 &at, const Vec3 &color)
{
    for (int index = 0; index < 8; ++index)
    {
        const double angle = index * 0.78539816339;
        Particle particle;
        particle.position = at;
        particle.velocity = Vec3(std::cos(angle), 1.4, std::sin(angle)) * 1.6;
        particle.color = color;
        particle.life = 0.45;
        particle.size = 0.07;
        scene.addParticle(particle);
    }
}

void applyAimedUse(Scene &scene, Hittable &object, const PlayInput &input, PlayState &state, bool &prompt, bool &won, bool &advance, std::string &overlapMessage)
{
    object.ensureRole();
    if (!object.role)
        return;
    if (object.role->kind() == RoleKind::Goal)
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
    if (object.role->kind() != RoleKind::Use)
        return;
    if (input.use)
    {
        overlapMessage = object.name.empty() ? "Used" : object.name;
        fireAction(scene, object, state);
    }
    else
        prompt = true;
}

void collectPlayEvents(Scene &scene, int playerId, const Vec3 &center, double radius, const Vec3 &lookDirection, const PlayInput &input, PlayState &state)
{
    std::vector<int> removeIds;
    std::string overlapMessage;
    Vec3 messageAt = center;
    bool prompt = false;
    bool lost = false;
    bool won = false;
    bool advance = false;
    for (const auto &object : scene.objects())
    {
        if (object->id == playerId)
            continue;
        object->ensureRole();
        if (!object->role || !object->role->watchesOverlap())
            continue;
        if (!overlapsShape(*object, center, radius))
            continue;
        switch (object->role->kind())
        {
        case RoleKind::Pickup:
            state.score += 1;
            if (state.health < 3)
                state.health += 1;
            overlapMessage = "Picked up";
            messageAt = object->worldPosition();
            burstParticles(scene, messageAt, Vec3(0.95, 0.85, 0.25));
            removeIds.push_back(object->id);
            break;
        case RoleKind::Hazard:
            messageAt = object->worldPosition();
            lost = true;
            break;
        case RoleKind::Trigger:
            messageAt = object->worldPosition();
            overlapMessage = object->name.empty() ? "Triggered" : "Reached " + object->name;
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
                messageAt = object->worldPosition();
            break;
        }
        default:
            break;
        }
    }
    PlayRayHit look;
    const Vec3 lookDir = length(lookDirection) > 1e-8 ? normalize(lookDirection) : Vec3(0, 0, -1);
    castPlayRay(scene, center, lookDir, playRayDistance(), playerId, look);
    state.lookId = look.hit ? look.id : -1;
    state.lookName = look.hit ? look.name : std::string();
    state.lookTag = look.hit ? look.tag : std::string();
    state.lookPoint = look.hit ? look.point : Vec3();
    if (look.hit)
    {
        Hittable *aimed = scene.find(look.id);
        if (aimed != nullptr && !overlapsShape(*aimed, center, radius))
        {
            const std::string before = overlapMessage;
            const bool promptBefore = prompt;
            const bool wonBefore = won;
            const bool advanceBefore = advance;
            applyAimedUse(scene, *aimed, input, state, prompt, won, advance, overlapMessage);
            if (prompt != promptBefore || won != wonBefore || advance != advanceBefore || overlapMessage != before)
                messageAt = aimed->worldPosition();
        }
    }
    for (int id : removeIds)
        scene.remove(id);
    if (lost || center.y < fallY())
    {
        state.eventAt = center;
        Hittable *spawn = nullptr;
        for (const auto &object : scene.objects())
        {
            object->ensureRole();
            if (object->role && object->role->kind() == RoleKind::SpawnPoint)
            {
                spawn = object.get();
                break;
            }
        }
        if (lost && spawn != nullptr && state.health > 0)
        {
            burstParticles(scene, center, Vec3(0.9, 0.2, 0.15));
            state.health -= 1;
        }
        const bool dead = lost && state.health <= 0;
        if (spawn != nullptr && !dead)
        {
            if (Hittable *body = scene.find(playerId))
                body->setWorldPosition(spawn->worldPosition());
            state.verticalVelocity = 0;
            state.eventAt = spawn->worldPosition();
            state.message = "Respawned";
            return;
        }
        if (lost)
        {
            if (spawn == nullptr)
                burstParticles(scene, center, Vec3(0.9, 0.2, 0.15));
            state.health = 0;
        }
        endRound(state, "lost", "You lose");
        return;
    }
    if (advance)
    {
        enterEastRoom(scene, state);
        return;
    }
    if (won)
    {
        endRound(state, "won", "You win");
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
}

}

bool castPlayRay(const Scene &scene, const Vec3 &origin, const Vec3 &direction, double maxDistance, int skipId, PlayRayHit &hit)
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
        if (object->id == skipId || passThroughLook(*object))
            continue;
        HitRecord candidate;
        if (!object->intersect(ray, 0.05, nearest, candidate))
            continue;
        nearest = candidate.t;
        record = candidate;
        hit.id = object->id;
        hit.name = object->name;
        hit.tag = object->tag;
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

Vec3 chaseCameraPosition(Scene &scene, int playerId, const Vec3 &desired)
{
    Hittable *player = scene.find(playerId);
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
    if (!state.result.empty() || state.paused)
        return;

    syncPrefabInstances(scene);
    scene.advanceParticles(dt > 0.f ? static_cast<double>(dt) : 0);
    advanceMotions(scene, state, dt > 0.f ? static_cast<double>(dt) : 0);

    int playerId = -1;
    Hittable *player = findPlayerBody(scene, playerId);
    state.playerId = playerId;
    if (!player)
        return;

    const double radius = player->bodyRadius();
    const double stepDt = dt > 0.f ? static_cast<double>(dt) : 0;
    Vec3 center = player->worldPosition();

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

    double vy = state.verticalVelocity;
    if (onGround(scene, player->id, center, radius) && input.jump)
        vy = jumpSpeed();
    else
        vy += gravity() * stepDt;

    Vec3 velocity(wish.x * moveSpeed(), vy, wish.z * moveSpeed());
    int substeps = engineSettings().minSubsteps;
    if (radius > 1e-8)
    {
        const int needed = static_cast<int>(std::ceil(length(velocity) * stepDt / (radius * 0.5)));
        if (needed > substeps)
            substeps = needed;
    }
    if (substeps > engineSettings().maxSubsteps)
        substeps = engineSettings().maxSubsteps;

    const double subDt = stepDt / static_cast<double>(substeps);
    for (int sub = 0; sub < substeps; ++sub)
    {
        const Vec3 start = center;
        const Vec3 delta = velocity * subDt;
        const Vec3 velBefore = velocity;
        double stepSurface = -1e30;
        center += delta;
        solveSolids(scene, player->id, center, velocity, radius, stepSurface);
        const double wishH = length(Vec3(velBefore.x, 0, velBefore.z));
        const double gotH = length(Vec3(velocity.x, 0, velocity.z));
        if (stepSurface > -1e20 && wishH > 0.2 && gotH < wishH * 0.85 && velBefore.y < 1.5)
        {
            Vec3 raised = start;
            raised.y = stepSurface + radius + 0.02;
            Vec3 ignored(0, 0, 0);
            double unused = -1e30;
            solveSolids(scene, player->id, raised, ignored, radius, unused);
            if (raised.y > start.y + 0.02)
            {
                Vec3 stepped = raised + Vec3(delta.x, 0, delta.z);
                Vec3 stepVel(velBefore.x, 0, velBefore.z);
                double unusedStep = -1e30;
                solveSolids(scene, player->id, stepped, stepVel, radius, unusedStep);
                if (length(Vec3(stepVel.x, 0, stepVel.z)) > gotH + 0.05)
                {
                    const double climb = raised.y - start.y;
                    Vec3 dropped = stepped;
                    dropped.y -= climb;
                    Vec3 down(0, -1, 0);
                    double unusedDrop = -1e30;
                    solveSolids(scene, player->id, dropped, down, radius, unusedDrop);
                    if (dropped.y >= start.y - 0.01)
                    {
                        center = dropped;
                        velocity.x = stepVel.x;
                        velocity.z = stepVel.z;
                        velocity.y = std::min(velBefore.y, 0.0);
                    }
                }
            }
        }
    }

    state.verticalVelocity = static_cast<float>(velocity.y);
    player->setWorldPosition(center);
    collectPlayEvents(scene, player->id, center, radius, cameraForwardXZ, input, state);
    if (state.result.empty())
        advanceActions(scene, state, stepDt);
}

