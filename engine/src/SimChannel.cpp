#include "SimChannel.hpp"

#include "Object.hpp"
#include "Play.hpp"
#include "Scene.hpp"
#include "TransformMath.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <tracy/Tracy.hpp>
#include <unordered_map>

namespace
{

Vec3 lerpVec(const Vec3 &a, const Vec3 &b, double t)
{
    return a * (1.0 - t) + b * t;
}

glm::dvec3 lerpScale(const glm::dvec3 &a, const glm::dvec3 &b, double t)
{
    return a * (1.0 - t) + b * t;
}

void writeDisplay(Scene &scene, const Snapshot &snapshot)
{
    for (const SnapshotPose &pose : snapshot.poses)
    {
        Object *object = scene.find(pose.id);
        if (object == nullptr)
            continue;
        object->setDisplayPose(toGlm(pose.position), pose.rotation, pose.scale);
    }
}

} // namespace

void CommandRecorder::record(const Command &command)
{
    if (commands_.size() >= kMaxRecordedCommands)
    {
        if (!overflowed_)
        {
            spdlog::warn("command recorder full at {} ticks; further commands are not saved", kMaxRecordedCommands);
            overflowed_ = true;
        }
        return;
    }
    commands_.push_back(command);
}

Snapshot captureSnapshot(const Scene &scene, const PlayState &state, SimTick tick)
{
    Snapshot snapshot;
    snapshot.tick = tick;
    snapshot.playerId = state.playerId;
    snapshot.score = state.score;
    snapshot.health = state.health;
    snapshot.paused = !state.result.empty();
    snapshot.message = state.message;
    snapshot.result = state.result;
    snapshot.lookId = state.lookId;
    snapshot.lookName = state.lookName;
    snapshot.lookTag = state.lookTag;
    snapshot.lookPoint = state.lookPoint;
    snapshot.poses.reserve(scene.objects().size());
    for (const auto &object : scene.objects())
    {
        SnapshotPose pose;
        pose.id = object->id();
        pose.position = object->localPosition();
        pose.rotation = object->localRotationQuat();
        pose.scale = object->localScaleVec();
        snapshot.poses.push_back(pose);
    }
    return snapshot;
}

void applySnapshotPoses(Scene &scene, const Snapshot &snapshot)
{
    writeDisplay(scene, snapshot);
}

Snapshot interpolateSnapshots(const Snapshot &from, const Snapshot &to, double alpha)
{
    Snapshot snapshot = to;
    if (alpha <= 0.0)
    {
        snapshot.poses = from.poses;
        snapshot.tick = from.tick;
        return snapshot;
    }
    if (alpha >= 1.0)
        return snapshot;

    snapshot.poses.clear();
    snapshot.poses.reserve(to.poses.size());
    std::unordered_map<EntityId, const SnapshotPose *> startById;
    startById.reserve(from.poses.size());
    for (const SnapshotPose &pose : from.poses)
        startById[pose.id] = &pose;
    for (const SnapshotPose &end : to.poses)
    {
        SnapshotPose pose = end;
        const auto found = startById.find(end.id);
        if (found != startById.end())
        {
            const SnapshotPose *start = found->second;
            pose.position = lerpVec(start->position, end.position, alpha);
            pose.rotation = slerpRotation(start->rotation, end.rotation, alpha);
            pose.scale = lerpScale(start->scale, end.scale, alpha);
        }
        snapshot.poses.push_back(pose);
    }
    return snapshot;
}

void SimSession::attach(Scene &scene, PlayState &state)
{
    scene_ = &scene;
    state_ = &state;
}

void SimSession::begin()
{
    if (recorder_ != nullptr)
        recorder_->clear();
    queue_.clear();
    events_.clear();
    tick_ = 0;
    lastLook_ = Vec3(0, 0, -1);
    if (scene_ == nullptr || state_ == nullptr)
    {
        previous_ = {};
        current_ = {};
        return;
    }
    clearDisplay();
    syncPlayPlayerId(*scene_, *state_);
    current_ = captureSnapshot(*scene_, *state_, 0);
    previous_ = current_;
}

Command SimSession::takeCommand(SimTick forTick)
{
    while (!queue_.empty() && queue_.front().tick < forTick)
        queue_.pop_front();
    if (!queue_.empty() && queue_.front().tick == forTick)
    {
        const Command command = queue_.front();
        queue_.pop_front();
        lastLook_ = command.look;
        return command;
    }
    Command command;
    command.tick = forTick;
    command.look = lastLook_;
    return command;
}

void SimSession::enqueue(const Command &command)
{
    queue_.push_back(command);
    if (recorder_ != nullptr)
        recorder_->record(command);
}

void SimSession::primeFromCurrent(SimTick tick, const Vec3 &lastLook)
{
    queue_.clear();
    events_.clear();
    tick_ = tick;
    lastLook_ = lastLook;
    if (scene_ == nullptr || state_ == nullptr)
    {
        previous_ = {};
        current_ = {};
        return;
    }
    current_ = captureSnapshot(*scene_, *state_, tick_);
    previous_ = current_;
}

void SimSession::take(int steps)
{
    ZoneScopedN("SimSession::take");
    events_.clear();
    if (scene_ == nullptr || state_ == nullptr || steps <= 0)
        return;
    for (int i = 0; i < steps; ++i)
    {
        const SimTick forTick = nextTick();
        const Command command = takeCommand(forTick);
        const PlayInput input = playInputFromCommand(command);
        Vec3 look = command.look;
        if (length(look) < 1e-8)
            look = lastLook_;
        std::vector<SimEvent> tickEvents;
        stepPlay(*scene_, input, look, static_cast<float>(kPlayStep), *state_, tickEvents, forTick);
        previous_ = current_;
        tick_ = forTick;
        current_ = captureSnapshot(*scene_, *state_, tick_);
        events_.insert(events_.end(), tickEvents.begin(), tickEvents.end());
    }
}

void SimSession::step(const PlayInput &input, const Vec3 &look)
{
    enqueue(commandFromPlayInput(input, nextTick(), look));
    take(1);
}

void SimSession::clearDisplay()
{
    if (scene_ == nullptr)
        return;
    for (const auto &object : scene_->objects())
        object->clearDisplayPose();
}

void SimSession::applyDisplay(double alpha)
{
    if (scene_ == nullptr)
        return;
    const double t = std::clamp(alpha, 0.0, 1.0);
    writeDisplay(*scene_, interpolateSnapshots(previous_, current_, t));
}

void SimSession::refreshHud()
{
    if (state_ == nullptr)
        return;
    current_.paused = !state_->result.empty();
    current_.message = state_->message;
    current_.result = state_->result;
    current_.score = state_->score;
    current_.health = state_->health;
    current_.playerId = state_->playerId;
    current_.lookId = state_->lookId;
    current_.lookName = state_->lookName;
    current_.lookTag = state_->lookTag;
    current_.lookPoint = state_->lookPoint;
    current_.tick = tick_;
}
