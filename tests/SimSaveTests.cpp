#include "DemoScene.hpp"
#include "Play.hpp"
#include "SimSave.hpp"
#include "SimSerialize.hpp"

#include <doctest/doctest.h>

#include <filesystem>

namespace
{

struct TempSessionFile
{
    std::filesystem::path path;

    TempSessionFile()
    {
        path = std::filesystem::temp_directory_path() / "raytracer-play-w7-test.sqlite";
        std::error_code ec;
        std::filesystem::path wal = path;
        wal += "-wal";
        std::filesystem::path shm = path;
        shm += "-shm";
        std::filesystem::remove(path, ec);
        std::filesystem::remove(wal, ec);
        std::filesystem::remove(shm, ec);
    }

    ~TempSessionFile()
    {
        std::error_code ec;
        std::filesystem::path wal = path;
        wal += "-wal";
        std::filesystem::path shm = path;
        shm += "-shm";
        std::filesystem::remove(path, ec);
        std::filesystem::remove(wal, ec);
        std::filesystem::remove(shm, ec);
    }
};

void runTicks(SimSession &session, int ticks)
{
    const Vec3 look(0, 0, -1);
    for (int i = 0; i < ticks; ++i)
    {
        PlayInput input;
        input.moveZ = 1.f;
        if (i == 10)
            input.jump = true;
        session.step(input, look);
    }
}

} // namespace

TEST_CASE("default play-session path sits next to settings")
{
    CHECK(defaultPlaySessionPath() == std::filesystem::path(kDefaultPlaySessionFileName));
    CHECK(defaultPlaySessionPath().filename() == "raytracer-play.sqlite");
}

TEST_CASE("mid-play save resumes 300 ticks bit for bit")
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
    runTicks(live, 14);

    TempSessionFile file;
    REQUIRE(savePlaySession(file.path, liveScene, liveState, live.tick(), live.lastLook(), recorder.commands()));

    Scene restored = start.clone();
    PlayState restoredState;
    PlaySessionInfo info;
    REQUIRE(loadPlaySession(file.path, restored, restoredState, info));
    CHECK(info.tick == 14);
    CHECK(sceneTransformsEqual(liveScene, restored));
    CHECK(playStateFieldsEqual(liveState, restoredState));
    REQUIRE(liveState.physics);
    REQUIRE(restoredState.physics);
    CHECK(liveState.physics->capsuleFeet().x == restoredState.physics->capsuleFeet().x);
    CHECK(liveState.physics->capsuleFeet().y == restoredState.physics->capsuleFeet().y);
    CHECK(liveState.physics->capsuleFeet().z == restoredState.physics->capsuleFeet().z);
    CHECK(liveState.physics->capsuleVelocity().x == restoredState.physics->capsuleVelocity().x);
    CHECK(liveState.physics->capsuleVelocity().y == restoredState.physics->capsuleVelocity().y);
    CHECK(liveState.physics->capsuleVelocity().z == restoredState.physics->capsuleVelocity().z);
    CHECK(liveState.physics->grounded() == restoredState.physics->grounded());

    runTicks(live, 300);

    SimSession restoredSession;
    restoredSession.attach(restored, restoredState);
    restoredSession.primeFromCurrent(info.tick, info.lastLook);
    runTicks(restoredSession, 300);

    CHECK(sceneTransformsEqual(liveScene, restored));
    CHECK(playStateFieldsEqual(liveState, restoredState));
    if (liveState.physics && restoredState.physics && liveState.physics->hasCapsule()
        && restoredState.physics->hasCapsule())
    {
        CHECK(liveState.physics->capsuleVelocity().x == restoredState.physics->capsuleVelocity().x);
        CHECK(liveState.physics->capsuleVelocity().y == restoredState.physics->capsuleVelocity().y);
        CHECK(liveState.physics->capsuleVelocity().z == restoredState.physics->capsuleVelocity().z);
        CHECK(liveState.physics->capsuleFeet().x == restoredState.physics->capsuleFeet().x);
        CHECK(liveState.physics->capsuleFeet().y == restoredState.physics->capsuleFeet().y);
        CHECK(liveState.physics->capsuleFeet().z == restoredState.physics->capsuleFeet().z);
        CHECK(liveState.physics->grounded() == restoredState.physics->grounded());
    }
}

TEST_CASE("applySimPlay fails on a missing mesh file")
{
    Scene scene = createDemoScene();
    PlayState state;
    seedPlayRng(state);
    SimPlayBlob blob = captureSimPlay(scene, state, 0, Vec3(0, 0, -1));
    bool touchedMesh = false;
    for (SimEntityRecord &record : blob.entities)
    {
        if (record.shape == SimShapeKind::Mesh)
        {
            record.meshSourcePath = "definitely-missing-mesh.obj";
            touchedMesh = true;
        }
    }
    REQUIRE(touchedMesh);
    Scene dest;
    PlayState destState;
    CHECK_FALSE(applySimPlay(dest, destState, blob));
}

TEST_CASE("spawned materials survive capture")
{
    const Material gem = Material::makeDiffuse(Vec3(0.1, 0.9, 0.2));
    Scene scene;
    Object *ground = scene.addPlane(Vec3(0, 0, 0), Vec3(0, 1, 0), Material::makeDiffuse(Vec3(0.8, 0.8, 0.8)));
    ground->setTag("solid");
    Object *player = scene.addSphere(Vec3(0, 2, 0), 0.5, Material::makeDiffuse(Vec3(0.8, 0.8, 0.8)));
    player->setTag("player");
    Object *pickup = scene.addSphere(Vec3(0, 0.5, 0), 0.2, gem);
    pickup->setTag("pickup");
    PlayState state;
    seedPlayRng(state);
    const SimPlayBlob blob = captureSimPlay(scene, state, 0, Vec3(0, 0, -1));
    Scene dest;
    PlayState destState;
    REQUIRE(applySimPlay(dest, destState, blob));
    Object *loaded = dest.find(pickup->id());
    REQUIRE(loaded != nullptr);
    CHECK(loaded->material().albedo.x == gem.albedo.x);
    CHECK(loaded->material().albedo.y == gem.albedo.y);
    CHECK(loaded->material().albedo.z == gem.albedo.z);
}
