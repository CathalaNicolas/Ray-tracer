#include "JoltPlay.hpp"
#include "Jobs.hpp"
#include "Play.hpp"
#include "SceneFile.hpp"
#include "Terrain.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

TEST_CASE("hill terrain encodes RTT1 and samples height")
{
    const TerrainTile tile = makeHillTerrain(true);
    CHECK(tile.sampleCount == kTerrainSampleCount);
    CHECK(tile.heights.size() == kTerrainSampleCount * kTerrainSampleCount);
    CHECK(tile.splat.size() == tile.heights.size() * 4);

    std::vector<std::uint8_t> bytes;
    std::string error;
    REQUIRE(encodeTerrainTile(tile, bytes, error));
    REQUIRE(bytes.size() >= 8);
    CHECK(bytes[0] == 'R');
    CHECK(bytes[1] == 'T');
    CHECK(bytes[2] == 'T');
    CHECK(bytes[3] == '1');

    TerrainTile loaded;
    REQUIRE(decodeTerrainTile(bytes, loaded, error));
    REQUIRE(loaded.heights.size() == tile.heights.size());
    CHECK(loaded.heights[0] == doctest::Approx(tile.heights[0]));
    CHECK(loaded.holes[terrainSampleIndex(loaded, 3, 3)] == 1);

    float height = 0;
    const float cell = terrainCellSize(tile);
    REQUIRE(terrainSampleHeight(tile, 32.f * cell, 32.f * cell, height));
    CHECK(height > 1.f);
    CHECK_FALSE(terrainSampleHeight(tile, 3.5f * cell, 3.5f * cell, height));
}

TEST_CASE("terrain mesh lods skip holes")
{
    const TerrainTile tile = makeHillTerrain(true);
    MeshGeometry geometry;
    terrainBuildMesh(tile, geometry);
    REQUIRE_FALSE(geometry.triangles.empty());
    REQUIRE(geometry.extraLods.size() == 2);
    CHECK(geometry.extraLods[0].indices.size() / 3 < geometry.triangles.size());
    CHECK_FALSE(geometry.positions.empty());
    CHECK(geometry.root >= 0);
}

TEST_CASE("terrain lods reach the tile edge")
{
    const TerrainTile tile = makeHillTerrain(false);
    CHECK(tile.sampleCount == 65);
    MeshGeometry geometry;
    terrainBuildMesh(tile, geometry);
    auto maxXZ = [](const MeshGeometry &mesh) {
        double maxX = 0;
        double maxZ = 0;
        for (const MeshTri &triangle : mesh.triangles)
        {
            for (int corner = 0; corner < 3; ++corner)
            {
                maxX = std::max(maxX, triangle.position[corner].x);
                maxZ = std::max(maxZ, triangle.position[corner].z);
            }
        }
        return std::pair<double, double>{maxX, maxZ};
    };
    const auto lod0 = maxXZ(geometry);
    CHECK(lod0.first == doctest::Approx(tile.tileSize).epsilon(1e-4));
    CHECK(lod0.second == doctest::Approx(tile.tileSize).epsilon(1e-4));
    terrainApplyLod(geometry, 1);
    const auto lod1 = maxXZ(geometry);
    CHECK(lod1.first == doctest::Approx(tile.tileSize).epsilon(1e-4));
    CHECK(lod1.second == doctest::Approx(tile.tileSize).epsilon(1e-4));
    terrainApplyLod(geometry, 2);
    const auto lod2 = maxXZ(geometry);
    CHECK(lod2.first == doctest::Approx(tile.tileSize).epsilon(1e-4));
    CHECK(lod2.second == doctest::Approx(tile.tileSize).epsilon(1e-4));
}

TEST_CASE("map and liquid round-trip in the scene file")
{
    Scene scene;
    scene.map().id = 9;
    scene.map().name = "Wilds";
    scene.map().kind = MapKind::Dungeon;
    scene.map().tileSize = 64;
    scene.map().tilesX = 2;
    scene.map().tilesZ = 3;
    LiquidVolume water;
    water.kind = LiquidKind::Water;
    water.boundsMin = Vec3(-4, -2, -4);
    water.boundsMax = Vec3(4, 1, 4);
    water.surfaceY = 0.25f;
    scene.liquids().push_back(water);

    const TerrainTile tile = makeHillTerrain(false);
    const auto rtt = std::filesystem::temp_directory_path() / "raytracer-hill.rtt";
    std::string error;
    REQUIRE(writeTerrainFile(rtt, tile, error));
    Object *ground = scene.addTerrain();
    REQUIRE(ground->loadTerrain(rtt, error));
    ground->name() = "Hill";
    ground->setTag("solid");
    ground->setPosition(Vec3(0, 0, 0));

    const auto path = std::filesystem::temp_directory_path() / "raytracer-terrain-scene.txt";
    CameraSetup camera;
    REQUIRE(saveScene(path, scene, camera, error));
    Scene loaded;
    REQUIRE(loadScene(path, loaded, camera, error));
    CHECK(loaded.map().id == 9);
    CHECK(loaded.map().name == "Wilds");
    CHECK(loaded.map().kind == MapKind::Dungeon);
    CHECK(loaded.map().tilesX == 2);
    REQUIRE(loaded.liquids().size() == 1);
    CHECK(loaded.liquids()[0].surfaceY == doctest::Approx(0.25f));
    Object *loadedTerrain = nullptr;
    for (Object *object : loaded.objects())
    {
        if (object->isTerrain())
            loadedTerrain = object;
    }
    REQUIRE(loadedTerrain != nullptr);
    REQUIRE(loadedTerrain->terrainTile() != nullptr);
    CHECK(loadedTerrain->terrainTile()->sampleCount == kTerrainSampleCount);
    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(rtt, ec);
}

TEST_CASE("jolt capsule stands on a heightfield and falls through a hole")
{
    REQUIRE(jobs::ready());
    const float dt = 1.f / 60.f;
    const float cell = terrainCellSize(makeHillTerrain(false));

    SUBCASE("stand on the hill")
    {
        const TerrainTile tile = makeHillTerrain(false);
        jolt_play::World world;
        REQUIRE(world.valid());
        world.addStaticHeightField(tile);
        const float x = 32.f * cell;
        const float z = 32.f * cell;
        float height = 0;
        REQUIRE(terrainSampleHeight(tile, x, z, height));
        world.spawnCapsule(Vec3(x, height + 2.f, z));
        for (int i = 0; i < 180; ++i)
            world.step(dt);
        CHECK(world.grounded());
        CHECK(world.capsuleFeet().y == doctest::Approx(height).epsilon(0.2));
    }

    SUBCASE("fall through a hole")
    {
        const TerrainTile tile = makeHillTerrain(true);
        jolt_play::World world;
        REQUIRE(world.valid());
        world.addStaticHeightField(tile);
        world.spawnCapsule(Vec3(3.5f * cell, 2.f, 3.5f * cell));
        for (int i = 0; i < 180; ++i)
            world.step(dt);
        CHECK(world.capsuleFeet().y < -1.0);
    }
}

TEST_CASE("liquidContainsXZ")
{
    LiquidVolume volume;
    volume.boundsMin = Vec3(-1, -4, -1);
    volume.boundsMax = Vec3(1, 0, 1);
    volume.surfaceY = -0.5f;
    CHECK(liquidContainsXZ(volume, 0.f, 0.f));
    CHECK_FALSE(liquidContainsXZ(volume, 2.f, 0.f));
}
