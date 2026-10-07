# Review: Phase 2

Reviewed on 6 October 2026, branch `engine-editor-restructure`, uncommitted working tree. Measured against the Phase 2 section of `docs/mmo-engine-plan.md`. The Phase 0 and 1 re-verification is in `docs/review-phase-0-1.md`.

## Verdict

**Phase 2 is a set of file formats and a tile loader. It is not a continent yet, and the renderer half of the phase has not started.**

What exists is honest groundwork. There is a terrain tile format with holes, a Jolt heightfield, a cooker that runs the right libraries, a zstd mesh format, PhysFS mounting, a FlatBuffers schema, DDS writing, CPU frustum culling, and a tile ring loader with a sim-only mode. Each piece has a test, and the docs mostly admit what is missing.

But the pieces are not connected to each other or to the game:

- Nothing outside the tests creates a `TileStream`.
- The cooked LODs are never drawn.
- The cooked textures are decompressed back to RGBA8 on load.
- The catalog is never loaded by the engine.
- The renderer is still the OpenGL 3.3 ray tracer, with caps of 256 mesh instances, 8 lights, and 262144 triangles.

The plan's exit is "walk 2 km at 60 fps with no hitch over 50 ms". It cannot be attempted yet, because there is no continent to draw. Even the tile loader on its own misses the frame budget at every tile crossing.

Two of the problems are structural and should be fixed before more is built on top:

1. **The cooker throws away what it computes.** The indexed, cache-optimized mesh is expanded back into a triangle soup before it is written. Materials are dropped. The runtime still parses source files and silently prefers stale cooked ones.
2. **Streaming runs inside the simulation tick and depends on disk timing.** That breaks the determinism Phase 1 just earned.

## How this was checked

- Read `Terrain`, `Stream`, `Content`, `Catalog`, `MeshCooked`, `MeshLoad`, `TextureCooked`, `Hash`, `Frustum`, `GpuLights`, the `GpuRender.cpp` instance upload, `cooker/src/*`, `schemas/content.fbs`, and the Phase 2 tests.
- Built Release and Debug. Release `tests.exe`: 35 of 35. Ran `cooker.exe` on `assets/door.obj`, `content/catalog.json`, and a pack.
- A temporary probe in `tests`, run and removed:

| Probe | Result |
| --- | --- |
| Terrain LOD extent on a 64-unit tile | LOD0 reaches 64.0. LOD1 stops at 62.98, LOD2 at 60.95 |
| Render-layer `TileStream`, 34×3 tiles, focus moved 0.4 units per pump over 2000 units | Mean 0.20 ms per pump. Worst 64 ms on the first load. 28 pumps over 16.6 ms, one at every tile crossing, each 17 to 23 ms |
| The same walk: did the focus tile have data? | Yes after the cold start. The cold start took 124 back-to-back pumps (about 25 ms of wall time), so this probe cannot say how it behaves at 30 Hz |

The walk probe pumps the loader as fast as it can, so its "not ready" count says nothing about real pacing. The per-pump times are main-thread work and do carry over.

## What is good

- The `RTT1` tile format is small, versioned, bounds-checked on decode, and carries holes and a splat map.
- Holes go through to Jolt as `cNoCollisionValue`, and a test proves a capsule falls through one and stands on the hill next to it.
- `StreamLayer::Sim` loads collision without building a render mesh. That is the server-ready split the plan asked for.
- The load-ahead radius is derived from speed (`streamTileRadius`: two seconds at 3× walk), not hard-coded.
- The cooker is on the plan's libraries: Assimp, meshoptimizer, DirectXTex, zstd, FlatBuffers. Assimp and meshoptimizer stay out of the runtime.
- Cook skipping is by SHA-256 of the source and its `.mtl` and texture dependencies, not by modification time.
- `PinnedHandle` waits in its destructor, so dropping a tile while its read is in flight is safe.
- The docs say plainly that the GL upload is RGBA8, that the JSON is parsed by a regex, that `--if-newer` is unused, and that clustered lights and Diligent do not exist.

## Critical

### 1. The cooker discards its own work

`cookSourceToRtm` (`cooker/src/CookMesh.cpp`) imports with `JoinIdenticalVertices` and runs meshoptimizer's vertex cache, overdraw, and vertex fetch passes on an indexed mesh. Then `toGeometry` expands the result into `MeshTri`, three private vertices per triangle. `appendLod` (`engine/src/MeshCooked.cpp`) writes `vertexCount = triangles * 3` and the identity index list 0, 1, 2, … The RTM format has an index buffer, but it never shares a vertex. So:

- The vertex cache and fetch optimizations have no effect on the file or the GPU.
- Every shared vertex is stored once per triangle that uses it (about six times on a smooth mesh), before zstd tries to win it back.
- The raster path then uploads that soup (`GpuRender.cpp` copies `triangle.position[corner]` per corner) and draws with `glDrawArraysInstanced`, not indexed.

Make the runtime mesh indexed (`positions`, `normals`, `uvs`, `indices`), keep the indices the cooker computed, and draw indexed. The BVH can still be built over triangles.

The cooker also drops everything that is not geometry:

- `walk` appends every `aiMesh` into one vertex list. The per-mesh material index is ignored, and RTM has no material or texture reference. A model with a trunk and a crown cooks to one mesh with one material.
- `aiProcess_PreTransformVertices` bakes the node hierarchy away, so pivots for doors, wheels, and Phase 3 sockets are lost at cook time.

### 2. The runtime still parses sources, and trusts stale cooked files

The plan says "the game never parses a source file." `loadMeshFile` (`engine/src/MeshLoad.cpp`) does this:

1. It looks for `cooked/<stem>.rtm`, using the file stem only.
2. If that exists, it loads it without checking that it matches the source.
3. Otherwise it parses the OBJ, or the FBX through the editor-registered loader.

Each step has a problem:

- **Stale files win.** Edit `door.obj` while an old `cooked/door.rtm` exists, and the editor and the hot reload in `refreshMeshFile` both show the old mesh. Nothing warns. The `.d` sidecar the cooker writes has the hash that would tell; the runtime never reads it.
- **Stems collide.** `assets/props/tree.obj` and `assets/nature/tree.obj` both map to `cooked/tree.rtm`.
- **Directory cooks are unreachable.** `cooker --dir` keeps relative paths, so it writes `cooked/props/tree.rtm`. The runtime only looks at `cooked/tree.rtm`.

Decide on one key. The cooked path should mirror the source path. Read the sidecar in editor builds and refuse a stale cooked file. In a shipping build, refuse a source path entirely.

### 3. Streaming is inside the simulation and depends on IO timing

`stepPlay` calls `state.stream->setFocus` and `pump(scene)` in the middle of the tick (`Play.cpp` lines 572–576). `pump` adds terrain entities to the simulation `Scene` when a pinned enkiTS read happens to have finished. That means:

- **The tick on which collision appears depends on the disk.** Two runs of the same commands can have the player land on a tile on different ticks. Phase 1's bit-for-bit replay holds only because no test turns streaming on during a replay.
- **Tile entity ids depend on read completion order.** They are allocated from the scene counter when `applyScene` walks the `unordered_map` of slots, so the order is IO timing and hash order.
- **Saves cannot restore streamed tiles.** A streamed tile's `MeshShape::sourcePath` is empty, because `decodeTerrainTile` never sets it. Terrain has no `SimShapeKind`, so it is saved as a mesh with no path. `applySimPlay` returns `false` for it unless the tile is already in the scene.

The simulation should ask for tiles and wait for them deterministically. For example, the tick blocks until the tiles a command needs are ready, or tile readiness is itself part of the recorded input. Streamed tiles should get ids derived from their map coordinates, not from the counter. The render stream should run on the view side, outside the tick.

## Major

### 4. Terrain LODs leave holes at the tile edge

`kTerrainSampleCount` is 64, so a tile has 63 cells. LOD strides are 2 and 4, and neither divides 63. `addLod` stops at the last full stride, so LOD1 ends one cell (about 1 unit) short of the +X and +Z edges, and LOD2 ends three cells (about 3 units) short. The probe measured 62.98 and 60.95 on a 64-unit tile. Every tile at LOD1 or LOD2 has a visible gap along two sides.

`validCount` requires an even sample count. That is exactly the wrong rule: chunked LOD wants 2^n + 1 samples (65, 129, 257) so that every stride lands on the last sample. 257 passes the `<= 257` bound, but the even check rejects it.

Even with the sample count fixed, neighbouring tiles at different LODs meet at T-junctions and crack. Nothing stitches them and there are no skirts.

### 5. The tile loader does its heavy work on the main thread

Only the file read runs on the IO thread. `finishLoad` decodes the tile, and for the render layer it calls `terrainBuildMesh`, on whatever thread calls `pump`. `terrainBuildMesh` builds all three LODs and a BVH, and then `terrainApplyLod` throws two of them away. When a tile changes ring, `pump` builds all three again, again on the main thread.

The cost, measured with 64-sample tiles and no GPU upload: 17 to 23 ms at every tile crossing, and 64 ms on the cold start when eight tiles finish together. At 60 fps every crossing drops a frame. The cold start alone is over the plan's 50 ms hitch limit. With 129- or 257-sample tiles it multiplies.

Smaller problems in the same file:

- `drop` and `configure` destroy a slot whose read is still running. That is safe because `PinnedHandle` waits, but the main thread blocks until the read finishes.
- `bytesUsed` is computed once at load and never updated when the LOD changes, so the byte budget drifts.
- There is no hysteresis. Walking back and forth across a tile boundary drops and reloads a whole row each time.
- A read that fails leaves the slot in `Loading` forever. It is never retried and never reported.
- All reads are serialized on one pinned thread and on one global mutex in `Content.cpp`. That mutex is held for the whole `PHYSFS_readBytes`, so a main-thread `contentExists`, which runs on every mesh load, waits behind a tile read.

### 6. Cooked textures are not used as compressed textures

`writeDdsFromRgba8` writes BC7 (or BC3) for colour and BC5 for normals, with mips. `loadDdsFile` then decompresses to RGBA8 and keeps only `GetImage(0, 0, 0)`, the top mip. So:

- The GPU memory win, which is the reason for BC formats, is zero.
- The cooked mip chain is thrown away, and the load pays a BC7 decode on the CPU.
- DirectXTex, a Windows-only library, is now linked into `raytracer_core` to do that decode. Together with `Hash.cpp` calling BCrypt directly, the core no longer builds anywhere but Windows. That matters the day the headless simulation should run on a Linux server.

Upload BC data directly. GL 3.3 has `glCompressedTexImage2D`, and BC7 needs `ARB_texture_compression_bptc`, which every D3D11-class GPU exposes. Keep DirectXTex in the cooker. Use a portable SHA-256 (or xxHash, since this is change detection, not security) in core.

Colour is written as `BC7_UNORM`, not `BC7_UNORM_SRGB`. That does not matter while the runtime decompresses. It will matter on the first direct upload.

### 7. The catalog JSON is parsed with regular expressions

`cookCatalogJson` finds `"items"`, `"creatures"`, and `"zones"` by substring, then matches each row with a regex that requires a fixed field order (`id`, then `name`, then `health`). This is the "recreate what a library already does" case the plan warned against:

- A row with fields in another order, an escaped quote, a nested object, or a negative number is dropped without an error.
- Adding a field to `content.fbs` means editing a regex by hand.
- `flatc` is already a build dependency, and it compiles JSON against the schema directly: `flatc -b schemas/content.fbs content/catalog.json`. That validates types, field names, and required fields for free.

Nothing in the engine loads the catalog either. `loadCatalogBytes` is called only by `ContentTests.cpp`.

### 8. The renderer half of Phase 2 has not started

| Plan item | Status |
| --- | --- |
| Clustered forward on Diligent and Direct3D 12 | Not started. OpenGL 3.3, GLSL 330 |
| No fixed caps; buffers sized at runtime | Not met. 256 mesh instances, 8 lights, 64 spheres, 16 textures, 262144 triangles (`GpuLimits.hpp`) |
| CPU frustum culling | Done for the colour pass. Shadow maps draw every instance inside view distance |
| Portal and occlusion culling | Not built |
| Instanced drawing | Done, as `glDrawArraysInstanced` per shared geometry. Not indirect, not indexed |
| Clustered light culling | Not built. `rankGpuLights` keeps the best 8 |
| Cascaded sun shadows | Not built |
| Sky, day clock, weather | Not built |
| Transparency sorting | Done for mesh instances, far to near |
| GPU particles | Not built |
| Material shader variants | Not built. One shader |
| Quality settings | Done: `view_distance`, `shadow_map`, `particle_density`, persisted |
| Splat-mapped terrain | Not drawn. The splat bytes are loaded and ignored |

A 64-sample tile is 7938 triangles at LOD0 and 1922 at LOD1, and each tile is its own unique geometry and its own instance. One ring is about 23000 triangles before a single prop. A forest of more than about 240 trees on top of that runs out of instances. These caps, more than the frame rate, are what stop a continent today.

Two bugs in what exists:

- **Non-uniform scale is drawn as uniform.** The raster instance is position, normalized axes, and one `scale` equal to the largest axis (`GpuRender.cpp`, `worldAxesFromMatrix`). The culling box in `meshWorldAabb` does the same. A mesh scaled (1.25, 0.8, 1.1) draws at 1.25 on every axis.
- **The emitter self-skip is broken.** See item 8 of the Phase 0 and 1 re-verification: `float(object.id())` against a compact id.

## Minor

- `cooker` prints "wrote" when the sidecar says the output is current and nothing was written.
- `--if-newer` is parsed and ignored. The docs say so, but the flag should go.
- `contentInit` returns `true` when `PHYSFS_init` fails.
- `docs/assets.md` says "later mounts win". `PHYSFS_mount(..., 1)` appends, so the loose `cooked/` directory is searched first and wins over `pak0.zip`. That is the right default for development. The doc has it backwards, and a patch archive layered later would lose.
- `packCookedZip` is a hand-written STORE zip with 16-bit entry counts and 32-bit offsets: no zip64, at most 65535 entries and 4 GB. It also packs the `.d` sidecars, which contain absolute source paths from the cooking machine. PhysFS reads zip; a writer such as miniz would remove this file.
- `meshopt_simplify` runs with a target error of 0.15, which is 15% of the mesh extent. That is coarse even for LOD2. There is no `LockBorder` option, so meshes with UV seams can open at the seams.
- `LiquidVolume` and `MapDef::kind` are loaded and saved, and nothing reads them. The checklist says **partial**, which is fair.
- `makeHillTerrain` is the only terrain source. Neighbouring tiles match only because the hill is symmetric. There is no heightmap import, no edge-sharing rule between tiles, and no world-space generator.
- The tile ring at the default 64-unit tile and walk speed 4 is always radius 1, so the budget of 9 tiles is never under pressure in practice. The budget test reaches its eviction path by forcing `maxTiles = 1`.

## Docs that do not match the code

- `docs/assets.md`: "Later mounts win." Earlier mounts win. See above.
- `docs/assets.md` describes RTM payload v2 as "float positions/normals/uvs and uint32 indices." True, but the indices are always 0, 1, 2, … and no vertex is shared. Say so, or fix item 1.
- `docs/engine-checklist.md` marks streaming **here**. It loads and unloads tiles correctly. It is used by no running path, has no hitch budget, and does its meshing on the main thread. That is **partial**.
- `docs/engine-checklist.md` marks heightmap tiles **here**. LOD1 and LOD2 leave gaps at the tile edge. That is **partial**.
- `docs/engine-checklist.md` marks DirectXTex DDS **here** with "GL upload still RGBA8". The note is honest. As a capability, compressed textures are not in use, so **partial**.

## Status against the plan

### Terrain and world layout

| Item | Status |
| --- | --- |
| Heightmap tiles with chunked LOD | Partial. Format and LODs exist. Edge gaps, no crack handling |
| Texture splatting, four to eight layers | Data only. Four layers stored, not drawn |
| Water and liquid volumes | Data only |
| Holes | Done. Mesh and Jolt, tested |
| Doodads and buildings with interiors | Not built |
| Interior cells and portals | Not built |
| Map definitions, instanced dungeons | Data only. One map per scene |

### Streaming

| Item | Status |
| --- | --- |
| Tiles around the camera on a background thread, in rings | Partial. Reads are background, decode and meshing are not. Not wired to the editor or the game |
| Memory budget per resource type, farthest first | Partial. One tile count and byte budget. Bytes go stale on LOD change |
| Texture mip and mesh LOD streaming | Terrain LOD only. Object LODs are cooked and never drawn. No texture streaming |
| Load-ahead for a 3× mount | Done as a formula |
| Simulation loads collision without render data | Done as a layer. Not deterministic, see item 3 |

### Assets

| Item | Status |
| --- | --- |
| Cooker through Assimp and stb_image | Done. Materials and hierarchy are dropped |
| The game never parses a source file | Not met. Runtime falls back to OBJ and FBX, and prefers stale cooked files |
| PhysFS zip with a hash manifest | Partial. Mounts and manifest exist. No patch layering, hand-written zip |
| BC1/3/5/7 with mips through DirectXTex | Cooked, then decompressed on load. Mips discarded |
| Mesh LODs, cache order, zstd | LODs cooked and never drawn. Cache order thrown away by the soup format. zstd done |
| Dependency tracking | Done for mesh sources and their `.mtl` and textures |
| FlatBuffers tables from JSON through `flatc` | Partial. Schema and loader done. JSON parsed by regex. Not loaded by the engine |

### Renderer

See item 8. Of the eleven renderer lines in the plan, three are done or nearly done: CPU frustum culling (no portals or occlusion), transparency sorting, and quality settings. Instancing is half done. The rest has not started.

### Exit

"Walk 2 km across the test continent at 60 fps on the minimum spec, with no hitch longer than 50 ms." **Not attempted.** There is no test continent, the stream is not connected to the renderer, and the render caps cannot hold the tiles and props of one. The loader alone misses 16.6 ms at every tile crossing and 50 ms on the cold start.

## Fix before going further, in this order

1. **Take streaming out of the tick.** Make tile readiness deterministic for the simulation. Derive tile entity ids from tile coordinates. Give terrain its own `SimShapeKind`, and save tile coordinates instead of a path. Pump the render stream on the view side.
2. **Index the mesh format.** Keep the cooker's indices end to end: RTM, `MeshGeometry`, the GPU upload, an indexed draw, and the Jolt `MeshShape` (which also fixes the seam problem in the Phase 1 review).
3. **One cooked path rule.** Mirror source paths under `cooked/`. Check the sidecar hash in editor builds, and refuse source formats in shipping builds. Carry submeshes and material references in RTM.
4. **Terrain sample count 2^n + 1**, LODs that reach the edge, and skirts or edge stitching between LODs.
5. **Move decoding and meshing off the main thread.** Build only the LOD that is needed, add a per-frame time budget for applying finished tiles, and add hysteresis to the ring.
6. **Upload BC textures compressed**, with their mips. Move DirectXTex out of core. Replace BCrypt with a portable hash.
7. **Compile the catalog with `flatc`** and delete the regex. Load it at startup.
8. **Build the measurement before the renderer.** A test continent, which is cooked tiles plus a few thousand instanced props, and a scripted 2 km walk that records frame times. That makes the exit target a test instead of a claim. It will also show at once that the GL caps cannot hold it. Then decide between lifting those caps in GL and starting the Diligent path the plan chose. Do not do both.
