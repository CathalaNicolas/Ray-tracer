#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "Content.hpp"
#include "Jobs.hpp"
#include "Log.hpp"
#include "Play.hpp"
#include "DemoScene.hpp"
#include "JoltPlay.hpp"

#include <atomic>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

// Headless play and demo-scene checks. Renderer / FBX writer checks stay on
// `raytracer.exe --self-test` (offscreen GL). tests links core only; OBJ loads
// in MeshObj.cpp. It does not link import, render, ImGui, or OpenGL.
TEST_CASE("play self-tests (headless)")
{
    CHECK(runPlaySelfTests() == 0);
}

TEST_CASE("demo scene steps headless")
{
    Scene scene = createDemoScene();
    PlayState state;
    SimSession sim;
    sim.attach(scene, state);
    sim.begin();
    for (int frame = 0; frame < 2; ++frame)
        sim.step(PlayInput{}, Vec3(0, 0, -1));
    CHECK(state.playerId != kInvalidEntityId);
    CHECK(sim.snapshot().playerId == state.playerId);
}

TEST_CASE("jobs parallelFor and pinned file read")
{
    REQUIRE(jobs::ready());

    // Smoke that the pool runs a multi-range set without hanging.
    std::atomic<std::uint32_t> ranges{0};
    jobs::parallelFor(64, [&ranges](std::uint32_t begin, std::uint32_t end) {
        if (begin < end)
            ranges.fetch_add(1, std::memory_order_relaxed);
    });
    CHECK(ranges.load() > 0);

    std::uint32_t expected = 0;
    for (std::uint32_t i = 0; i < 64; ++i)
        expected += i;
    std::uint32_t serial = 0;
    jobs::parallelFor(1, [&serial, expected](std::uint32_t, std::uint32_t) {
        serial = expected;
    });
    CHECK(serial == expected);

    std::string bytes;
    jobs::runPinned([&bytes]() {
        std::ifstream in("vcpkg.json", std::ios::binary);
        bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    });
    CHECK(bytes.find("\"enkits\"") != std::string::npos);
}

TEST_CASE("jolt character virtual spike (headless)")
{
    REQUIRE(jobs::ready());
    REQUIRE(jobs::joltJobSystem() != nullptr);

    const float dt = 1.f / 60.f;

    SUBCASE("init world, fall onto a static box")
    {
        jolt_play::World world;
        REQUIRE(world.valid());
        world.addStaticBox(Vec3(0, -0.5, 0), Vec3(20, 0.5, 20));
        world.spawnCapsule(Vec3(0, 2, 0));
        REQUIRE(world.hasCapsule());
        for (int i = 0; i < 180; ++i)
            world.step(dt);
        CHECK(world.grounded());
        CHECK(world.capsuleFeet().y < 0.15);
    }

    SUBCASE("steps a 0.35 curb")
    {
        jolt_play::World world;
        REQUIRE(world.valid());
        world.addStaticBox(Vec3(0, -0.5, 0), Vec3(20, 0.5, 20));
        world.addStaticBox(Vec3(8, 0.175, 0), Vec3(7, 0.175, 8));
        world.spawnCapsule(Vec3(0, 0.5, 0));
        for (int i = 0; i < 60; ++i)
            world.step(dt);
        REQUIRE(world.grounded());
        world.setWishVelocityXZ(jolt_play::kMoveSpeed, 0);
        for (int i = 0; i < 180; ++i)
            world.step(dt);
        CHECK(world.capsuleFeet().x > 1.5);
        CHECK(world.capsuleFeet().y > 0.25);
    }

    SUBCASE("kinematic platform carry")
    {
        jolt_play::World world;
        REQUIRE(world.valid());
        const int platform = world.addKinematicBox(Vec3(0, 0.1, 0), Vec3(1.5, 0.1, 1.5));
        REQUIRE(platform != 0);
        world.spawnCapsule(Vec3(0, 1.2, 0));
        for (int i = 0; i < 90; ++i)
            world.step(dt);
        REQUIRE(world.grounded());
        const double startX = world.capsuleFeet().x;
        Vec3 platformCenter(0, 0.1, 0);
        for (int i = 0; i < 60; ++i)
        {
            platformCenter.x += 0.04;
            world.moveKinematicBox(platform, platformCenter);
            world.step(dt);
        }
        CHECK(world.capsuleFeet().x > startX + 1.5);
        CHECK(world.grounded());
    }

    SUBCASE("static triangle mesh floor")
    {
        jolt_play::World world;
        REQUIRE(world.valid());
        const std::vector<Vec3> verts = {
            Vec3(-8, 0, -8), Vec3(8, 0, -8), Vec3(8, 0, 8), Vec3(-8, 0, 8),
        };
        const std::vector<std::uint32_t> indices = {0, 1, 2, 0, 2, 3};
        world.addStaticMesh(verts, indices);
        world.spawnCapsule(Vec3(0, 1.5, 0));
        for (int i = 0; i < 180; ++i)
            world.step(dt);
        CHECK(world.grounded());
        CHECK(world.capsuleFeet().y < 0.15);
    }
}

int main(int argc, char **argv)
{
    logging::init();
    jobs::init();
    contentInit(".");

    doctest::Context context;
    context.applyCommandLine(argc, argv);
    const int result = context.run();

    contentShutdown();
    jobs::shutdown();
    logging::shutdown();
    return result;
}
