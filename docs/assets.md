# Assets

Meshes and images are loaded from paths stored on the object or the scene. There is no asset database and no cooked copy.

## Meshes

`Mesh::load` reads `.obj` and `.fbx`. FBX import uses the Autodesk SDK: OpenGL axis, meters, triangulated, static node transforms only, one triangle soup, then fitted so the bottom sits on the ground. Skinning, animation, and more than one material per file are not read.

The editor Add mesh dialog filters `*.obj;*.fbx`. The scene file writes `obj` or `fbx` from the extension. Both kinds share the geometry cache described in `docs/transforms.md`. The cache key includes the write time. The editor calls `Mesh::refreshFromDisk` on every mesh at most four times a second, about every 0.25 s. If the file's write time or size changed, that mesh loads again and the view redraws. Albedo, normal, and environment images are checked the same way. A changed image is uploaded on the next render. The first time a path is seen it is only recorded, so opening the editor does not count as a change.

`setTriangles` builds a unique geometry that is not cached. The self-test uses that for a one-triangle mesh.

## Images

Albedo maps and normal maps are paths on `Material`. The environment map is one path on the scene. The GPU uploads them into a fixed set of texture units. See `docs/rendering.md` for the unit list and `docs/materials.md` for how the shader reads them.

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
- A reimport button or a watcher.
- Cooked runtime meshes so play does not parse FBX.
- LODs.
- Texture compression, and mipmaps beyond the upload that exists today.
- A budget readout that shows remaining room before the cap is hit.
