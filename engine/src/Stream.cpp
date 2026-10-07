#include "Stream.hpp"

#include "Content.hpp"
#include "EngineSettings.hpp"
#include "Jobs.hpp"
#include "Object.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>

namespace
{

int clampi(int value, int lo, int hi)
{
    return std::max(lo, std::min(hi, value));
}

} // namespace

int streamTileRadius(float tileSize, double moveSpeed)
{
    if (tileSize <= 0.f)
        return 1;
    const double ahead = std::max(static_cast<double>(tileSize), moveSpeed * kStreamMountScale * kStreamLookaheadSeconds);
    return std::max(1, static_cast<int>(std::ceil(ahead / static_cast<double>(tileSize) - 1e-9)));
}

std::string streamTileVirtualPath(int tileX, int tileZ)
{
    return "cooked/t" + std::to_string(tileX) + "_" + std::to_string(tileZ) + ".rtt";
}

TileStream::Key TileStream::pack(int x, int z)
{
    return (static_cast<Key>(static_cast<std::uint32_t>(x)) << 32) | static_cast<std::uint32_t>(z);
}

int TileStream::loadRadius() const
{
    return streamTileRadius(map_.tileSize, engineSettings().moveSpeed);
}

int TileStream::unloadRadius() const
{
    return loadRadius() + 1;
}

int TileStream::chebyshev(int x, int z) const
{
    return std::max(std::abs(x - focusTileX_), std::abs(z - focusTileZ_));
}

std::uint32_t TileStream::workerPin() const
{
    if (!jobs::ready())
        return 0;
    if (jobs::threadCount() > 2)
        return 2u;
    if (jobs::threadCount() > 1)
        return 1u;
    return 0u;
}

void TileStream::configure(const MapDef &map, StreamLayer layer)
{
    map_ = map;
    layer_ = layer;
    if (map_.tilesX < 1)
        map_.tilesX = 1;
    if (map_.tilesZ < 1)
        map_.tilesZ = 1;
    if (map_.tileSize <= 0.f)
        map_.tileSize = kTerrainTileSize;
    slots_.clear();
}

void TileStream::setBudget(const StreamBudget &budget)
{
    budget_ = budget;
    if (budget_.maxTiles < 1)
        budget_.maxTiles = 1;
}

void TileStream::setApplyBudgetMs(double ms)
{
    applyBudgetMs_ = ms < 0.0 ? 0.0 : ms;
}

void TileStream::setPathFn(std::function<std::string(int, int)> fn)
{
    pathFn_ = std::move(fn);
}

void TileStream::setFocus(double worldX, double worldZ)
{
    focusX_ = worldX;
    focusZ_ = worldZ;
    if (map_.tileSize <= 0.f)
        return;
    focusTileX_ = static_cast<int>(std::floor(worldX / map_.tileSize));
    focusTileZ_ = static_cast<int>(std::floor(worldZ / map_.tileSize));
    focusTileX_ = clampi(focusTileX_, 0, map_.tilesX - 1);
    focusTileZ_ = clampi(focusTileZ_, 0, map_.tilesZ - 1);
}

void TileStream::desiredTiles(std::vector<std::pair<int, int>> &out) const
{
    out.clear();
    const int radius = loadRadius();
    for (int z = focusTileZ_ - radius; z <= focusTileZ_ + radius; ++z)
    {
        for (int x = focusTileX_ - radius; x <= focusTileX_ + radius; ++x)
        {
            if (x < 0 || z < 0 || x >= map_.tilesX || z >= map_.tilesZ)
                continue;
            out.push_back({x, z});
        }
    }
    std::sort(out.begin(), out.end(), [&](const auto &a, const auto &b) {
        const int da = std::max(std::abs(a.first - focusTileX_), std::abs(a.second - focusTileZ_));
        const int db = std::max(std::abs(b.first - focusTileX_), std::abs(b.second - focusTileZ_));
        if (da != db)
            return da < db;
        if (a.second != b.second)
            return a.second < b.second;
        return a.first < b.first;
    });
}

void TileStream::refreshBytes(Slot &slot)
{
    slot.bytesUsed = 0;
    if (slot.tile)
    {
        slot.bytesUsed += slot.tile->heights.size() * sizeof(float);
        slot.bytesUsed += slot.tile->holes.size();
        slot.bytesUsed += slot.tile->splat.size();
    }
    if (slot.mesh)
    {
        slot.bytesUsed += slot.mesh->positions.size() * sizeof(Vec3) * 2
            + slot.mesh->indices.size() * sizeof(std::uint32_t);
    }
}

void TileStream::startLoad(int x, int z)
{
    const Key key = pack(x, z);
    if (slots_.contains(key))
        return;
    auto slot = std::make_unique<Slot>();
    slot->x = x;
    slot->z = z;
    slot->state = Slot::State::Loading;
    slot->entity = terrainTileEntityId(x, z);
    const std::string path = pathFn_ ? pathFn_(x, z) : streamTileVirtualPath(x, z);
    auto *raw = slot.get();
    auto read = [raw, path]() {
        std::string error;
        if (!contentRead(path, raw->bytes, error))
            raw->error = error;
    };
    if (jobs::ready())
    {
        const std::uint32_t pin = jobs::threadCount() > 1 ? 1u : 0u;
        slot->io = jobs::schedulePinned(std::move(read), pin);
    }
    else
    {
        read();
        slot->ioDone = true;
    }
    slots_[key] = std::move(slot);
}

void TileStream::startBuild(Slot &slot, bool remeshOnly)
{
    slot.state = Slot::State::Building;
    slot.buildDone = false;
    slot.remeshOnly = remeshOnly;
    slot.buildError.clear();
    slot.builtTile.reset();
    slot.builtMesh.reset();
    slot.targetLod = (layer_ == StreamLayer::Render) ? std::min(2, chebyshev(slot.x, slot.z)) : 0;
    slot.builtLod = slot.targetLod;

    auto *raw = &slot;
    const StreamLayer layer = layer_;
    const std::string path = pathFn_ ? pathFn_(slot.x, slot.z) : streamTileVirtualPath(slot.x, slot.z);
    const int tileX = slot.x;
    const int tileZ = slot.z;
    const int lod = slot.targetLod;
    const bool remesh = remeshOnly;
    const std::shared_ptr<TerrainTile> remeshTile = remeshOnly ? slot.tile : nullptr;

    auto work = [raw, layer, path, tileX, tileZ, lod, remesh, remeshTile]() {
        if (remesh)
        {
            if (remeshTile == nullptr)
            {
                raw->buildError = "remesh without tile";
                return;
            }
            if (layer == StreamLayer::Render)
            {
                auto mesh = std::make_shared<MeshGeometry>();
                terrainBuildMeshLod(*remeshTile, lod, *mesh);
                raw->builtMesh = std::move(mesh);
            }
            raw->builtLod = lod;
            return;
        }
        if (!raw->error.empty() || raw->bytes.empty())
        {
            if (raw->buildError.empty())
                raw->buildError = raw->error.empty() ? "empty tile bytes" : raw->error;
            return;
        }
        auto tile = std::make_shared<TerrainTile>();
        std::string decodeError;
        if (!decodeTerrainTile(raw->bytes, *tile, decodeError))
        {
            raw->buildError = decodeError;
            return;
        }
        tile->tileX = tileX;
        tile->tileZ = tileZ;
        tile->sourcePath = path;
        if (layer == StreamLayer::Render)
        {
            auto mesh = std::make_shared<MeshGeometry>();
            terrainBuildMeshLod(*tile, lod, *mesh);
            raw->builtMesh = std::move(mesh);
        }
        raw->builtTile = std::move(tile);
        raw->builtLod = lod;
    };

    if (jobs::ready())
    {
        slot.build = jobs::schedulePinned(std::move(work), workerPin());
    }
    else
    {
        work();
        slot.buildDone = true;
    }
}

void TileStream::pollIo(Slot &slot)
{
    if (slot.state != Slot::State::Loading)
        return;
    if (!slot.ioDone)
    {
        if (!slot.io.complete())
            return;
        slot.ioDone = true;
    }
    startBuild(slot, false);
}

void TileStream::pollBuild(Slot &slot)
{
    if (slot.state != Slot::State::Building)
        return;
    if (!slot.buildDone)
    {
        if (!slot.build.complete())
            return;
        slot.buildDone = true;
    }
    if (!slot.buildError.empty())
    {
        slot.state = Slot::State::Failed;
        slot.bytes.clear();
        slot.bytes.shrink_to_fit();
        slot.builtTile.reset();
        slot.builtMesh.reset();
        return;
    }
    if (slot.remeshOnly)
    {
        if (slot.builtMesh)
            slot.mesh = std::move(slot.builtMesh);
        slot.lod = slot.builtLod;
        slot.sceneApplied = false;
    }
    else
    {
        if (!slot.builtTile)
        {
            slot.state = Slot::State::Failed;
            slot.bytes.clear();
            slot.bytes.shrink_to_fit();
            return;
        }
        slot.tile = std::move(slot.builtTile);
        slot.mesh = std::move(slot.builtMesh);
        slot.lod = slot.builtLod;
        slot.sceneApplied = false;
    }
    slot.bytes.clear();
    slot.bytes.shrink_to_fit();
    refreshBytes(slot);
    slot.state = Slot::State::Ready;
}

void TileStream::scheduleLodRebuild(Slot &slot, int lod)
{
    if (slot.state != Slot::State::Ready || slot.tile == nullptr)
        return;
    if (lod == slot.lod)
        return;
    slot.targetLod = lod;
    startBuild(slot, true);
}

void TileStream::applyScene(Scene &scene, Slot &slot)
{
    if (slot.state != Slot::State::Ready || slot.tile == nullptr)
        return;
    if (slot.entity == kInvalidEntityId)
        slot.entity = terrainTileEntityId(slot.x, slot.z);
    Object *object = scene.find(slot.entity);
    if (object == nullptr)
    {
        object = scene.addTerrain(slot.entity);
        object->name() = "Tile " + std::to_string(slot.x) + " " + std::to_string(slot.z);
        object->setTag("solid");
        object->setPosition(Vec3(static_cast<double>(slot.x) * map_.tileSize, 0,
            static_cast<double>(slot.z) * map_.tileSize));
    }
    object->setTerrainData(slot.tile, slot.mesh);
    slot.sceneApplied = true;
}

void TileStream::drop(Scene &scene, Key key)
{
    auto found = slots_.find(key);
    if (found == slots_.end())
        return;
    if (found->second->entity != kInvalidEntityId)
        scene.remove(found->second->entity);
    slots_.erase(found);
}

std::size_t TileStream::totalBytes() const
{
    std::size_t sum = 0;
    for (const auto &pair : slots_)
        sum += pair.second->bytesUsed;
    return sum;
}

bool TileStream::ringSettled(const std::vector<std::pair<int, int>> &wanted) const
{
    for (const auto &tile : wanted)
    {
        const auto found = slots_.find(pack(tile.first, tile.second));
        if (found == slots_.end())
            return false;
        if (found->second->state == Slot::State::Loading || found->second->state == Slot::State::Building)
            return false;
    }
    return true;
}

void TileStream::pump(Scene &scene)
{
    for (auto &pair : slots_)
    {
        pollIo(*pair.second);
        pollBuild(*pair.second);
    }

    std::vector<std::pair<int, int>> wanted;
    desiredTiles(wanted);
    const int keepRadius = unloadRadius();

    std::vector<Key> extra;
    for (const auto &pair : slots_)
    {
        if (chebyshev(pair.second->x, pair.second->z) > keepRadius)
            extra.push_back(pair.first);
    }
    std::sort(extra.begin(), extra.end(), [&](Key a, Key b) {
        return chebyshev(static_cast<int>(a >> 32), static_cast<int>(a))
            > chebyshev(static_cast<int>(b >> 32), static_cast<int>(b));
    });
    for (Key key : extra)
        drop(scene, key);

    int started = 0;
    for (const auto &tile : wanted)
    {
        if (slots_.contains(pack(tile.first, tile.second)))
            continue;
        if (slots_.size() >= budget_.maxTiles)
            break;
        startLoad(tile.first, tile.second);
        if (++started >= 2)
            break;
    }

    while (slots_.size() > budget_.maxTiles || totalBytes() > budget_.maxBytes)
    {
        Key farthest = 0;
        int best = -1;
        for (const auto &pair : slots_)
        {
            const int d = chebyshev(pair.second->x, pair.second->z);
            if (d > best)
            {
                best = d;
                farthest = pair.first;
            }
        }
        if (best < 0)
            break;
        drop(scene, farthest);
    }

    if (layer_ == StreamLayer::Render)
    {
        for (auto &pair : slots_)
        {
            Slot &slot = *pair.second;
            if (slot.state != Slot::State::Ready || !slot.tile)
                continue;
            const int lod = std::min(2, chebyshev(slot.x, slot.z));
            if (lod != slot.lod)
                scheduleLodRebuild(slot, lod);
        }
    }

    const auto budgetStart = std::chrono::steady_clock::now();
    for (auto &pair : slots_)
    {
        Slot &slot = *pair.second;
        if (slot.state != Slot::State::Ready || slot.sceneApplied)
            continue;
        applyScene(scene, slot);
        const double elapsedMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - budgetStart).count();
        if (elapsedMs >= applyBudgetMs_)
            break;
    }
}

void TileStream::ensureReady(Scene &scene)
{
    for (int i = 0; i < 256; ++i)
    {
        std::vector<std::pair<int, int>> wanted;
        desiredTiles(wanted);

        const int keepRadius = unloadRadius();
        std::vector<Key> extra;
        for (const auto &pair : slots_)
        {
            if (chebyshev(pair.second->x, pair.second->z) > keepRadius)
                extra.push_back(pair.first);
        }
        for (Key key : extra)
            drop(scene, key);

        const std::size_t need = std::max(budget_.maxTiles, wanted.size());
        while (slots_.size() + 1 > need)
        {
            std::vector<Key> wantedKeys;
            wantedKeys.reserve(wanted.size());
            for (const auto &tile : wanted)
                wantedKeys.push_back(pack(tile.first, tile.second));
            Key farthest = 0;
            int best = -1;
            for (const auto &pair : slots_)
            {
                if (std::find(wantedKeys.begin(), wantedKeys.end(), pair.first) != wantedKeys.end())
                    continue;
                const int d = chebyshev(pair.second->x, pair.second->z);
                if (d > best)
                {
                    best = d;
                    farthest = pair.first;
                }
            }
            if (best < 0)
                break;
            drop(scene, farthest);
        }

        for (const auto &tile : wanted)
        {
            if (!slots_.contains(pack(tile.first, tile.second)))
                startLoad(tile.first, tile.second);
        }

        for (auto &pair : slots_)
        {
            Slot &slot = *pair.second;
            if (slot.state == Slot::State::Loading)
            {
                slot.io.wait();
                slot.ioDone = true;
                pollIo(slot);
            }
            if (slot.state == Slot::State::Building)
            {
                slot.build.wait();
                slot.buildDone = true;
                pollBuild(slot);
            }
        }

        if (ringSettled(wanted))
        {
            for (auto &pair : slots_)
            {
                if (pair.second->state == Slot::State::Ready)
                    applyScene(scene, *pair.second);
            }
            return;
        }
    }
}

void TileStream::waitIdle(Scene &scene)
{
    for (int i = 0; i < 128; ++i)
    {
        pump(scene);
        bool pending = false;
        for (auto &pair : slots_)
        {
            Slot &slot = *pair.second;
            if (slot.state == Slot::State::Loading)
            {
                slot.io.wait();
                pending = true;
            }
            else if (slot.state == Slot::State::Building)
            {
                slot.build.wait();
                pending = true;
            }
            else if (slot.state == Slot::State::Ready && !slot.sceneApplied)
            {
                pending = true;
            }
        }
        if (!pending)
        {
            pump(scene);
            return;
        }
    }
}

StreamLayer TileStream::layer() const { return layer_; }
std::size_t TileStream::readyCount() const
{
    std::size_t n = 0;
    for (const auto &pair : slots_)
        if (pair.second->state == Slot::State::Ready && pair.second->tile)
            ++n;
    return n;
}
std::size_t TileStream::pendingCount() const
{
    std::size_t n = 0;
    for (const auto &pair : slots_)
        if (pair.second->state == Slot::State::Loading || pair.second->state == Slot::State::Building)
            ++n;
    return n;
}
std::size_t TileStream::bytesUsed() const { return totalBytes(); }

bool TileStream::hasReady(int tileX, int tileZ) const
{
    const auto found = slots_.find(pack(tileX, tileZ));
    return found != slots_.end() && found->second->state == Slot::State::Ready && found->second->tile;
}

const TerrainTile *TileStream::readyTile(int tileX, int tileZ) const
{
    const auto found = slots_.find(pack(tileX, tileZ));
    if (found == slots_.end() || !found->second->tile)
        return nullptr;
    return found->second->tile.get();
}

const MeshGeometry *TileStream::readyMesh(int tileX, int tileZ) const
{
    const auto found = slots_.find(pack(tileX, tileZ));
    if (found == slots_.end())
        return nullptr;
    return found->second->mesh.get();
}

int TileStream::readyLod(int tileX, int tileZ) const
{
    const auto found = slots_.find(pack(tileX, tileZ));
    return found == slots_.end() ? 0 : found->second->lod;
}

EntityId TileStream::entityId(int tileX, int tileZ) const
{
    const auto found = slots_.find(pack(tileX, tileZ));
    if (found == slots_.end())
        return terrainTileEntityId(tileX, tileZ);
    return found->second->entity != kInvalidEntityId ? found->second->entity : terrainTileEntityId(tileX, tileZ);
}
