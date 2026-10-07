#pragma once

#include "EntityId.hpp"
#include "Jobs.hpp"
#include "Scene.hpp"
#include "Terrain.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Mount travel is 3× walk. Load-ahead is two seconds of that, at least one tile.
inline constexpr double kStreamMountScale = 3;
inline constexpr double kStreamLookaheadSeconds = 2;
inline constexpr double kStreamDefaultApplyBudgetMs = 4.0;

enum class StreamLayer : std::uint8_t
{
    Sim = 0,
    Render = 1,
};

struct StreamBudget
{
    std::size_t maxTiles = 9;
    std::size_t maxBytes = 8u << 20;
};

int streamTileRadius(float tileSize, double moveSpeed);
std::string streamTileVirtualPath(int tileX, int tileZ);

class TileStream
{
public:
    void configure(const MapDef &map, StreamLayer layer);
    void setBudget(const StreamBudget &budget);
    // Cap main-thread apply work per pump (decode/mesh already off-thread). Default 4 ms.
    void setApplyBudgetMs(double ms);
    void setPathFn(std::function<std::string(int tileX, int tileZ)> fn);
    void setFocus(double worldX, double worldZ);
    // Non-blocking: start loads, finish ready IO/builds, apply/drop scene objects. For the view / render layer.
    void pump(Scene &scene);
    // Blocking: wait until every tile in the focus ring is Ready or Failed, then apply. For the sim layer.
    void ensureReady(Scene &scene);
    void waitIdle(Scene &scene);

    StreamLayer layer() const;
    std::size_t readyCount() const;
    std::size_t pendingCount() const;
    std::size_t bytesUsed() const;
    bool hasReady(int tileX, int tileZ) const;
    const TerrainTile *readyTile(int tileX, int tileZ) const;
    const MeshGeometry *readyMesh(int tileX, int tileZ) const;
    int readyLod(int tileX, int tileZ) const;
    EntityId entityId(int tileX, int tileZ) const;

private:
    struct Slot
    {
        int x = 0;
        int z = 0;
        enum class State : std::uint8_t
        {
            Loading,
            Building,
            Ready,
            Failed,
        } state = State::Loading;
        std::vector<std::uint8_t> bytes;
        std::string error;
        std::shared_ptr<TerrainTile> tile;
        std::shared_ptr<MeshGeometry> mesh;
        int lod = 0;
        int targetLod = 0;
        EntityId entity = kInvalidEntityId;
        std::size_t bytesUsed = 0;
        jobs::PinnedHandle io;
        jobs::PinnedHandle build;
        bool ioDone = false;
        bool buildDone = false;
        bool remeshOnly = false;
        bool sceneApplied = false;
        // Worker outputs (read on main after buildDone).
        std::shared_ptr<TerrainTile> builtTile;
        std::shared_ptr<MeshGeometry> builtMesh;
        std::string buildError;
        int builtLod = 0;
    };

    using Key = std::uint64_t;
    static Key pack(int x, int z);
    int loadRadius() const;
    int unloadRadius() const;
    int chebyshev(int x, int z) const;
    void desiredTiles(std::vector<std::pair<int, int>> &out) const;
    void startLoad(int x, int z);
    void startBuild(Slot &slot, bool remeshOnly);
    void pollIo(Slot &slot);
    void pollBuild(Slot &slot);
    void scheduleLodRebuild(Slot &slot, int lod);
    void refreshBytes(Slot &slot);
    void applyScene(Scene &scene, Slot &slot);
    void drop(Scene &scene, Key key);
    std::size_t totalBytes() const;
    bool ringSettled(const std::vector<std::pair<int, int>> &wanted) const;
    std::uint32_t workerPin() const;

    MapDef map_{};
    StreamLayer layer_ = StreamLayer::Sim;
    StreamBudget budget_{};
    double applyBudgetMs_ = kStreamDefaultApplyBudgetMs;
    std::function<std::string(int, int)> pathFn_;
    double focusX_ = 0;
    double focusZ_ = 0;
    int focusTileX_ = 0;
    int focusTileZ_ = 0;
    std::unordered_map<Key, std::unique_ptr<Slot>> slots_;
};
