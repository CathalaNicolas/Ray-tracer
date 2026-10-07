# Terrain and world layout

One heightmap tile is 65 by 65 samples (`kTerrainSampleCount` = 2^6 + 1) covering 64 world units (`kTerrainTileSize`). That count makes every LOD stride land on the far edge and matches Jolt `HeightFieldShape` block size 2. Neighbor tiles are addressed by integer `tileX` / `tileZ`. Loading a ring of tiles is `TileStream` in `docs/streaming.md`, not this page.

## Tile file

Magic `RTT1`, payload version 1, uncompressed. Layout in `engine/src/Terrain.cpp`: sample count, tile size, tile indices, flags, tightly packed float heights, optional per-sample hole bytes, optional 4-layer splat weights (`uint8` × 4 per sample). `cooker terrain [--hole] <output.rtt>` writes `makeHillTerrain`: a 2-unit Gaussian hill and, with `--hole`, a 4×4 hole near sample (2,2). Tests encode the same hill without the cooker.

`loadTerrainFile` uses PhysFS / disk the same way as other content. `Object::loadMesh` on a `.rtt` path becomes `loadTerrain`.

## Mesh and collision

`terrainBuildMesh` builds indexed geometry at stride 1 and extra LODs at stride 2 and 4 (each LOD owns its verts). A quad is omitted if any of its four samples is a hole. Each LOD also gets vertical skirts along the four tile edges so neighbouring tiles at different LODs do not show cracks. The GPU and the CPU ray still use LOD 0 (`MeshShape` geometry). Play does not pick a LOD by distance.

Play collision is a Jolt `HeightFieldShape` (`World::upsertHeightField`). Hole samples are `HeightFieldShapeConstants::cNoCollisionValue`. The body sits at the object's world position (sample 0,0). Object scale multiplies cell size and height. `tests.exe` stands a capsule on the hill and drops one through the hole.

## Scene

`Scene::map()` is `MapDef`: 64-bit id, name, `continent` or `dungeon`, tile size, tile counts. `Scene::liquids()` is a list of AABB + `surfaceY` + `water`/`lava`. Liquids are data only; play does not swim.

A terrain object is a mesh plus `TerrainTileComponent`. The scene file writes:

- `map <id> "name" continent|dungeon <tileSize> <tilesX> <tilesZ>`
- `liquid water|lava xmin ymin zmin xmax ymax zmax surfaceY`
- `terrain "name" px py pz scale` then material, optional `rot`, then `rtt "path"`

Older files without `map` keep the default Demo continent 64×1×1. The demo room is still a checker plane; it does not load a tile.

`terrainSampleHeight` bilinear-samples one tile in local XZ and returns false on a hole or outside the tile. `liquidContainsXZ` tests the volume's XZ rectangle.

## Not built

- GPU splat shaders; splat weights are stored and unused by GLSL.
- Swimming, buoyancy, or liquid rendering.
- Interior cells and portals.
- Instanced doodads / scatter brushes / terrain sculpting in the editor.
- Recast navmesh on the tile.
- Selecting a stored terrain LOD at draw time.
