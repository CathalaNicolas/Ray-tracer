# Streaming

`TileStream` (`engine/include/Stream.hpp`, `engine/src/Stream.cpp`) loads and unloads heightmap tiles around a focus point. The demo scene does not create a streamer.

## Rings and look-ahead

Focus world XZ maps to a tile index with `floor(x / tileSize)`, clamped to the map. The load ring is Chebyshev distance `streamTileRadius`. That radius is `ceil(ahead / tileSize)` with `ahead = max(tileSize, moveSpeed * 3 * 2)` (`kStreamMountScale` 3, `kStreamLookaheadSeconds` 2). Walk speed 4 and a 64-unit tile give radius 1 (a 3×3 clipped to the map). Unload uses hysteresis: a tile stays until Chebyshev distance is greater than `loadRadius + 1`, so walking back and forth across a boundary does not drop and reload a whole row.

Paths default to `cooked/t{x}_{z}.rtt`. `setPathFn` overrides that.

## Sim vs render

`StreamLayer::Sim` reads the `.rtt`, keeps `TerrainTile` for Jolt, and does not build a triangle mesh. `StreamLayer::Render` builds only the LOD needed for the current ring distance (`terrainBuildMeshLod`, lod 0/1/2 → stride 1/2/4). It does not build the unused coarser/finer levels. Texture mip streaming is not implemented.

`PlayState::stream` is the sim layer only. Before each play tick, `stepPlay` sets focus from the player XZ and calls `ensureReady`, which blocks until every tile in the focus ring is `Ready` or `Failed`, then applies those entities. Tile readiness therefore does not depend on whether a background read happened to finish mid-tick: the tick does not proceed until the ring is settled. A failed read marks the slot `Failed` so the wait cannot hang forever.

Render-layer streaming is owned by the view (`ViewState::renderStream` in the editor). After `applyDisplay`, the editor sets focus from the player display pose (or the camera) and calls `pump` — non-blocking, outside the simulation tick. The demo does not create a render stream yet.

## Entity ids

Streamed tiles use `terrainTileEntityId(tileX, tileZ)` (`kTerrainTileEntityPrefix` plus a 16+16 packing of the indices). Scene session prefixes never issue that prefix. Two sessions that load the same tile get the same id. Play-session save stores terrain as `SimShapeKind::Terrain` with `tileX` / `tileZ` (see `docs/persistence.md`).

## IO, decode, and budget

Each new tile schedules a pinned enkiTS read (`jobs::schedulePinned` on thread 1 when `jobs::threadCount() > 1`, else thread 0). When the read finishes, decode and (for render) a single-LOD mesh build run on another pinned worker (thread 2 when available). `contentRead` is mutexed. Slot states are `Loading` → `Building` → `Ready` / `Failed`. LOD changes re-enter `Building` with remesh-only work.

`pump` starts at most two loads per call and applies finished tiles under `setApplyBudgetMs` (default `kStreamDefaultApplyBudgetMs` = 4 ms). `ensureReady` starts every missing ring tile, waits on IO and build handles, and applies without the time budget. `waitIdle` loops `pump` until nothing is `Loading`/`Building` and every ready tile is applied (used by tests).

`StreamBudget::maxTiles` (default 9) and `maxBytes` (default 8 MiB) evict the farthest loaded tile first under `pump`. `bytesUsed` recounts height/hole/splat and mesh buffers whenever a build finishes. `ensureReady` will temporarily allow at least as many slots as the focus ring so the sim can wait on a full ring. Scene objects are tagged `solid`, named `Tile x z`, at `(x * tileSize, 0, z * tileSize)`.

## Not built

- GPU mip streaming or bindless residency.
- Recast/navmesh streaming.
- A mount speed separate from `EngineSettings::moveSpeed`.
- Wiring a render stream (or sim stream) into the demo room or editor camera by default.
