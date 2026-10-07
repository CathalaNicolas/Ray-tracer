#pragma once

#include "EntityId.hpp"
#include "Vec3.hpp"

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

class Scene;
struct PlayState;
struct PlayInput;
class CommandRecorder;

// In-process sim ↔ view channel (plan W2). Tick duration is Play.hpp kPlayStep (1/30 s).
// bitsery encode/decode is SimSerialize.hpp; do not #include <bitsery/...> here.

using SimTick = std::uint32_t;

inline constexpr std::uint16_t kCommandBitseryVersion = 1;
inline constexpr std::uint16_t kSnapshotBitseryVersion = 2;
inline constexpr std::uint16_t kSimEventBitseryVersion = 1;
inline constexpr std::uint16_t kSimPlayBitseryVersion = 4;

// Tick-stamped player intent. Same buttons as PlayInput, plus the look vector stepPlay
// already takes as cameraForwardXZ (the editor passes lookAt - lookFrom; movement uses
// the XZ of that vector, the play ray uses the full direction). No device ids, no dt.
struct Command
{
    SimTick tick = 0;
    float moveX = 0; // strafe, camera-right, -1..1
    float moveZ = 0; // forward, camera-forward on XZ, -1..1
    bool jump = false;
    bool use = false;
    Vec3 look{0, 0, -1};
};

// One entity's interpolable transform. Simulation stores quaternions; Euler is file/editor only.
struct SnapshotPose
{
    EntityId id = kInvalidEntityId;
    Vec3 position;
    glm::dquat rotation{1, 0, 0, 0};
    glm::dvec3 scale{1, 1, 1};
};

// Post-tick view payload: poses to lerp, plus HUD fields PlayState already exposes.
// No extra combat stats. verticalVelocity, rests, and tweens stay simulation-only.
struct Snapshot
{
    SimTick tick = 0;
    EntityId playerId = kInvalidEntityId;
    std::vector<SnapshotPose> poses;
    int score = 0;
    int health = 3;
    bool paused = false;
    std::string message;
    std::string result;
    EntityId lookId = kInvalidEntityId;
    std::string lookName;
    std::string lookTag;
    Vec3 lookPoint;
};

// What collectPlayEvents actually cause (no damage/death kinds beyond win / lose-as-sound
// / hazard effect). View maps kind → clip or particle burst.
enum class SimEventKind : std::uint8_t
{
    Pickup = 1, // overlap pickup: GameSound::Pickup; gold burst (8 discs)
    Win = 2,    // result "won" / "You win": GameSound::Win
    Sound = 3,  // other one-shots: GameSound::Beep (Respawned, Press F, Next room, You lose, trigger/use text)
    Effect = 4, // hazard / fall burst: red discs at position (pickup gold is Pickup, not this)
};

struct SimEvent
{
    SimEventKind kind = SimEventKind::Sound;
    EntityId id = kInvalidEntityId;
    Vec3 position;
    SimTick tick = 0;
};

Snapshot captureSnapshot(const Scene &scene, const PlayState &state, SimTick tick);
void applySnapshotPoses(Scene &scene, const Snapshot &snapshot);
Snapshot interpolateSnapshots(const Snapshot &from, const Snapshot &to, double alpha);

// Command queue in; one Snapshot and the events from the consumed ticks out.
// Simulation Transforms are never written by the view. applyDisplay writes DisplayTransform.
class SimSession
{
public:
    void attach(Scene &scene, PlayState &state);
    void begin();

    void enqueue(const Command &command);
    void setRecorder(CommandRecorder *recorder) { recorder_ = recorder; }
    void take(int steps);
    void step(const PlayInput &input, const Vec3 &look);

    SimTick tick() const { return tick_; }
    SimTick nextTick() const { return tick_ + 1; }
    Vec3 lastLook() const { return lastLook_; }
    void primeFromCurrent(SimTick tick, const Vec3 &lastLook = Vec3(0, 0, -1));

    const Snapshot &snapshot() const { return current_; }
    const Snapshot &previousSnapshot() const { return previous_; }
    const std::vector<SimEvent> &events() const { return events_; }

    void applyDisplay(double alpha);
    void clearDisplay();
    void refreshHud();

private:
    Command takeCommand(SimTick forTick);

    Scene *scene_ = nullptr;
    PlayState *state_ = nullptr;
    SimTick tick_ = 0;
    std::deque<Command> queue_;
    Snapshot previous_;
    Snapshot current_;
    std::vector<SimEvent> events_;
    Vec3 lastLook_{0, 0, -1};
    CommandRecorder *recorder_ = nullptr;
};

// Records every Command passed to SimSession::enqueue (the ticks play actually ran).
// Replay walks that list tick-by-tick; it does not reconstruct dropped hitch leftover.
inline constexpr std::size_t kMaxRecordedCommands = 1u << 20;

class CommandRecorder
{
public:
    void clear() { commands_.clear(); }
    void record(const Command &command);
    const std::vector<Command> &commands() const { return commands_; }

private:
    std::vector<Command> commands_;
    bool overflowed_ = false;
};
