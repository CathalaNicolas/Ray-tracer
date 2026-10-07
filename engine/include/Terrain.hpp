#pragma once

#include "MeshGeometry.hpp"
#include "Vec3.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

inline constexpr char kTerrainMagic[4] = {'R', 'T', 'T', '1'};
inline constexpr std::uint32_t kTerrainPayloadVersion = 1;
// 2^n + 1 so every LOD stride lands on the far edge (Jolt heightfield block size 2).
inline constexpr std::uint32_t kTerrainSampleCount = 65;
inline constexpr float kTerrainTileSize = 64.f;
inline constexpr int kTerrainSplatLayers = 4;

enum class MapKind : std::uint8_t
{
    Continent = 0,
    Dungeon = 1,
};

enum class LiquidKind : std::uint8_t
{
    Water = 0,
    Lava = 1,
};

struct MapDef
{
    std::uint64_t id = 1;
    std::string name = "Demo";
    MapKind kind = MapKind::Continent;
    float tileSize = kTerrainTileSize;
    int tilesX = 1;
    int tilesZ = 1;
};

struct LiquidVolume
{
    Vec3 boundsMin{-1, -1, -1};
    Vec3 boundsMax{1, 1, 1};
    float surfaceY = 0;
    LiquidKind kind = LiquidKind::Water;
};

struct TerrainTile
{
    std::uint32_t sampleCount = kTerrainSampleCount;
    float tileSize = kTerrainTileSize;
    int tileX = 0;
    int tileZ = 0;
    std::vector<float> heights;
    std::vector<std::uint8_t> holes;
    std::vector<std::uint8_t> splat;
    std::string sourcePath;
};

inline std::size_t terrainSampleIndex(const TerrainTile &tile, std::uint32_t x, std::uint32_t z)
{
    return static_cast<std::size_t>(z) * tile.sampleCount + x;
}

inline float terrainCellSize(const TerrainTile &tile)
{
    return tile.sampleCount > 1 ? tile.tileSize / static_cast<float>(tile.sampleCount - 1) : tile.tileSize;
}

bool isRttPath(const std::filesystem::path &path);
bool encodeTerrainTile(const TerrainTile &tile, std::vector<std::uint8_t> &out, std::string &error);
bool decodeTerrainTile(const std::vector<std::uint8_t> &bytes, TerrainTile &tile, std::string &error);
bool writeTerrainFile(const std::filesystem::path &path, const TerrainTile &tile, std::string &error);
std::shared_ptr<TerrainTile> loadTerrainFile(const std::filesystem::path &path, std::string &error);

TerrainTile makeHillTerrain(bool holeNearOrigin);
void terrainBuildMesh(const TerrainTile &tile, MeshGeometry &geometry);
// Build only one LOD into geometry (lod 0/1/2 → stride 1/2/4). No extraLods.
void terrainBuildMeshLod(const TerrainTile &tile, int lod, MeshGeometry &geometry);
void terrainApplyLod(MeshGeometry &geometry, int lod);
bool terrainSampleHeight(const TerrainTile &tile, float localX, float localZ, float &height);
bool liquidContainsXZ(const LiquidVolume &volume, float x, float z);
const char *mapKindName(MapKind kind);
bool mapKindFromName(const std::string &name, MapKind &kind);
const char *liquidKindName(LiquidKind kind);
bool liquidKindFromName(const std::string &name, LiquidKind &kind);
