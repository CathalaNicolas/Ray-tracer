#include "Content.hpp"
#include "Jobs.hpp"
#include "Play.hpp"
#include "Scene.hpp"
#include "SimSerialize.hpp"
#include "Stream.hpp"
#include "Terrain.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <string>

namespace
{

std::filesystem::path writeGrid(const std::filesystem::path &root, int tilesX, int tilesZ)
{
    const auto cooked = root / "cooked";
    std::filesystem::create_directories(cooked);
    std::string error;
    for (int z = 0; z < tilesZ; ++z)
    {
        for (int x = 0; x < tilesX; ++x)
        {
            TerrainTile tile = makeHillTerrain(false);
            tile.tileX = x;
            tile.tileZ = z;
            REQUIRE(writeTerrainFile(cooked / ("t" + std::to_string(x) + "_" + std::to_string(z) + ".rtt"), tile, error));
        }
    }
    return cooked;
}

} // namespace

TEST_CASE("streamTileRadius covers two seconds of 3x walk")
{
    CHECK(streamTileRadius(64.f, 4.0) == 1);
    CHECK(streamTileRadius(16.f, 4.0) >= 2);
}

TEST_CASE("sim stream loads collision tiles without render meshes")
{
    const auto root = std::filesystem::temp_directory_path() / "raytracer-stream-sim";
    writeGrid(root, 2, 2);
    REQUIRE(contentInit(root));

    MapDef map;
    map.tileSize = kTerrainTileSize;
    map.tilesX = 2;
    map.tilesZ = 2;
    TileStream stream;
    stream.configure(map, StreamLayer::Sim);
    stream.setFocus(0, 0);
    Scene scene;
    stream.waitIdle(scene);
    CHECK(stream.readyCount() >= 1);
    CHECK(stream.hasReady(0, 0));
    CHECK(stream.readyMesh(0, 0) == nullptr);
    CHECK(stream.entityId(0, 0) == terrainTileEntityId(0, 0));
    Object *object = scene.find(stream.entityId(0, 0));
    REQUIRE(object != nullptr);
    CHECK(object->isTerrain());
    CHECK(object->id() == terrainTileEntityId(0, 0));
    CHECK(object->triangles().empty());
    contentInit(".");
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("streamed tile entity ids come from coordinates")
{
    const auto root = std::filesystem::temp_directory_path() / "raytracer-stream-ids";
    writeGrid(root, 2, 2);
    REQUIRE(contentInit(root));

    MapDef map;
    map.tileSize = kTerrainTileSize;
    map.tilesX = 2;
    map.tilesZ = 2;
    Scene left;
    Scene right;
    TileStream a;
    TileStream b;
    a.configure(map, StreamLayer::Sim);
    b.configure(map, StreamLayer::Sim);
    a.setFocus(0, 0);
    b.setFocus(0, 0);
    a.waitIdle(left);
    b.waitIdle(right);
    CHECK(a.entityId(0, 0) == b.entityId(0, 0));
    CHECK(a.entityId(1, 0) == terrainTileEntityId(1, 0));
    CHECK(left.find(terrainTileEntityId(0, 0)) != nullptr);
    CHECK(right.find(terrainTileEntityId(0, 0)) != nullptr);
    contentInit(".");
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("render stream picks a farther LOD and unloads by budget")
{
    const auto root = std::filesystem::temp_directory_path() / "raytracer-stream-render";
    writeGrid(root, 2, 2);
    REQUIRE(contentInit(root));

    MapDef map;
    map.tileSize = 16.f;
    map.tilesX = 2;
    map.tilesZ = 2;
    TileStream stream;
    stream.configure(map, StreamLayer::Render);
    StreamBudget budget;
    budget.maxTiles = 4;
    budget.maxBytes = 32u << 20;
    stream.setBudget(budget);
    stream.setFocus(0, 0);
    Scene scene;
    stream.waitIdle(scene);
    REQUIRE(stream.hasReady(0, 0));
    CHECK(stream.readyLod(0, 0) == 0);
    REQUIRE(stream.readyMesh(0, 0) != nullptr);
    CHECK_FALSE(stream.readyMesh(0, 0)->triangles.empty());
    CHECK(stream.readyMesh(0, 0)->extraLods.empty());

    if (stream.hasReady(1, 1))
    {
        CHECK(stream.readyLod(1, 1) >= 1);
        REQUIRE(stream.readyMesh(1, 1) != nullptr);
        CHECK(stream.readyMesh(1, 1)->extraLods.empty());
    }

    budget.maxTiles = 1;
    stream.setBudget(budget);
    stream.setFocus(16.0, 16.0);
    stream.waitIdle(scene);
    CHECK(stream.readyCount() == 1);
    CHECK(stream.hasReady(1, 1));
    CHECK_FALSE(stream.hasReady(0, 0));
    contentInit(".");
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("play ensures the sim tile stream before the tick")
{
    const auto root = std::filesystem::temp_directory_path() / "raytracer-stream-play";
    writeGrid(root, 1, 1);
    REQUIRE(contentInit(root));

    Scene scene;
    scene.map().tilesX = 1;
    scene.map().tilesZ = 1;
    Material paint = Material::makeDiffuse(Vec3(0.5, 0.5, 0.5));
    Object *player = scene.addSphere(Vec3(8, 2, 8), 0.35, paint);
    player->setTag("player");

    PlayState state;
    seedPlayRng(state);
    state.stream = std::make_unique<TileStream>();
    state.stream->configure(scene.map(), StreamLayer::Sim);
    SimSession sim;
    sim.attach(scene, state);
    sim.begin();
    sim.step(PlayInput{}, Vec3(0, 0, -1));
    CHECK(state.stream->readyCount() >= 1);
    CHECK(scene.find(terrainTileEntityId(0, 0)) != nullptr);
    contentInit(".");
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("sim play blob restores streamed terrain by tile coordinates")
{
    const auto root = std::filesystem::temp_directory_path() / "raytracer-stream-save";
    writeGrid(root, 1, 1);
    REQUIRE(contentInit(root));

    Scene scene;
    scene.map().tilesX = 1;
    scene.map().tilesZ = 1;
    Material paint = Material::makeDiffuse(Vec3(0.5, 0.5, 0.5));
    Object *player = scene.addSphere(Vec3(8, 2, 8), 0.35, paint);
    player->setTag("player");

    PlayState state;
    seedPlayRng(state);
    state.stream = std::make_unique<TileStream>();
    state.stream->configure(scene.map(), StreamLayer::Sim);
    state.stream->setFocus(8, 8);
    state.stream->ensureReady(scene);
    REQUIRE(scene.find(terrainTileEntityId(0, 0)) != nullptr);

    const SimPlayBlob blob = captureSimPlay(scene, state, 0, Vec3(0, 0, -1));
    bool foundTerrain = false;
    for (const SimEntityRecord &record : blob.entities)
    {
        if (record.shape == SimShapeKind::Terrain)
        {
            foundTerrain = true;
            CHECK(record.id == terrainTileEntityId(0, 0));
            CHECK(record.tileX == 0);
            CHECK(record.tileZ == 0);
        }
    }
    REQUIRE(foundTerrain);

    Scene dest;
    dest.map() = scene.map();
    PlayState destState;
    seedPlayRng(destState);
    REQUIRE(applySimPlay(dest, destState, blob));
    Object *tile = dest.find(terrainTileEntityId(0, 0));
    REQUIRE(tile != nullptr);
    CHECK(tile->isTerrain());
    contentInit(".");
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}
