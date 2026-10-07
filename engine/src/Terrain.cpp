#include "Terrain.hpp"

#include "Content.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>

namespace
{

void appendU32(std::vector<std::uint8_t> &out, std::uint32_t value)
{
    out.push_back(static_cast<std::uint8_t>(value));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value >> 16));
    out.push_back(static_cast<std::uint8_t>(value >> 24));
}

void appendI32(std::vector<std::uint8_t> &out, std::int32_t value)
{
    appendU32(out, static_cast<std::uint32_t>(value));
}

void appendF32(std::vector<std::uint8_t> &out, float value)
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    appendU32(out, bits);
}

bool readU32(const std::uint8_t *&cursor, const std::uint8_t *end, std::uint32_t &value)
{
    if (end - cursor < 4)
        return false;
    value = static_cast<std::uint32_t>(cursor[0]) | (static_cast<std::uint32_t>(cursor[1]) << 8)
        | (static_cast<std::uint32_t>(cursor[2]) << 16) | (static_cast<std::uint32_t>(cursor[3]) << 24);
    cursor += 4;
    return true;
}

bool readI32(const std::uint8_t *&cursor, const std::uint8_t *end, std::int32_t &value)
{
    std::uint32_t bits = 0;
    if (!readU32(cursor, end, bits))
        return false;
    value = static_cast<std::int32_t>(bits);
    return true;
}

bool readF32(const std::uint8_t *&cursor, const std::uint8_t *end, float &value)
{
    std::uint32_t bits = 0;
    if (!readU32(cursor, end, bits))
        return false;
    std::memcpy(&value, &bits, sizeof(bits));
    return true;
}

std::string pathUtf8(const std::filesystem::path &path)
{
    const std::u8string bytes = path.u8string();
    return std::string(bytes.begin(), bytes.end());
}

bool validCount(std::uint32_t sampleCount)
{
    // Chunked LOD and Jolt heightfields want 2^n + 1 samples (5..257).
    if (sampleCount < 5 || sampleCount > 257)
        return false;
    const std::uint32_t span = sampleCount - 1;
    return span > 0 && (span & (span - 1)) == 0;
}

bool sampleHole(const TerrainTile &tile, std::uint32_t x, std::uint32_t z)
{
    if (tile.holes.empty())
        return false;
    return tile.holes[terrainSampleIndex(tile, x, z)] != 0;
}

void addLod(const TerrainTile &tile, std::uint32_t stride, std::vector<MeshTri> &triangles)
{
    const std::uint32_t n = tile.sampleCount;
    if (n < 2 || stride == 0 || stride >= n)
        return;
    const float cell = terrainCellSize(tile);
    const float skirtDrop = std::max(cell * 2.f, tile.tileSize * 0.05f);
    auto heightAt = [&](std::uint32_t x, std::uint32_t z) {
        return tile.heights[terrainSampleIndex(tile, x, z)];
    };
    auto pushTri = [&](const Vec3 &p0, const Vec3 &p1, const Vec3 &p2, float u0, float v0, float u1, float v1, float u2,
        float v2) {
        MeshTri triangle;
        triangle.position[0] = p0;
        triangle.position[1] = p1;
        triangle.position[2] = p2;
        Vec3 normal = cross(p1 - p0, p2 - p0);
        const double len = length(normal);
        if (len > 1e-8)
            normal = normal / len;
        else
            normal = Vec3(0, 1, 0);
        triangle.normal[0] = triangle.normal[1] = triangle.normal[2] = normal;
        triangle.u[0] = u0;
        triangle.v[0] = v0;
        triangle.u[1] = u1;
        triangle.v[1] = v1;
        triangle.u[2] = u2;
        triangle.v[2] = v2;
        triangles.push_back(triangle);
    };
    auto pushGridTri = [&](std::uint32_t x0, std::uint32_t z0, std::uint32_t x1, std::uint32_t z1, std::uint32_t x2,
        std::uint32_t z2) {
        const float inv = n > 1 ? 1.f / static_cast<float>(n - 1) : 1.f;
        pushTri(Vec3(x0 * cell, heightAt(x0, z0), z0 * cell), Vec3(x1 * cell, heightAt(x1, z1), z1 * cell),
            Vec3(x2 * cell, heightAt(x2, z2), z2 * cell), static_cast<float>(x0) * inv, static_cast<float>(z0) * inv,
            static_cast<float>(x1) * inv, static_cast<float>(z1) * inv, static_cast<float>(x2) * inv,
            static_cast<float>(z2) * inv);
    };

    // With sampleCount = 2^k + 1, every stride lands on the last sample.
    for (std::uint32_t z = 0; z + stride < n; z += stride)
    {
        for (std::uint32_t x = 0; x + stride < n; x += stride)
        {
            const std::uint32_t x1 = x + stride;
            const std::uint32_t z1 = z + stride;
            if (sampleHole(tile, x, z) || sampleHole(tile, x1, z) || sampleHole(tile, x, z1) || sampleHole(tile, x1, z1))
                continue;
            pushGridTri(x, z, x1, z, x1, z1);
            pushGridTri(x, z, x1, z1, x, z1);
        }
    }

    // Vertical skirts hide cracks when a neighbour tile uses a different LOD.
    auto pushSkirt = [&](std::uint32_t ax, std::uint32_t az, std::uint32_t bx, std::uint32_t bz) {
        if (sampleHole(tile, ax, az) || sampleHole(tile, bx, bz))
            return;
        const float inv = n > 1 ? 1.f / static_cast<float>(n - 1) : 1.f;
        const Vec3 a(ax * cell, heightAt(ax, az), az * cell);
        const Vec3 b(bx * cell, heightAt(bx, bz), bz * cell);
        const Vec3 aDown(a.x, a.y - skirtDrop, a.z);
        const Vec3 bDown(b.x, b.y - skirtDrop, b.z);
        const float ua = static_cast<float>(ax) * inv;
        const float va = static_cast<float>(az) * inv;
        const float ub = static_cast<float>(bx) * inv;
        const float vb = static_cast<float>(bz) * inv;
        pushTri(a, b, bDown, ua, va, ub, vb, ub, vb);
        pushTri(a, bDown, aDown, ua, va, ub, vb, ua, va);
    };
    for (std::uint32_t x = 0; x + stride < n; x += stride)
    {
        pushSkirt(x, 0, x + stride, 0);
        pushSkirt(x, n - 1, x + stride, n - 1);
    }
    for (std::uint32_t z = 0; z + stride < n; z += stride)
    {
        pushSkirt(0, z, 0, z + stride);
        pushSkirt(n - 1, z, n - 1, z + stride);
    }
}

} // namespace

bool isRttPath(const std::filesystem::path &path)
{
    std::string ext = path.extension().string();
    for (char &c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".rtt";
}

bool encodeTerrainTile(const TerrainTile &tile, std::vector<std::uint8_t> &out, std::string &error)
{
    if (!validCount(tile.sampleCount) || tile.heights.size() != tile.sampleCount * tile.sampleCount)
    {
        error = "invalid terrain samples";
        return false;
    }
    const bool hasHoles = !tile.holes.empty();
    if (hasHoles && tile.holes.size() != tile.heights.size())
    {
        error = "terrain hole map size";
        return false;
    }
    const bool hasSplat = !tile.splat.empty();
    if (hasSplat && tile.splat.size() != tile.heights.size() * static_cast<std::size_t>(kTerrainSplatLayers))
    {
        error = "terrain splat size";
        return false;
    }
    out.clear();
    out.insert(out.end(), kTerrainMagic, kTerrainMagic + 4);
    appendU32(out, kTerrainPayloadVersion);
    appendU32(out, tile.sampleCount);
    appendF32(out, tile.tileSize);
    appendI32(out, tile.tileX);
    appendI32(out, tile.tileZ);
    std::uint32_t flags = 0;
    if (hasHoles)
        flags |= 1u;
    if (hasSplat)
        flags |= 2u;
    appendU32(out, flags);
    for (float h : tile.heights)
        appendF32(out, h);
    if (hasHoles)
        out.insert(out.end(), tile.holes.begin(), tile.holes.end());
    if (hasSplat)
        out.insert(out.end(), tile.splat.begin(), tile.splat.end());
    return true;
}

bool decodeTerrainTile(const std::vector<std::uint8_t> &bytes, TerrainTile &tile, std::string &error)
{
    if (bytes.size() < 28 || std::memcmp(bytes.data(), kTerrainMagic, 4) != 0)
    {
        error = "not an RTT1 terrain tile";
        return false;
    }
    const std::uint8_t *cursor = bytes.data() + 4;
    const std::uint8_t *end = bytes.data() + bytes.size();
    std::uint32_t version = 0;
    if (!readU32(cursor, end, version) || version != kTerrainPayloadVersion)
    {
        error = "unsupported RTT payload";
        return false;
    }
    TerrainTile loaded;
    if (!readU32(cursor, end, loaded.sampleCount) || !readF32(cursor, end, loaded.tileSize)
        || !readI32(cursor, end, loaded.tileX) || !readI32(cursor, end, loaded.tileZ))
    {
        error = "truncated RTT header";
        return false;
    }
    std::uint32_t flags = 0;
    if (!readU32(cursor, end, flags) || !validCount(loaded.sampleCount) || loaded.tileSize <= 0.f)
    {
        error = "invalid RTT header";
        return false;
    }
    const std::size_t samples = static_cast<std::size_t>(loaded.sampleCount) * loaded.sampleCount;
    loaded.heights.resize(samples);
    for (std::size_t i = 0; i < samples; ++i)
        if (!readF32(cursor, end, loaded.heights[i]))
        {
            error = "truncated RTT heights";
            return false;
        }
    if (flags & 1u)
    {
        if (static_cast<std::size_t>(end - cursor) < samples)
        {
            error = "truncated RTT holes";
            return false;
        }
        loaded.holes.assign(cursor, cursor + samples);
        cursor += samples;
    }
    if (flags & 2u)
    {
        const std::size_t splatBytes = samples * static_cast<std::size_t>(kTerrainSplatLayers);
        if (static_cast<std::size_t>(end - cursor) < splatBytes)
        {
            error = "truncated RTT splat";
            return false;
        }
        loaded.splat.assign(cursor, cursor + splatBytes);
    }
    tile = std::move(loaded);
    return true;
}

bool writeTerrainFile(const std::filesystem::path &path, const TerrainTile &tile, std::string &error)
{
    std::vector<std::uint8_t> bytes;
    if (!encodeTerrainTile(tile, bytes, error))
        return false;
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    if (!out)
    {
        error = "could not write " + pathUtf8(path);
        return false;
    }
    out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

std::shared_ptr<TerrainTile> loadTerrainFile(const std::filesystem::path &path, std::string &error)
{
    std::vector<std::uint8_t> bytes;
    const std::string virtualPath = path.generic_string();
    if (contentExists(virtualPath))
    {
        if (!contentRead(virtualPath, bytes, error))
            return nullptr;
    }
    else
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
        {
            error = "could not open " + pathUtf8(path);
            return nullptr;
        }
        bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    auto tile = std::make_shared<TerrainTile>();
    if (!decodeTerrainTile(bytes, *tile, error))
        return nullptr;
    tile->sourcePath = pathUtf8(path);
    return tile;
}

TerrainTile makeHillTerrain(bool holeNearOrigin)
{
    TerrainTile tile;
    tile.sampleCount = kTerrainSampleCount;
    tile.tileSize = kTerrainTileSize;
    const std::size_t n = tile.sampleCount;
    tile.heights.resize(n * n);
    tile.holes.assign(n * n, 0);
    tile.splat.assign(n * n * static_cast<std::size_t>(kTerrainSplatLayers), 0);
    const float inv = 1.f / static_cast<float>(n - 1);
    for (std::uint32_t z = 0; z < tile.sampleCount; ++z)
    {
        for (std::uint32_t x = 0; x < tile.sampleCount; ++x)
        {
            const float fx = static_cast<float>(x) * inv - 0.5f;
            const float fz = static_cast<float>(z) * inv - 0.5f;
            const float hill = 2.f * std::exp(-(fx * fx + fz * fz) * 8.f);
            const std::size_t i = terrainSampleIndex(tile, x, z);
            tile.heights[i] = hill;
            tile.splat[i * 4] = 255;
            tile.splat[i * 4 + 1] = static_cast<std::uint8_t>(std::min(255.f, hill * 80.f));
        }
    }
    if (holeNearOrigin)
    {
        for (std::uint32_t z = 2; z < 6; ++z)
            for (std::uint32_t x = 2; x < 6; ++x)
                tile.holes[terrainSampleIndex(tile, x, z)] = 1;
    }
    return tile;
}

void terrainBuildMesh(const TerrainTile &tile, MeshGeometry &geometry)
{
    geometry = MeshGeometry();
    std::vector<MeshTri> lod0;
    addLod(tile, 1, lod0);
    meshAssignTriangles(geometry, std::move(lod0));
    std::vector<MeshTri> lod2;
    addLod(tile, 2, lod2);
    if (!lod2.empty())
        geometry.extraLods.push_back(meshLodFromTriangles(lod2));
    std::vector<MeshTri> lod4;
    addLod(tile, 4, lod4);
    if (!lod4.empty())
        geometry.extraLods.push_back(meshLodFromTriangles(lod4));
}

void terrainBuildMeshLod(const TerrainTile &tile, int lod, MeshGeometry &geometry)
{
    geometry = MeshGeometry();
    const int clamped = std::max(0, std::min(2, lod));
    const int stride = 1 << clamped;
    std::vector<MeshTri> tris;
    addLod(tile, stride, tris);
    meshAssignTriangles(geometry, std::move(tris));
}

void terrainApplyLod(MeshGeometry &geometry, int lod)
{
    if (lod <= 0 || geometry.extraLods.empty())
        return;
    const std::size_t index = static_cast<std::size_t>(std::min(lod, static_cast<int>(geometry.extraLods.size())) - 1);
    const MeshLod &level = geometry.extraLods[index];
    if (level.indices.empty())
        return;
    if (!level.positions.empty())
    {
        geometry.positions = level.positions;
        geometry.normals = level.normals;
        geometry.u = level.u;
        geometry.v = level.v;
    }
    geometry.indices = level.indices;
    finishGeometry(geometry);
}

bool terrainSampleHeight(const TerrainTile &tile, float localX, float localZ, float &height)
{
    if (tile.heights.empty() || tile.sampleCount < 2)
        return false;
    const float cell = terrainCellSize(tile);
    if (cell <= 0.f)
        return false;
    const float max = tile.tileSize;
    if (localX < 0.f || localZ < 0.f || localX > max || localZ > max)
        return false;
    const float gx = localX / cell;
    const float gz = localZ / cell;
    const auto x0 = static_cast<std::uint32_t>(gx);
    const auto z0 = static_cast<std::uint32_t>(gz);
    const std::uint32_t x1 = std::min(x0 + 1, tile.sampleCount - 1);
    const std::uint32_t z1 = std::min(z0 + 1, tile.sampleCount - 1);
    if (sampleHole(tile, x0, z0) || sampleHole(tile, x1, z0) || sampleHole(tile, x0, z1) || sampleHole(tile, x1, z1))
        return false;
    const float tx = gx - static_cast<float>(x0);
    const float tz = gz - static_cast<float>(z0);
    const float h00 = tile.heights[terrainSampleIndex(tile, x0, z0)];
    const float h10 = tile.heights[terrainSampleIndex(tile, x1, z0)];
    const float h01 = tile.heights[terrainSampleIndex(tile, x0, z1)];
    const float h11 = tile.heights[terrainSampleIndex(tile, x1, z1)];
    height = (h00 * (1.f - tx) + h10 * tx) * (1.f - tz) + (h01 * (1.f - tx) + h11 * tx) * tz;
    return true;
}

bool liquidContainsXZ(const LiquidVolume &volume, float x, float z)
{
    return x >= static_cast<float>(volume.boundsMin.x) && x <= static_cast<float>(volume.boundsMax.x)
        && z >= static_cast<float>(volume.boundsMin.z) && z <= static_cast<float>(volume.boundsMax.z);
}

const char *mapKindName(MapKind kind)
{
    return kind == MapKind::Dungeon ? "dungeon" : "continent";
}

bool mapKindFromName(const std::string &name, MapKind &kind)
{
    if (name == "dungeon")
    {
        kind = MapKind::Dungeon;
        return true;
    }
    if (name == "continent")
    {
        kind = MapKind::Continent;
        return true;
    }
    return false;
}

const char *liquidKindName(LiquidKind kind)
{
    return kind == LiquidKind::Lava ? "lava" : "water";
}

bool liquidKindFromName(const std::string &name, LiquidKind &kind)
{
    if (name == "lava")
    {
        kind = LiquidKind::Lava;
        return true;
    }
    if (name == "water")
    {
        kind = LiquidKind::Water;
        return true;
    }
    return false;
}
