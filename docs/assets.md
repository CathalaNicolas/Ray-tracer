# Assets

Meshes and images are loaded from paths stored on the object or the scene. There is no asset database id; the path is the key. `contentInit` in `engine/src/Content.cpp` mounts `cooked/` and, if it exists, `cooked/pak0.zip` through PhysFS at virtual prefix `cooked/`. Later mounts win. If PhysFS init fails, reads fall back to the real filesystem under the content root. When `cooked/catalog.bin` is present, `contentInit` loads it into `contentCatalog()` (`contentCatalogLoaded()` is true). `tests.exe` and `raytracer.exe` call `contentInit(".")` at startup.

## Meshes

`loadMeshFile` in `engine/src/MeshLoad.cpp` is the runtime entry. `.rtm` loads through `loadRtmFile`. Any other extension looks for a cooked mirror that keeps the source path under `cooked/` (`assets/props/tree.obj` → `cooked/assets/props/tree.rtm`). When that file exists, the runtime reads the cooker `.d` sidecar and accepts the cooked mesh only if the source (and OBJ `.mtl` / texture deps) still match. A stale or missing sidecar is ignored in editor/test builds so the source is parsed instead. With `RAYTRACER_SHIPPING=ON` (or `setAllowSourceMeshes(false)`), source OBJ/FBX/glTF paths are refused and only verified cooked meshes load. Otherwise the optional `setMeshFileLoader` (FBX SDK, editor only) or `mesh_obj::load` parses the source. FBX import uses the Autodesk SDK: OpenGL axis, meters, triangulated, static node transforms only, then fitted so the bottom sits on the ground. Skinning and animation are not read; Assimp cooking stores submeshes and material references in RTM.

The editor Add mesh dialog filters `*.obj;*.fbx`. The scene file writes `obj` or `fbx` from the extension. Both kinds share the geometry cache described in `docs/transforms.md`. The cache key includes the write time. The editor calls `Mesh::refreshFromDisk` on every mesh at most four times a second, about every 0.25 s. If the file's write time or size changed, that mesh loads again and the view redraws. Albedo, normal, and environment images are checked the same way. A changed image is uploaded on the next render. The first time a path is seen it is only recorded, so opening the editor does not count as a change.

`setTriangles` builds a unique geometry that is not cached. The self-test uses that for a one-triangle mesh.

## Cooked meshes

`cooker.exe` (`cooker/src/`) reads OBJ, glTF/GLB, FBX, or DAE through Assimp, runs meshoptimizer vertex-cache / overdraw / vertex-fetch (per submesh for cache/overdraw), then `meshopt_simplify` at 50% and 25% index counts into `MeshGeometry::extraLods` as shared-vertex index lists (skipped when the source has fewer than 12 indices or simplify does not shrink). Each Assimp mesh becomes a `MeshSubmesh` with a `MeshMaterialRef` (name, diffuse albedo, albedo/normal map paths). The cooker keeps that indexed mesh end to end. `engine/src/MeshCooked.cpp` encodes magic `RTM1`, zstd frame. Payload version 4 is the v3 vertex buffer and lod index lists plus material refs and submesh ranges. Versions 1–3 still decode. `MeshGeometry` stores indexed verts, materials, and submeshes; builds `triangles` for the BVH; uploads unique verts for `glDrawElementsInstanced` (still one draw with the placement material); and feeds the same indices to Jolt `MeshShape`. `tests.exe` round-trips `assets/platform.obj` through encode/decode without Assimp.

`cooker [mesh] [--if-newer] <source> <output.rtm>` cooks one file. Skip is SHA-256 of the source (and OBJ `.mtl` / `map_Kd` deps) against `<output>.d`, not mtime. `--if-newer` is accepted and unused. `cooker --dir [mesh] <folder> <out-folder>` walks mesh extensions and keeps relative paths with a `.rtm` suffix.

## Textures

`cooker image [--normal] <source.png> <output.dds>` loads with stb_image and writes DDS through DirectXTex inside `cooker/src/CookTexture.cpp` (not linked into core). Color tries BC7 (`TEX_COMPRESS_BC7_QUICK`), then BC3; normals use BC5. Mipmaps are generated when DirectXTex accepts the image. If compress fails, the DDS is uncompressed RGBA8 with those mips. HDR cook is not written.

`loadImage` sends `.dds` through `loadDdsFile` (`engine/src/TextureCooked.cpp`), a header-only-style DDS parser with no DirectXTex. Compressed BC1/2/3/5/7 keep every mip in `LoadedImage::mips` (`compressed = true`, empty `rgba8`). Uncompressed RGBA8 DDS fills `rgba8` and mip 0. `GpuRender` uploads matching compressed layers with `glCompressedTexImage3D` (and env maps with `glCompressedTexImage2D`), including the mip chain. Mixed or non-BC sets still scale into the fixed `kAlbedoEdge` RGBA8 array.

## Catalog and pack

`schemas/content.fbs` is compiled by `flatc` into `build*/generated/content_generated.h` (namespace `content`, identifier `RTCT`). `encodeCatalog` / `loadCatalogBytes` in `engine/src/Catalog.cpp` hold items, creature templates (id, name, health), and zones (id, name, map_id). `cooker tables content/catalog.json cooked/catalog.bin` shells out to `flatc -b` against that schema (no regex). `cooker pack <cooked-dir> <pak0.zip>` writes a STORE zip. `cooker manifest <cooked-dir>` writes `manifest.txt` (`rt-manifest 1` then `sha256 relative-path` per file, skipping `.d` and `manifest.txt`).

`engine/src/Hash.cpp` is a portable SHA-256 (no BCrypt).

## Images (runtime)

Albedo maps and normal maps are paths on `Material`. The environment map is one path on the scene. The GPU uploads them into a fixed set of texture units. See `docs/rendering.md` for the unit list and `docs/materials.md` for how the shader reads them.

## Third-party meshes

`assets/kenney-nature-kit/` is Kenney Nature Kit 2.1 (CC0). 329 vertex-colored OBJ models (trees, rocks, cliffs, bridges, tents). Source: [kenney.nl/assets/nature-kit](https://www.kenney.nl/assets/nature-kit), zip from [OpenGameArt](https://opengameart.org/content/nature-kit). See `assets/kenney-nature-kit/License.txt`. Credit Kenney.nl is requested, not required. The Autodesk FBX SDK is a separate Autodesk license used only by `raytracer_import`.

The demo scene (`createDemoScene`) still loads the handmade `assets/tree-*.obj`, `door.obj`, and `platform.obj`. After those stems exist under `cooked/`, `loadMeshFile` uses the `.rtm`. The Kenney kit is in the tree so a later demo pass can place those paths without another download.

`gpu_detail::cachedImage` in `engine/src/GpuRender.cpp` keeps loaded images in a map of at most 64 paths. A path that cannot be opened is not stored. If a stored entry is marked missing and the file is there on a later lookup, that lookup loads it. A stored image is loaded again when its write time or size changes. When the map grows past 64, every path except the one just requested is dropped.

## GPU budgets

`engine/include/GpuLimits.hpp` is the hard cap. The editor shows a warning only after a count is over the cap. Extra objects are skipped by the GPU path.

| What | Cap | How it is counted |
| --- | --- | --- |
| Mesh placements | 256 | Every mesh object |
| Triangles | 262144 | Once per shared geometry |
| Spheres | 64 | |
| Planes | 16 | |
| Lights | 8 | |
| Textures | 16 | Unique albedo map paths |

## Not built

- A reimport button. The Assets list already picks a mesh or image from `assets/` without a file dialog.
- Selecting a stored LOD at draw time; extra lods sit on `MeshGeometry` and are unused by the GPU path.
- HDR cooked textures.
- Stopping OBJ/FBX parse for the current demo scene unless a matching `cooked/<stem>.rtm` is present.
- A budget readout that shows remaining room before the cap is hit.
- Kenney meshes in `createDemoScene`.
