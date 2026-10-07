#pragma once

#include "EngineSettings.hpp"
#include "Scene.hpp"
#include "Vec3.hpp"

#include <string>
#include <vector>

struct RestPose
{
    int id = -1;
    Vec3 position;
    Vec3 rotation;
    double scale = 1;
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
    int targetId = -1;
    Vec3 origin;
    Vec3 originRotation;
    Vec3 move;
    Vec3 rotate;
    double time = 0;
};

struct PlayRayHit
{
    bool hit = false;
    int id = -1;
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
    int playerId = -1;
    float verticalVelocity = 0;
    std::string result;
    int room = 0;
    double motionTime = 0;
    std::vector<RestPose> rests;
    std::vector<Tween> tweens;
    int lookId = -1;
    std::string lookName;
    std::string lookTag;
    Vec3 lookPoint;
    Vec3 eventAt;
};

// Closest object along the ray, skipping skipId. maxDistance is in world units.
bool castPlayRay(const Scene &scene, const Vec3 &origin, const Vec3 &direction, double maxDistance, int skipId, PlayRayHit &hit);

Vec3 chaseCameraPosition(Scene &scene, int playerId, const Vec3 &desired);

void stepPlay(Scene &scene, const PlayInput &input, const Vec3 &cameraForwardXZ, float dt, PlayState &state);

inline constexpr double kPlayStep = 1.0 / 60.0;

inline double actionDuration() { return engineSettings().actionDuration; }
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

// How many 1/60 s steps to run this window frame.
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
