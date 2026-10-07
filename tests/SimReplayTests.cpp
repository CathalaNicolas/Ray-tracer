#include "DemoScene.hpp"
#include "Play.hpp"
#include "SimReplay.hpp"
#include "SimSerialize.hpp"

#include <doctest/doctest.h>

#include <glm/gtc/quaternion.hpp>

namespace
{

void driveTicks(SimSession &session, int ticks, bool displayPath)
{
    const Vec3 look(0, 0, -1);
    for (int i = 0; i < ticks; ++i)
    {
        if (displayPath)
            session.clearDisplay();
        PlayInput input;
        input.moveZ = 1.f;
        if (i % 40 == 10)
            input.jump = true;
        if (i % 90 == 20)
            input.use = true;
        session.step(input, look);
        if (displayPath)
            session.applyDisplay(0.37);
    }
    if (displayPath)
        session.clearDisplay();
}

} // namespace

TEST_CASE("bitsery command snapshot event roundtrip")
{
    Command command;
    command.tick = 7;
    command.moveX = 0.25f;
    command.moveZ = -1.f;
    command.jump = true;
    command.use = false;
    command.look = Vec3(0.1, 0.2, -0.9);

    std::vector<std::uint8_t> bytes;
    REQUIRE(encodeCommand(command, bytes));
    Command decoded;
    REQUIRE(decodeCommand(bytes, decoded));
    CHECK(decoded.tick == command.tick);
    CHECK(decoded.moveX == command.moveX);
    CHECK(decoded.moveZ == command.moveZ);
    CHECK(decoded.jump == command.jump);
    CHECK(decoded.use == command.use);
    CHECK(decoded.look.x == command.look.x);
    CHECK(decoded.look.y == command.look.y);
    CHECK(decoded.look.z == command.look.z);

    Snapshot snapshot;
    snapshot.tick = 3;
    snapshot.score = 2;
    snapshot.message = "Picked up";
    snapshot.poses.push_back(SnapshotPose{1, Vec3(1, 2, 3), glm::dquat(1, 0, 0, 0), glm::dvec3(1.5, 1.5, 1.5)});
    REQUIRE(encodeSnapshot(snapshot, bytes));
    Snapshot snapOut;
    REQUIRE(decodeSnapshot(bytes, snapOut));
    CHECK(snapOut.tick == 3);
    CHECK(snapOut.score == 2);
    CHECK(snapOut.message == "Picked up");
    REQUIRE(snapOut.poses.size() == 1);
    CHECK(snapOut.poses[0].id == 1);
    CHECK(snapOut.poses[0].scale.x == 1.5);

    SimEvent event;
    event.kind = SimEventKind::Pickup;
    event.id = 9;
    event.tick = 4;
    event.position = Vec3(1, 0, 2);
    REQUIRE(encodeSimEvent(event, bytes));
    SimEvent eventOut;
    REQUIRE(decodeSimEvent(bytes, eventOut));
    CHECK(eventOut.kind == SimEventKind::Pickup);
    CHECK(eventOut.id == 9);
    CHECK(eventOut.tick == 4);
}

TEST_CASE("headless command replay matches 18000 ticks bit for bit")
{
    Scene start = createDemoScene();
    Scene liveScene = start.clone();
    PlayState liveState;
    liveState.simSeed = 1;
    seedPlayRng(liveState);
    CommandRecorder recorder;
    SimSession live;
    live.attach(liveScene, liveState);
    live.setRecorder(&recorder);
    live.begin();
    driveTicks(live, 18000, false);

    std::vector<std::uint8_t> commandBytes;
    REQUIRE(encodeCommandList(recorder.commands(), commandBytes));
    std::vector<Command> decodedCommands;
    REQUIRE(decodeCommandList(commandBytes, decodedCommands));
    REQUIRE(decodedCommands.size() == recorder.commands().size());

    PlayState replayState;
    replayState.simSeed = liveState.simSeed;
    seedPlayRng(replayState);
    Scene replayScene = start.clone();
    replayCommands(replayScene, replayState, decodedCommands);

    CHECK(sceneTransformsEqual(liveScene, replayScene));
    CHECK(playStateFieldsEqual(liveState, replayState));
}

TEST_CASE("editor display path does not change simulation state")
{
    Scene start = createDemoScene();
    Scene headlessScene = start.clone();
    Scene editorScene = start.clone();
    PlayState headlessState;
    PlayState editorState;
    headlessState.simSeed = 1;
    editorState.simSeed = 1;
    seedPlayRng(headlessState);
    seedPlayRng(editorState);
    SimSession headless;
    SimSession editor;
    headless.attach(headlessScene, headlessState);
    editor.attach(editorScene, editorState);
    headless.begin();
    editor.begin();
    driveTicks(headless, 3000, false);
    driveTicks(editor, 3000, true);
    CHECK(sceneTransformsEqual(headlessScene, editorScene));
    CHECK(playStateFieldsEqual(headlessState, editorState));
}

TEST_CASE("replay matches pickup score and removal")
{
    Scene start;
    Object *ground = start.addPlane(Vec3(0, 0, 0), Vec3(0, 1, 0), Material::makeDiffuse(Vec3(0.8, 0.8, 0.8)));
    ground->setTag("solid");
    Object *player = start.addSphere(Vec3(0, 0.5, 0), 0.35, Material::makeDiffuse(Vec3(0.2, 0.4, 0.9)));
    player->setTag("player");
    Object *pickup = start.addSphere(Vec3(0, 0.5, -1.2), 0.22, Material::makeDiffuse(Vec3(0.9, 0.8, 0.2)));
    pickup->setTag("pickup");
    const EntityId pickupId = pickup->id();

    Scene liveScene = start.clone();
    PlayState liveState;
    liveState.simSeed = 1;
    seedPlayRng(liveState);
    CommandRecorder recorder;
    SimSession live;
    live.attach(liveScene, liveState);
    live.setRecorder(&recorder);
    live.begin();
    const Vec3 look(0, 0, -1);
    for (int i = 0; i < 90; ++i)
    {
        PlayInput input;
        input.moveZ = 1.f;
        live.step(input, look);
    }
    CHECK(liveState.score == 1);
    CHECK(liveScene.find(pickupId) == nullptr);

    PlayState replayState;
    replayState.simSeed = 1;
    seedPlayRng(replayState);
    Scene replayScene = start.clone();
    replayCommands(replayScene, replayState, recorder.commands());
    CHECK(replayState.score == 1);
    CHECK(replayScene.find(pickupId) == nullptr);
    CHECK(sceneTransformsEqual(liveScene, replayScene));
    CHECK(playStateFieldsEqual(liveState, replayState));
}

TEST_CASE("replay matches goal win")
{
    Scene start;
    Object *ground = start.addPlane(Vec3(0, 0, 0), Vec3(0, 1, 0), Material::makeDiffuse(Vec3(0.8, 0.8, 0.8)));
    ground->setTag("solid");
    Object *player = start.addSphere(Vec3(0, 0.5, 0), 0.35, Material::makeDiffuse(Vec3(0.2, 0.4, 0.9)));
    player->setTag("player");
    Object *goal = start.addSphere(Vec3(0, 0.5, -0.6), 0.45, Material::makeDiffuse(Vec3(0.2, 0.8, 0.3)));
    goal->setTag("goal");

    Scene liveScene = start.clone();
    PlayState liveState;
    liveState.simSeed = 1;
    seedPlayRng(liveState);
    CommandRecorder recorder;
    SimSession live;
    live.attach(liveScene, liveState);
    live.setRecorder(&recorder);
    live.begin();
    const Vec3 look(0, 0, -1);
    for (int i = 0; i < 20; ++i)
    {
        PlayInput input;
        input.use = true;
        live.step(input, look);
    }
    CHECK(liveState.result == "won");

    PlayState replayState;
    replayState.simSeed = 1;
    seedPlayRng(replayState);
    Scene replayScene = start.clone();
    replayCommands(replayScene, replayState, recorder.commands());
    CHECK(replayState.result == "won");
    CHECK(playStateFieldsEqual(liveState, replayState));
}

TEST_CASE("replay matches hazard loss")
{
    Scene start;
    Object *ground = start.addPlane(Vec3(0, 0, 0), Vec3(0, 1, 0), Material::makeDiffuse(Vec3(0.8, 0.8, 0.8)));
    ground->setTag("solid");
    Object *player = start.addSphere(Vec3(0, 0.5, 0), 0.35, Material::makeDiffuse(Vec3(0.2, 0.4, 0.9)));
    player->setTag("player");
    Object *hazard = start.addSphere(Vec3(0, 0.5, -0.4), 0.4, Material::makeDiffuse(Vec3(0.6, 0.1, 0.1)));
    hazard->setTag("hazard");

    Scene liveScene = start.clone();
    PlayState liveState;
    liveState.simSeed = 1;
    seedPlayRng(liveState);
    CommandRecorder recorder;
    SimSession live;
    live.attach(liveScene, liveState);
    live.setRecorder(&recorder);
    live.begin();
    const Vec3 look(0, 0, -1);
    for (int i = 0; i < 10; ++i)
    {
        PlayInput input;
        live.step(input, look);
    }
    CHECK(liveState.result == "lost");

    PlayState replayState;
    replayState.simSeed = 1;
    seedPlayRng(replayState);
    Scene replayScene = start.clone();
    replayCommands(replayScene, replayState, recorder.commands());
    CHECK(replayState.result == "lost");
    CHECK(playStateFieldsEqual(liveState, replayState));
}
