#include "Play.hpp"
#include "PlayDetail.hpp"

#include "Collision.hpp"
#include "DemoScene.hpp"
#include "Mesh.hpp"
#include "Plane.hpp"
#include "Sphere.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

int runPlaySelfTests()
{
    int failed = 0;
    auto expect = [&](bool condition, const std::string &name)
    {
        if (!condition)
        {
            std::cerr << "FAIL " << name << "\n";
            ++failed;
        }
    };
    auto near = [](double a, double b, double eps = 1e-4)
    {
        return std::abs(a - b) <= eps;
    };

    const Material paint = Material::makeDiffuse(Vec3(0.8, 0.8, 0.8));

    {
        Scene scene;
        auto deck = std::make_unique<Sphere>(Vec3(0, 0.5, 0), 0.5, paint);
        deck->tag = "platform";
        deck->motion.move = Vec3(2, 0, 0);
        deck->motion.period = 2;
        scene.add(std::move(deck));
        auto body = std::make_unique<Sphere>(Vec3(0, 1.5, 0), 0.5, paint);
        body->tag = "player";
        const int playerId = scene.add(std::move(body));
        PlayState state;
        stepPlay(scene, PlayInput{}, Vec3(0, 0, -1), 1.f / 60.f, state);
        const Sphere *player = dynamic_cast<const Sphere *>(scene.find(playerId));
        expect(player != nullptr && player->center().x > 0.01, "a moving platform carries the player");
    }

    expect(sphereIntersectsSphere(Vec3(0, 0, 0), 1, Vec3(1.5, 0, 0), 1), "sphere-sphere overlap");
    expect(sphereIntersectsSphere(Vec3(0, 0, 0), 1, Vec3(2, 0, 0), 1), "touching spheres overlap");
    expect(!sphereIntersectsSphere(Vec3(0, 0, 0), 1, Vec3(3, 0, 0), 1), "separated spheres do not overlap");

    expect(sphereIntersectsAabb(Vec3(0, 0, 0), 1, Vec3(0.5, -1, -1), Vec3(2, 1, 1)), "sphere-aabb overlap");
    expect(sphereIntersectsAabb(Vec3(0, 0, 0), 0.2, Vec3(-1, -1, -1), Vec3(1, 1, 1)), "sphere inside an aabb overlaps");
    expect(!sphereIntersectsAabb(Vec3(0, 0, 0), 0.4, Vec3(1, -1, -1), Vec3(2, 1, 1)), "separated sphere and aabb do not overlap");

    Vec3 resolved(1, 2, 3);
    Hit push;
    push.hit = true;
    push.normal = Vec3(0, 2, 0);
    push.penetration = 0.4f;
    resolveSphere(resolved, push);
    expect(near(resolved.x, 1) && near(resolved.y, 2.4) && near(resolved.z, 3), "resolve pushes the sphere out along the normal");

    {
        Scene scene;
        auto ground = std::make_unique<Plane>(Vec3(0, 0, 0), Vec3(0, 1, 0), paint);
        ground->tag = "solid";
        scene.add(std::move(ground));
        auto body = std::make_unique<Sphere>(Vec3(0, 2, 0), 0.5, paint);
        body->tag = "player";
        scene.add(std::move(body));

        PlayState state;
        PlayInput input;
        for (int frame = 0; frame < 180; ++frame)
            stepPlay(scene, input, Vec3(0, 0, -1), 1.f / 60.f, state);

        auto *player = dynamic_cast<Sphere *>(scene.find(state.playerId));
        expect(player != nullptr && std::abs(player->center().y - 0.5) <= 0.05, "player sphere falling onto a plane tagged solid ends near y = radius");
        expect(player != nullptr && near(player->center().x, 0, 1e-3) && near(player->center().z, 0, 1e-3), "a falling player stays on its vertical line");

        if (player)
        {
            const double rested = player->center().y;
            input.jump = true;
            stepPlay(scene, input, Vec3(0, 0, -1), 1.f / 60.f, state);
            expect(player->center().y > rested + 0.05, "a grounded player jumps");
        }
    }

    {
        Scene scene;
        auto body = std::make_unique<Sphere>(Vec3(0, 1, 0), 0.5, paint);
        body->tag = "player";
        scene.add(std::move(body));
        auto gem = std::make_unique<Sphere>(Vec3(0.2, 1, 0), 0.3, paint);
        gem->tag = "pickup";
        const int gemId = scene.add(std::move(gem));

        PlayState state;
        stepPlay(scene, PlayInput{}, Vec3(0, 0, -1), 1.f / 60.f, state);
        expect(state.score == 1, "a pickup sphere overlapping the player scores 1");
        expect(scene.find(gemId) == nullptr, "a pickup sphere overlapping the player is removed");
        expect(state.message == "Picked up", "a pickup sets the picked up message");
    }

    {
        Scene scene;
        auto body = std::make_unique<Sphere>(Vec3(0, 1, 0), 0.5, paint);
        body->tag = "player";
        scene.add(std::move(body));
        auto gem = std::make_unique<Sphere>(Vec3(0.2, 1, 0), 0.3, paint);
        gem->tag = "pickup";
        scene.add(std::move(gem));

        PlayState state;
        state.health = 2;
        stepPlay(scene, PlayInput{}, Vec3(0, 0, -1), 1.f / 60.f, state);
        expect(state.health == 3 && state.score == 1, "a pickup while health is 2 raises it to 3 and scores");
    }

    {
        Scene scene;
        auto body = std::make_unique<Sphere>(Vec3(0, 1, 0), 0.5, paint);
        body->tag = "player";
        scene.add(std::move(body));
        auto gem = std::make_unique<Sphere>(Vec3(0.2, 1, 0), 0.3, paint);
        gem->tag = "pickup";
        scene.add(std::move(gem));

        PlayState state;
        stepPlay(scene, PlayInput{}, Vec3(0, 0, -1), 1.f / 60.f, state);
        expect(state.health == 3 && state.score == 1, "a pickup while health is 3 scores and leaves health at 3");
    }

    {
        Scene scene;
        auto body = std::make_unique<Sphere>(Vec3(0, 5, 0), 0.5, paint);
        body->tag = "player";
        scene.add(std::move(body));

        PlayInput input;
        input.moveX = 1;
        input.moveZ = 1;
        PlayState state;
        stepPlay(scene, input, Vec3(0, 0, -1), 0.25f, state);
        auto *player = dynamic_cast<Sphere *>(scene.find(state.playerId));
        expect(player != nullptr, "movement keeps the player");
        if (player)
        {
            const double dx = player->center().x;
            const double dz = player->center().z;
            expect(near(std::sqrt(dx * dx + dz * dz), 1, 1e-3), "diagonal movement stays at move speed");
            expect(dx > 0.5 && dz < -0.5, "strafe follows camera right and forward follows camera forward");
        }

        PlayState airborne;
        PlayInput hop;
        hop.jump = true;
        stepPlay(scene, hop, Vec3(0, 0, -1), 0.25f, airborne);
        expect(airborne.verticalVelocity < 0, "jump in the air does not set jump speed");
    }

    {
        Scene scene;
        auto ground = std::make_unique<Plane>(Vec3(0, 0, 0), Vec3(0, 1, 0), paint);
        ground->tag = "solid";
        scene.add(std::move(ground));
        auto body = std::make_unique<Sphere>(Vec3(0, 0.5, 0), 0.5, paint);
        body->tag = "player";
        scene.add(std::move(body));
        auto wall = std::make_unique<Sphere>(Vec3(2, 0.5, 0), 0.5, paint);
        wall->tag = "solid";
        scene.add(std::move(wall));

        PlayInput input;
        input.moveX = 1;
        PlayState state;
        for (int frame = 0; frame < 90; ++frame)
            stepPlay(scene, input, Vec3(0, 0, -1), 1.f / 60.f, state);
        auto *player = dynamic_cast<Sphere *>(scene.find(state.playerId));
        expect(player != nullptr && std::abs(player->center().x - 1) <= 0.05, "the player slides to a stop against a solid sphere");
    }

    {
        Scene scene;
        auto body = std::make_unique<Sphere>(Vec3(0, 1, 0), 0.5, paint);
        body->tag = "player";
        scene.add(std::move(body));
        auto goal = std::make_unique<Sphere>(Vec3(0, 1, 0), 0.4, paint);
        goal->name = "Goal";
        goal->tag = "trigger";
        const int goalId = scene.add(std::move(goal));

        PlayState state;
        stepPlay(scene, PlayInput{}, Vec3(0, 0, -1), 1.f / 60.f, state);
        expect(state.message == "Reached Goal", "a trigger overlap reports the object name");
        expect(scene.find(goalId) != nullptr, "a trigger overlap keeps the object");
        expect(state.score == 0, "a trigger overlap does not score");

        Scene blank;
        auto runner = std::make_unique<Sphere>(Vec3(0, 1, 0), 0.5, paint);
        runner->tag = "player";
        blank.add(std::move(runner));
        auto zone = std::make_unique<Sphere>(Vec3(0, 1, 0), 0.4, paint);
        zone->tag = "trigger";
        blank.add(std::move(zone));
        PlayState unnamed;
        stepPlay(blank, PlayInput{}, Vec3(0, 0, -1), 1.f / 60.f, unnamed);
        expect(unnamed.message == "Triggered", "a nameless trigger reports Triggered");
    }

    {
        Scene scene;
        auto body = std::make_unique<Sphere>(Vec3(0, 3, 0), 0.5, paint);
        body->tag = "player";
        const int id = scene.add(std::move(body));
        PlayState state;
        state.paused = true;
        state.verticalVelocity = 4;
        stepPlay(scene, PlayInput{}, Vec3(0, 0, -1), 0.5f, state);
        auto *player = dynamic_cast<Sphere *>(scene.find(id));
        expect(player != nullptr && near(player->center().y, 3), "paused play does not move the player");
        expect(state.verticalVelocity == 4.f, "paused play keeps vertical velocity");
        expect(state.playerId == -1, "paused play returns before selecting the player");
    }

    {
        Scene scene;
        PlayState state;
        state.playerId = 7;
        state.score = 3;
        stepPlay(scene, PlayInput{}, Vec3(0, 0, -1), 0.016f, state);
        expect(state.playerId == -1, "a scene without a player leaves playerId at -1");
        expect(state.score == 3, "a scene without a player does not change the score");
    }

    {
        double accumulator = 0;
        expect(takePlaySteps(kPlayStep, 1, accumulator, false) == 1, "one 1/60 s frame runs one step");
        expect(near(accumulator, 0), "an exact step leaves the accumulator empty");

        accumulator = 0;
        expect(takePlaySteps(kPlayStep * 0.5, 1, accumulator, false) == 0, "half a frame does not step");
        expect(takePlaySteps(kPlayStep * 0.5, 1, accumulator, false) == 1, "two half frames make one step");

        accumulator = 0.01;
        expect(takePlaySteps(0, 1, accumulator, true) == 1, "a single step runs while paused");
        expect(near(accumulator, 0.01), "a single step keeps the accumulator");

        accumulator = 0;
        expect(takePlaySteps(kPlayStep, 2, accumulator, false) == 2, "double speed runs two steps in one frame");

        accumulator = 0;
        expect(takePlaySteps(maxPlayFrameDt(), 1, accumulator, false) == maxPlayStepsPerFrame(), "a hitch runs at most four steps");
        expect(near(accumulator, 0), "a hitch drops the leftover time");

        expect(near(clampFrameDt(-1), maxPlayFrameDt()) && near(clampFrameDt(5), maxPlayFrameDt()), "a bad frame dt is clamped");
    }

    {
        Scene scene;
        auto ground = std::make_unique<Plane>(Vec3(0, 0, 0), Vec3(0, 1, 0), paint);
        ground->tag = "solid";
        scene.add(std::move(ground));
        auto body = std::make_unique<Sphere>(Vec3(0, 0.5, 1.2), 0.5, paint);
        body->tag = "player";
        scene.add(std::move(body));

        MeshTri wallA;
        wallA.position[0] = Vec3(-1, 0, 0);
        wallA.position[1] = Vec3(1, 0, 0);
        wallA.position[2] = Vec3(1, 2, 0);
        MeshTri wallB;
        wallB.position[0] = Vec3(-1, 0, 0);
        wallB.position[1] = Vec3(1, 2, 0);
        wallB.position[2] = Vec3(-1, 2, 0);
        auto wall = std::make_unique<Mesh>();
        wall->setTriangles({wallA, wallB});
        wall->tag = "solid";
        wall->setPosition(Vec3(0, 0, 0));
        scene.add(std::move(wall));

        PlayInput input;
        input.moveZ = 1;
        PlayState state;
        for (int frame = 0; frame < 90; ++frame)
            stepPlay(scene, input, Vec3(0, 0, -1), 1.f / 60.f, state);
        auto *player = dynamic_cast<Sphere *>(scene.find(state.playerId));
        expect(player != nullptr && player->center().z > 0.4 && player->center().z < 0.7, "the player stops against mesh triangles");
    }

    {
        Scene scene;
        auto ground = std::make_unique<Plane>(Vec3(0, 0, 0), Vec3(0, 1, 0), paint);
        ground->tag = "solid";
        scene.add(std::move(ground));
        auto body = std::make_unique<Sphere>(Vec3(1.2, 0.5, 0), 0.5, paint);
        body->tag = "player";
        scene.add(std::move(body));

        MeshTri trunk;
        trunk.position[0] = Vec3(-0.05, 0, -0.05);
        trunk.position[1] = Vec3(0.05, 0, 0.05);
        trunk.position[2] = Vec3(0, 1.2, 0);
        MeshTri branch;
        branch.position[0] = Vec3(1.8, 2.2, -0.3);
        branch.position[1] = Vec3(2.4, 2.2, 0.3);
        branch.position[2] = Vec3(2.1, 2.8, 0);
        auto tree = std::make_unique<Mesh>();
        tree->setTriangles({trunk, branch});
        tree->tag = "solid";
        scene.add(std::move(tree));

        PlayInput input;
        input.moveX = 1;
        PlayState state;
        for (int frame = 0; frame < 45; ++frame)
            stepPlay(scene, input, Vec3(0, 0, -1), 1.f / 60.f, state);
        auto *player = dynamic_cast<Sphere *>(scene.find(state.playerId));
        expect(player != nullptr && player->center().x > 3.0, "the player walks under a high mesh triangle instead of its bounding box");
    }

    {
        Scene scene;
        auto body = std::make_unique<Sphere>(Vec3(0, 1, 0), 0.5, paint);
        body->tag = "player";
        scene.add(std::move(body));
        auto goal = std::make_unique<Sphere>(Vec3(0, 1, 0), 0.4, paint);
        goal->name = "Goal";
        goal->tag = "goal";
        scene.add(std::move(goal));

        PlayState waiting;
        stepPlay(scene, PlayInput{}, Vec3(0, 0, -1), 1.f / 60.f, waiting);
        expect(waiting.result.empty() && waiting.message == "Press F", "overlapping a goal prompts for use");
        expect(!waiting.paused, "overlapping a goal does not end the round");

        PlayInput use;
        use.use = true;
        PlayState won;
        stepPlay(scene, use, Vec3(0, 0, -1), 1.f / 60.f, won);
        expect(won.result == "won" && won.message == "You win" && won.paused, "using the goal wins and pauses");
        expect(won.room == 0, "room 0 goal still wins");

        PlayState stuck = won;
        const Vec3 held = dynamic_cast<Sphere *>(scene.find(won.playerId))->center();
        stepPlay(scene, use, Vec3(0, 0, -1), 1.f / 60.f, stuck);
        expect(near(dynamic_cast<Sphere *>(scene.find(stuck.playerId))->center().y, held.y), "a finished round does not keep simulating");
    }

    {
        Scene scene;
        auto body = std::make_unique<Sphere>(Vec3(0, 1, 0), 0.5, paint);
        body->tag = "player";
        scene.add(std::move(body));
        auto goal = std::make_unique<Sphere>(Vec3(0, 1, 0), 0.4, paint);
        goal->name = "Goal";
        goal->tag = "goal";
        scene.add(std::move(goal));

        PlayState state;
        state.room = 1;
        state.score = 5;
        state.health = 2;
        PlayInput use;
        use.use = true;
        stepPlay(scene, use, Vec3(0, 0, -1), 1.f / 60.f, state);
        expect(state.room == 2 && state.result.empty() && state.message == "Next room" && !state.paused, "room 1 goal use switches to the second room");
        expect(state.score == 5 && state.health == 2, "room 1 goal use keeps the score");

        Sphere *eastPlayer = nullptr;
        Sphere *eastGoal = nullptr;
        for (const auto &object : scene.objects())
        {
            if (object->tag == "player")
                eastPlayer = dynamic_cast<Sphere *>(object.get());
            if (object->tag == "goal")
                eastGoal = dynamic_cast<Sphere *>(object.get());
        }
        expect(eastPlayer != nullptr && eastGoal != nullptr, "the second room has a player and a goal");
        if (eastPlayer != nullptr && eastGoal != nullptr)
            eastPlayer->setCenter(eastGoal->center());
        stepPlay(scene, use, Vec3(0, 0, -1), 1.f / 60.f, state);
        expect(state.result == "won" && state.message == "You win" && state.paused, "using the goal after that wins");
    }

    {
        Scene scene;
        auto body = std::make_unique<Sphere>(Vec3(0, 1, 0), 0.5, paint);
        body->tag = "player";
        scene.add(std::move(body));
        auto trap = std::make_unique<Sphere>(Vec3(0, 1, 0), 0.4, paint);
        trap->tag = "hazard";
        scene.add(std::move(trap));
        PlayState state;
        stepPlay(scene, PlayInput{}, Vec3(0, 0, -1), 1.f / 60.f, state);
        expect(state.result == "lost" && state.message == "You lose" && state.paused, "a hazard overlap loses the round");
        expect(state.health == 0, "a hazard with no spawn point spends the remaining health");
    }

    {
        Scene scene;
        auto body = std::make_unique<Sphere>(Vec3(0, -4, 0), 0.5, paint);
        body->tag = "player";
        scene.add(std::move(body));
        PlayState state;
        stepPlay(scene, PlayInput{}, Vec3(0, 0, -1), 1.f / 60.f, state);
        expect(state.result == "lost" && state.message == "You lose", "falling below the ground loses the round");
    }

    {
        Scene scene;
        auto body = std::make_unique<Sphere>(Vec3(0, 1, 0), 0.5, paint);
        body->tag = "player";
        scene.add(std::move(body));
        auto door = std::make_unique<Sphere>(Vec3(0, 1, 0), 0.4, paint);
        door->name = "Door";
        door->tag = "use";
        scene.add(std::move(door));
        PlayInput use;
        use.use = true;
        PlayState state;
        stepPlay(scene, use, Vec3(0, 0, -1), 1.f / 60.f, state);
        expect(state.message == "Door" && state.result.empty() && !state.paused, "use activates a tagged object without ending the round");
    }

    {
        Scene scene;
        auto ground = std::make_unique<Plane>(Vec3(0, 0, 0), Vec3(0, 1, 0), paint);
        ground->tag = "solid";
        scene.add(std::move(ground));
        auto body = std::make_unique<Sphere>(Vec3(0, 0.5, 0), 0.5, paint);
        body->tag = "player";
        scene.add(std::move(body));
        MeshTri topA;
        topA.position[0] = Vec3(0.7, 0.2, -1);
        topA.position[1] = Vec3(6, 0.2, 1);
        topA.position[2] = Vec3(6, 0.2, -1);
        MeshTri topB;
        topB.position[0] = Vec3(0.7, 0.2, -1);
        topB.position[1] = Vec3(0.7, 0.2, 1);
        topB.position[2] = Vec3(6, 0.2, 1);
        MeshTri faceA;
        faceA.position[0] = Vec3(0.7, 0, -1);
        faceA.position[1] = Vec3(0.7, 0, 1);
        faceA.position[2] = Vec3(0.7, 0.2, 1);
        MeshTri faceB;
        faceB.position[0] = Vec3(0.7, 0, -1);
        faceB.position[1] = Vec3(0.7, 0.2, 1);
        faceB.position[2] = Vec3(0.7, 0.2, -1);
        auto curb = std::make_unique<Mesh>();
        curb->setTriangles({topA, topB, faceA, faceB});
        curb->tag = "solid";
        scene.add(std::move(curb));
        PlayInput input;
        input.moveX = 1;
        PlayState state;
        for (int frame = 0; frame < 45; ++frame)
            stepPlay(scene, input, Vec3(0, 0, -1), 1.f / 60.f, state);
        auto *player = dynamic_cast<Sphere *>(scene.find(state.playerId));
        expect(player != nullptr && player->center().x > 2.0, "the player steps onto a low curb");
        expect(player != nullptr && player->center().y > 0.65, "the player stands on top of the curb");
    }

    {
        Scene scene;
        auto body = std::make_unique<Sphere>(Vec3(0, 0.5, 0), 0.5, paint);
        body->tag = "player";
        const int playerId = scene.add(std::move(body));
        auto wall = std::make_unique<Sphere>(Vec3(0, 0.5, 2), 0.5, paint);
        wall->tag = "solid";
        scene.add(std::move(wall));
        const Vec3 pulled = chaseCameraPosition(scene, playerId, Vec3(0, 0.5, 4));
        expect(pulled.z < 2.0 && pulled.z > 0.4, "the chase camera stops before a solid");

        Scene clear;
        auto runner = std::make_unique<Sphere>(Vec3(0, 0.5, 0), 0.5, paint);
        runner->tag = "player";
        const int runnerId = clear.add(std::move(runner));
        const Vec3 free = chaseCameraPosition(clear, runnerId, Vec3(0, 1, 4));
        expect(near(free.z, 4), "the chase camera keeps its distance when nothing is in the way");

        Scene fenceScene;
        auto runnerFence = std::make_unique<Sphere>(Vec3(0, 0.5, 0), 0.5, paint);
        runnerFence->tag = "player";
        const int fencePlayer = fenceScene.add(std::move(runnerFence));
        auto fence = std::make_unique<Sphere>(Vec3(0, 0.5, 2), 0.5, paint);
        fence->tag = "solid";
        fence->layer = 1;
        fenceScene.add(std::move(fence));
        const Vec3 through = chaseCameraPosition(fenceScene, fencePlayer, Vec3(0, 0.5, 4));
        expect(near(through.z, 4), "the chase camera passes a player-only layer");
    }

    {
        Scene scene;
        auto mover = std::make_unique<Sphere>(Vec3(0, 0, 0), 0.2, paint);
        mover->motion.move = Vec3(2, 0, 0);
        mover->motion.period = 2;
        const int id = scene.add(std::move(mover));
        PlayState state;
        stepPlay(scene, PlayInput{}, Vec3(0, 0, -1), 1.f, state);
        auto *sphere = dynamic_cast<Sphere *>(scene.find(id));
        expect(sphere != nullptr && near(sphere->center().x, 1, 1e-3), "motion is halfway across at half the period");
        stepPlay(scene, PlayInput{}, Vec3(0, 0, -1), 1.f, state);
        expect(sphere != nullptr && near(sphere->center().x, 2, 1e-3), "motion reaches the offset at one period");
        stepPlay(scene, PlayInput{}, Vec3(0, 0, -1), 2.f, state);
        expect(sphere != nullptr && near(sphere->center().x, 0, 1e-3), "motion returns to the rest pose");
    }

    {
        Scene scene;
        MeshTri triangle;
        triangle.position[0] = Vec3(0, 0, 0);
        triangle.position[1] = Vec3(1, 0, 0);
        triangle.position[2] = Vec3(0, 0, 1);
        auto mesh = std::make_unique<Mesh>();
        mesh->setTriangles({triangle});
        mesh->setScale(1);
        mesh->motion.rotate = Vec3(0, 90, 0);
        mesh->motion.scale = 1;
        mesh->motion.period = 1;
        const int id = scene.add(std::move(mesh));
        PlayState state;
        stepPlay(scene, PlayInput{}, Vec3(0, 0, -1), 1.f, state);
        auto *moved = dynamic_cast<Mesh *>(scene.find(id));
        expect(moved != nullptr && near(moved->rotation().y, 90, 1e-3), "motion rotates a mesh");
        expect(moved != nullptr && near(moved->scale(), 2, 1e-3), "motion scales a mesh");
    }

    {
        Scene scene;
        auto source = std::make_unique<Sphere>(Vec3(1, 0, 0), 0.1, paint);
        source->tag = "spawner";
        source->spawnEvery = 3;
        const int id = scene.add(std::move(source));
        PlayState state;
        stepPlay(scene, PlayInput{}, Vec3(0, 0, -1), 3.f, state);
        int pickups = 0;
        int spawnedId = -1;
        for (const auto &object : scene.objects())
        {
            if (object->tag == "pickup")
            {
                ++pickups;
                spawnedId = object->id;
            }
        }
        expect(pickups == 1, "a spawner adds a pickup when its interval elapses");
        stepPlay(scene, PlayInput{}, Vec3(0, 0, -1), 1.f, state);
        pickups = 0;
        for (const auto &object : scene.objects())
        {
            if (object->tag == "pickup")
                ++pickups;
        }
        expect(pickups == 1, "a spawner waits while its pickup is still in the scene");
        scene.remove(spawnedId);
        stepPlay(scene, PlayInput{}, Vec3(0, 0, -1), 3.f, state);
        pickups = 0;
        for (const auto &object : scene.objects())
        {
            if (object->tag == "pickup")
                ++pickups;
        }
        expect(pickups == 1 && scene.find(id) != nullptr, "a spawner replaces a collected pickup");
    }

    {
        Scene scene;
        auto body = std::make_unique<Sphere>(Vec3(0, 1, 0), 0.5, paint);
        body->tag = "player";
        scene.add(std::move(body));
        auto trap = std::make_unique<Sphere>(Vec3(0, 1, 0), 0.4, paint);
        trap->tag = "hazard";
        scene.add(std::move(trap));
        auto point = std::make_unique<Sphere>(Vec3(3, 0.5, 1), 0.1, paint);
        point->tag = "spawn";
        scene.add(std::move(point));
        PlayState state;
        stepPlay(scene, PlayInput{}, Vec3(0, 0, -1), 1.f / 60.f, state);
        auto *player = dynamic_cast<Sphere *>(scene.find(state.playerId));
        expect(state.result.empty() && state.message == "Respawned", "a spawn point respawns instead of ending the round");
        expect(player != nullptr && near(player->center().x, 3) && near(player->center().y, 0.5) && near(player->center().z, 1), "respawn moves the player to the spawn point");
        expect(near(state.verticalVelocity, 0.f), "respawn clears vertical speed");
        expect(state.health == 2, "a hazard costs one health when a spawn point exists");
    }

    {
        Scene scene;
        auto body = std::make_unique<Sphere>(Vec3(0, 1, 0), 0.5, paint);
        body->tag = "player";
        scene.add(std::move(body));
        auto trap = std::make_unique<Sphere>(Vec3(0, 1, 0), 0.4, paint);
        trap->tag = "hazard";
        scene.add(std::move(trap));
        auto point = std::make_unique<Sphere>(Vec3(3, 0.5, 1), 0.1, paint);
        point->tag = "spawn";
        scene.add(std::move(point));
        PlayState state;
        state.health = 1;
        stepPlay(scene, PlayInput{}, Vec3(0, 0, -1), 1.f / 60.f, state);
        expect(state.health == 0 && state.result == "lost" && state.message == "You lose", "zero health loses even when a spawn point exists");
    }

    {
        Scene scene;
        auto body = std::make_unique<Sphere>(Vec3(0, -4, 0), 0.5, paint);
        body->tag = "player";
        scene.add(std::move(body));
        auto point = std::make_unique<Sphere>(Vec3(1, 0.5, 0), 0.1, paint);
        point->tag = "spawn";
        scene.add(std::move(point));
        PlayState state;
        stepPlay(scene, PlayInput{}, Vec3(0, 0, -1), 1.f / 60.f, state);
        expect(state.result.empty() && state.health == 3, "a fall respawns without spending health");
    }

    {
        Scene scene;
        scene.add(std::make_unique<Plane>(Vec3(0, 0, 0), Vec3(0, 1, 0), paint));
        auto body = std::make_unique<Sphere>(Vec3(0, 0.5, 0), 0.5, paint);
        body->tag = "player";
        scene.add(std::move(body));
        auto lever = std::make_unique<Sphere>(Vec3(0, 0.5, 0), 0.4, paint);
        lever->tag = "use";
        lever->name = "Switch";
        lever->action.target = "Gate";
        lever->action.move = Vec3(2, 0, 0);
        scene.add(std::move(lever));
        auto gate = std::make_unique<Sphere>(Vec3(4, 1, 0), 0.2, paint);
        gate->name = "Gate";
        const int gateId = scene.add(std::move(gate));
        PlayInput use;
        use.use = true;
        PlayState state;
        const int steps = static_cast<int>(std::round(actionDuration() / kPlayStep));
        stepPlay(scene, use, Vec3(0, 0, -1), static_cast<float>(kPlayStep), state);
        for (int step = 1; step < steps; ++step)
            stepPlay(scene, PlayInput{}, Vec3(0, 0, -1), static_cast<float>(kPlayStep), state);
        auto *moved = dynamic_cast<Sphere *>(scene.find(gateId));
        expect(moved != nullptr && near(moved->center().x, 6, 1e-3), "use moves the named object to the offset");
        expect(state.result.empty(), "use does not end the round");
        stepPlay(scene, use, Vec3(0, 0, -1), static_cast<float>(kPlayStep), state);
        expect(moved != nullptr && near(moved->center().x, 6, 1e-3), "a second use leaves the object where it stopped");
    }

    {
        Scene scene;
        auto body = std::make_unique<Sphere>(Vec3(0, 0.5, 0), 0.35, paint);
        body->tag = "player";
        const int playerId = scene.add(std::move(body));
        auto wall = std::make_unique<Sphere>(Vec3(0, 0.5, -2), 0.4, paint);
        wall->name = "Wall";
        wall->tag = "solid";
        scene.add(std::move(wall));
        PlayRayHit hit;
        expect(castPlayRay(scene, Vec3(0, 0.5, 0), Vec3(0, 0, -1), playRayDistance(), playerId, hit) && hit.name == "Wall", "a play ray hits the object in front");
        PlayRayHit missed;
        expect(!castPlayRay(scene, Vec3(0, 0.5, 0), Vec3(0, 0, 1), playRayDistance(), playerId, missed), "a play ray misses empty space");
    }

    {
        Scene scene;
        auto body = std::make_unique<Sphere>(Vec3(0, 0.5, 0), 0.35, paint);
        body->tag = "player";
        scene.add(std::move(body));
        auto lever = std::make_unique<Sphere>(Vec3(0, 0.5, -2), 0.2, paint);
        lever->tag = "use";
        lever->name = "Far switch";
        lever->action.target = "Gate";
        lever->action.move = Vec3(2, 0, 0);
        scene.add(std::move(lever));
        auto gate = std::make_unique<Sphere>(Vec3(4, 1, 0), 0.2, paint);
        gate->name = "Gate";
        const int gateId = scene.add(std::move(gate));
        PlayInput use;
        use.use = true;
        PlayState state;
        stepPlay(scene, use, Vec3(0, 0, -1), static_cast<float>(kPlayStep), state);
        expect(state.lookName == "Far switch", "the play ray records the switch in front of the player");
        expect(state.lookTag == "use" && state.lookPoint.z < -1.0, "the play ray stores the hit point on the switch");
        const int steps = static_cast<int>(std::round(actionDuration() / kPlayStep));
        for (int step = 1; step < steps; ++step)
            stepPlay(scene, PlayInput{}, Vec3(0, 0, -1), static_cast<float>(kPlayStep), state);
        auto *moved = dynamic_cast<Sphere *>(scene.find(gateId));
        expect(moved != nullptr && near(moved->center().x, 6, 1e-3), "use along the play ray moves the named object");
    }

    {
        Scene scene;
        auto body = std::make_unique<Sphere>(Vec3(0, 0.5, 0), 0.35, paint);
        body->tag = "player";
        scene.add(std::move(body));
        auto lever = std::make_unique<Sphere>(Vec3(0, 0.5, 0), 0.4, paint);
        lever->tag = "use";
        lever->action.target = "Door";
        lever->action.rotate = Vec3(0, 90, 0);
        scene.add(std::move(lever));
        auto door = std::make_unique<Mesh>();
        door->name = "Door";
        const int doorId = scene.add(std::move(door));
        PlayInput use;
        use.use = true;
        PlayState state;
        const int steps = static_cast<int>(std::round(actionDuration() / kPlayStep));
        stepPlay(scene, use, Vec3(0, 0, -1), static_cast<float>(kPlayStep), state);
        for (int step = 1; step < steps; ++step)
            stepPlay(scene, PlayInput{}, Vec3(0, 0, -1), static_cast<float>(kPlayStep), state);
        auto *turned = dynamic_cast<Mesh *>(scene.find(doorId));
        expect(turned != nullptr && near(turned->rotation().y, 90, 1e-2), "use rotates the named mesh");
        expect(state.tweens.empty(), "a finished tween leaves the list");
    }

    {
        Sphere wall(Vec3(0, 0.5, 2), 0.5, paint);
        wall.setTag("solid");
        wall.layer = 0;
        expect(play_detail::isSolid(wall, -1) && play_detail::isSolid(wall, -1, true), "layer 0 blocks the player and the chase camera");
        wall.layer = 1;
        expect(play_detail::isSolid(wall, -1) && !play_detail::isSolid(wall, -1, true), "layer 1 blocks the player and is skipped by the chase camera");
    }

    {
        Scene scene;
        auto parent = std::make_unique<Mesh>();
        parent->setPosition(Vec3(0, 0, 0));
        parent->setScale(2);
        const int parentId = scene.add(std::move(parent));
        auto child = std::make_unique<Sphere>(Vec3(0, 0, 0), 0.5, paint);
        child->tag = "solid";
        child->parentId = parentId;
        scene.add(std::move(child));
        Hit hit = play_detail::contact(*scene.objects().back(), Vec3(1.4, 0, 0), 0.5);
        expect(hit.hit, "a sphere under a scaled parent collides at its world radius");
    }

    {
        Scene scene;
        auto parent = std::make_unique<Mesh>();
        parent->setPosition(Vec3(0, 0, 0));
        parent->setRotation(Vec3(0, 0, 90));
        const int parentId = scene.add(std::move(parent));
        auto floor = std::make_unique<Plane>(Vec3(0, 0, 0), Vec3(0, 1, 0), paint);
        floor->parentId = parentId;
        scene.add(std::move(floor));
        Hit hit = play_detail::contact(*scene.objects().back(), Vec3(0, 2, 0), 0.5);
        expect(hit.hit, "a plane under a rotated parent collides with its world normal");
    }

    {
        Scene scene;
        auto body = std::make_unique<Sphere>(Vec3(0, 0.5, 0), 0.35, paint);
        body->tag = "player";
        const int playerId = scene.add(std::move(body));
        auto spawn = std::make_unique<Sphere>(Vec3(0, 0.5, -1), 0.3, paint);
        spawn->tag = "spawn";
        spawn->name = "Spawn";
        scene.add(std::move(spawn));
        auto lever = std::make_unique<Sphere>(Vec3(0, 0.5, -2), 0.2, paint);
        lever->tag = "use";
        lever->name = "Switch";
        scene.add(std::move(lever));
        PlayRayHit hit;
        expect(castPlayRay(scene, Vec3(0, 0.5, 0), Vec3(0, 0, -1), playRayDistance(), playerId, hit) && hit.name == "Switch",
            "a play ray skips a spawn and hits the use behind it");
    }

    return failed;
}
