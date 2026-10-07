#include "Play.hpp"

#include "DemoScene.hpp"

#include <cmath>
#include <iostream>
#include <string>

int runPlaySelfTests()
{
    int failed = 0;
    auto expect = [&](bool condition, const char *name) {
        if (!condition)
        {
            std::cerr << "FAIL " << name << '\n';
            ++failed;
        }
    };

    const Material paint = Material::makeDiffuse(Vec3(0.8, 0.8, 0.8));
    Scene scene;
    Object *ground = scene.addPlane(Vec3(0, 0, 0), Vec3(0, 1, 0), paint);
    ground->setTag("solid");
    Object *player = scene.addSphere(Vec3(0, 2, 0), 0.5, paint);
    player->setTag("player");
    const EntityId playerId = player->id();

    PlayState state;
    SimSession sim;
    sim.attach(scene, state);
    sim.begin();
    for (int frame = 0; frame < 90; ++frame)
        sim.step(PlayInput{}, Vec3(0, 0, -1));
    expect(state.playerId == playerId, "play discovers the player entity");
    expect(state.physics && state.physics->valid() && state.physics->hasCapsule(), "play owns a Jolt capsule");
    expect(std::abs(player->center().y - 0.5) <= 0.12, "player capsule rests on plane");
    expect(sim.tick() == 90, "sim session counts ticks");
    expect(sim.snapshot().playerId == playerId, "snapshot copies player id");

    Object *wall = scene.addSphere(Vec3(2, 0.5, 0), 0.5, paint);
    wall->setTag("solid");
    PlayInput walk;
    walk.moveX = 1;
    for (int frame = 0; frame < 60; ++frame)
        sim.step(walk, Vec3(0, 0, -1));
    expect(player->center().x < 1.6, "jolt solids block the player capsule");

    Object *pickup = scene.addSphere(player->center(), 0.2, paint);
    pickup->setTag("pickup");
    const EntityId pickupId = pickup->id();
    sim.step(PlayInput{}, Vec3(0, 0, -1));
    expect(state.score == 1 && scene.find(pickupId) == nullptr, "pickup scores and is removed");
    bool sawPickup = false;
    for (const SimEvent &event : sim.events())
    {
        if (event.kind == SimEventKind::Pickup && event.id == pickupId)
            sawPickup = true;
    }
    expect(sawPickup, "pickup emits SimEventKind::Pickup");
    expect(scene.particles().empty(), "pickup does not spawn particles in sim");

    Scene parentScene;
    Object *parent = parentScene.addSphere(Vec3(1, 0, 0), 1, paint);
    parent->setLocalRotation(Vec3(0, 90, 0));
    parent->setLocalScale(2);
    Object *child = parentScene.addSphere(Vec3(1, 0, 0), 0.5, paint);
    child->setParentId(parent->id());
    expect(std::abs(child->center().x - 1.0) < 1e-4
        && std::abs(child->center().z + 2.0) < 1e-4
        && std::abs(child->worldRadius() - 1.0) < 1e-4,
        "sphere children inherit parent rotation and scale");

    Scene demo = createDemoScene();
    PlayState demoState;
    SimSession demoSim;
    demoSim.attach(demo, demoState);
    demoSim.begin();
    for (int frame = 0; frame < 2; ++frame)
        demoSim.step(PlayInput{}, Vec3(0, 0, -1));
    expect(demoState.playerId != kInvalidEntityId, "demo scene steps headless");

    Snapshot a = demoSim.previousSnapshot();
    Snapshot b = demoSim.snapshot();
    const Snapshot mid = interpolateSnapshots(a, b, 0.5);
    expect(mid.poses.size() == b.poses.size(), "interpolated snapshot keeps pose count");

    Scene padScene;
    Object *rider = padScene.addSphere(Vec3(0, 1.2, 0), 0.35, paint);
    rider->setTag("player");
    Object *pad = padScene.addMesh();
    std::string padError;
    if (pad->load("assets/platform.obj", padError))
    {
        pad->setTag("platform");
        pad->setPosition(Vec3(0, 0, 0));
        pad->motion().move = Vec3(1.6, 0, 0);
        pad->motion().period = 4;
        PlayState padState;
        SimSession padSim;
        padSim.attach(padScene, padState);
        padSim.begin();
        for (int frame = 0; frame < 45; ++frame)
            padSim.step(PlayInput{}, Vec3(0, 0, -1));
        const double startX = rider->center().x;
        for (int frame = 0; frame < 50; ++frame)
            padSim.step(PlayInput{}, Vec3(0, 0, -1));
        expect(rider->center().x > startX + 0.4, "jolt kinematic platform carries the player");
    }
    else
        expect(false, "platform mesh loads for carry test");

    return failed;
}
