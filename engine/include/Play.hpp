#pragma once

#include "EngineSettings.hpp"
#include "JoltPlay.hpp"
#include "Scene.hpp"
#include "SimChannel.hpp"
#include "Stream.hpp"
#include "Vec3.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <random>
#include <string>
#include <vector>

struct RestPose
{
    EntityId id = kInvalidEntityId;
    Vec3 position;
    glm::dquat rotation{1, 0, 0, 0};
    glm::dvec3 scale{1, 1, 1};
};

struct PlayInput
{
    float moveX = 0; // strafe, camera-right, -1..1
    float moveZ = 0; // forward, camera-forward on XZ, -1..1
    bool jump = false;
    bool use = false;
};

// One playing Use. origin and originRotation are captured when it starts.
struct Tween
{
    EntityId targetId = kInvalidEntityId;
    Vec3 origin;
    glm::dquat originRotation{1, 0, 0, 0};
    Vec3 move;
    Vec3 rotate;
    int time = 0; // ticks at kPlayStep
};

struct PlayRayHit
{
    bool hit = false;
    EntityId id = kInvalidEntityId;
    std::string name;
    std::string tag;
    Vec3 point;
    Vec3 normal;
    double distance = 0;
};

struct PlayState
{
    bool paused = false;
    int score = 0;
    int health = 3;
    std::string message;
    EntityId playerId = kInvalidEntityId;
    float verticalVelocity = 0;
    std::string result;
    int room = 0;
    int motionTime = 0; // ticks at kPlayStep
    std::vector<RestPose> rests;
    std::vector<Tween> tweens;
    EntityId lookId = kInvalidEntityId;
    std::string lookName;
    std::string lookTag;
    Vec3 lookPoint;
    Vec3 eventAt;
    // Simulation RNG. Play does not call rand(). No gameplay site draws yet;
    // same seed after PlayState{} is the same stream for W2/W6.
    std::uint32_t simSeed = 1;
    std::mt19937 simRng{1};
    std::unique_ptr<jolt_play::World> physics;
    // Sim-layer stream only. stepPlay calls ensureReady (blocks until the focus ring is settled).
    // Render-layer streaming is owned by the view and pumped outside the tick.
    std::unique_ptr<TileStream> stream;
    bool restoreCapsule = false;
    Vec3 restoreVelocity;
    Vec3 restoreFeet;
    double restoreRadius = 0.35;
    std::vector<std::uint8_t> restoreCharacterState;
    std::vector<std::uint8_t> restorePhysicsState;
};

// Closest object along the ray, skipping skipId. maxDistance is in world units.
bool castPlayRay(const Scene &scene, const Vec3 &origin, const Vec3 &direction, double maxDistance, EntityId skipId, PlayRayHit &hit);

Vec3 chaseCameraPosition(Scene &scene, EntityId playerId, const Vec3 &desired);

void stepPlay(Scene &scene, const PlayInput &input, const Vec3 &cameraForwardXZ, float dt, PlayState &state);
void stepPlay(Scene &scene, const PlayInput &input, const Vec3 &cameraForwardXZ, float dt, PlayState &state,
    std::vector<SimEvent> &events, SimTick tick);
void primePlayPhysics(Scene &scene, PlayState &state);
void syncPlayPlayerId(Scene &scene, PlayState &state);

inline PlayInput playInputFromCommand(const Command &command)
{
    PlayInput input;
    input.moveX = command.moveX;
    input.moveZ = command.moveZ;
    input.jump = command.jump;
    input.use = command.use;
    return input;
}

inline Command commandFromPlayInput(const PlayInput &input, SimTick tick, const Vec3 &look)
{
    Command command;
    command.tick = tick;
    command.moveX = input.moveX;
    command.moveZ = input.moveZ;
    command.jump = input.jump;
    command.use = input.use;
    command.look = look;
    return command;
}

inline constexpr double kPlayStep = 1.0 / 30.0;

inline void seedPlayRng(PlayState &state)
{
    state.simRng.seed(state.simSeed);
}

// Scene and settings still store durations in seconds. Play counts ticks.
inline int playTicksFromSeconds(double seconds)
{
    if (!(seconds > 0))
        return 0;
    const long long ticks = std::llround(seconds / kPlayStep);
    if (ticks < 1)
        return 1;
    if (ticks > std::numeric_limits<int>::max())
        return std::numeric_limits<int>::max();
    return static_cast<int>(ticks);
}

inline double actionDuration() { return engineSettings().actionDuration; }
inline int actionDurationTicks()
{
    const int ticks = playTicksFromSeconds(actionDuration());
    return ticks < 1 ? 1 : ticks;
}
inline double playRayDistance() { return engineSettings().playRayDistance; }
inline int maxTweens() { return engineSettings().maxTweens; }
inline int maxPlayStepsPerFrame() { return engineSettings().maxPlayStepsPerFrame; }
inline double maxPlayFrameDt() { return engineSettings().maxPlayFrameDt; }

inline double clampFrameDt(double frameDt)
{
    const double cap = maxPlayFrameDt();
    if (frameDt < 0 || frameDt > cap)
        return cap;
    return frameDt;
}

// How many 1/30 s steps to run this window frame.
// A single step does not touch the accumulator. A hitch past the step cap drops the leftover.
inline int takePlaySteps(double frameDt, double timeScale, double &accumulator, bool singleStep)
{
    if (singleStep)
        return 1;
    if (timeScale < 0)
        timeScale = 0;
    accumulator += clampFrameDt(frameDt) * timeScale;
    int steps = 0;
    const int stepCap = maxPlayStepsPerFrame();
    while (accumulator >= kPlayStep && steps < stepCap)
    {
        accumulator -= kPlayStep;
        ++steps;
    }
    if (accumulator >= kPlayStep)
        accumulator = 0;
    return steps;
}

int runPlaySelfTests();
