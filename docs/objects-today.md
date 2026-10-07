# Objects as they are used

This page lists the things the program treats as objects, and the features that behave like objects without being types. It describes the code as it is. [objects-as-types.md](objects-as-types.md) is the follow-up: which of these would be worth a real type later.

The scene owns EnTT entities and keeps stable 64-bit ids in insertion order. Play, save, the editor, and GPU upload walk non-owning `Object` handles and dispatch by shape components.

## Three kinds of "object"

| Kind of thing | What it is in code | How a caller tells them apart |
| --- | --- | --- |
| Shape | `SphereShape`, `PlaneShape`, or `MeshShape` component | `Object::isSphere/isPlane/isMesh` or `kind()` |
| Role | `Role` beside the shape | Built from `tag` by `makeRole`. Play uses `RoleKind` |
| Upload record | Arrays of floats and ints inside `GpuRender.cpp` | The shader loops `uSphereCount`, `uPlaneCount`, `uMeshCount`, `uLightCount`, `uMirrorCount` |

A purple switch is a `Sphere` whose `tag` is `use`. A door is a `Mesh` whose `tag` is `solid` and whose `layer` is 1. The same shape can carry any role. Nothing in the type system stops a mesh from being a pickup or a sphere from being a goal.

## Shapes

`Object` is a non-owning `{Scene*, EntityId}` handle. EnTT owns transform, shape, material, identity, role, motion, action, prefab, spawner, and audio-stub components. `Scene::objects()` exposes stable handles in `order_`.

The base also stores the fields that are not part of the shape:

- `id`, `parentId`, `name`
- `tag`, `layer`
- `prefab`, `instanceOf`
- `motion`, `action`
- `spawnEvery`, `spawnClock`, `spawnedId`

`kind()`, `clone()`, `material()`, `setMaterial()`, `intersect()`, and the shared shape ops live on `Object`: local/world position, rotation, scale, `bodyRadius`, `contactSphere` / `overlapsSphere`, `blocksPlayer` / `blocksCamera`, `colliderSketch`, `writeScene`, `copyShapeFrom`, and `mixShapeHash`. Play, save, prefab sync, and the collider overlay call those. GPU upload uses `gpu_detail::contribute` in render. The inspector still branches for shape-only fields (plane checker, mesh path).

`Sphere` stores a `SphereShape` radius plus a `Transform`. World position and radius come from the cached world matrix, including parent rotation and scale.

`Plane` stores a local point, a unit normal, a `Material`, and an optional checker color. A plane is infinite. The collider overlay draws a square of half-extent 2 so it has something to show.

`Mesh` stores a position, a uniform scale, Euler rotation (yaw, then pitch, then roll), a shared `MeshGeometry`, and a `Material`. Triangles and the BVH live on the geometry, so two placements can share one mesh file. Primary visibility on the GPU is a raster of that mesh. Reflections and glass walk the BVH in the shader. Play movement collides with a Jolt `MeshShape` from those triangles, not with the blue box the overlay draws.

Cached GLM local/world matrices handle parenting. Parent id 0 means no parent, and every shape inherits translation, rotation, and scale.

## Roles

The scene file stores one tag string. `Object::setTag` / `ensureRole` update the `Tag` component's `Role`. Play dispatches on `RoleKind`; `layer` stays separate.

| Role / tag | Who runs it | What the code does |
| --- | --- | --- |
| `Player` / `player` | `findPlayerBody` | The first body with this role and a positive radius is moved |
| `Solid` / empty, `solid` | `blocksPlayer` | A sphere or plane blocks unless pass-through. A mesh blocks when `meshBlocks` is true |
| `Solid` / `platform` | `blocksPlayer`, then Jolt kinematic ground velocity | Blocks, and a grounded player is carried with it |
| `Pickup` | overlap in `collectPlayEvents` | Score +1, health +1 up to 3, then the object is removed |
| `Trigger` | overlap | Banner text only |
| `Goal` | overlap, or the play ray within 3 | F wins, or loads the east room when `room` is 1 |
| `Use` | overlap, or the play ray within 3 | F shows the name and may start one `Action` |
| `Hazard` | overlap | Health and respawn, or a loss |
| `SpawnPoint` / `spawn` | the loss path | First one is the respawn point. Not solid |
| `Spawner` | `advanceMotions` | Adds a pickup sphere on a timer. Not solid |

`layer` is a second switch, not a role. `0` blocks the player and the chase camera. `1` still blocks the player. `chaseCameraPosition` calls `isSolid` with `forCamera` true and skips every layer other than 0.

The editor collider overlay calls `play_detail::isSolid`, so a platform mesh is drawn when Colliders is on.

## Relations that already exist

These are fields, not types that point at each other.

- `parentId` — the transform parent. `Object::worldMatrix()` walks it; the cache is invalidated by `Scene::bumpParentFrames()`.
- `prefab` and `instanceOf` — a source object and its copies. `syncPrefabInstances` copies material, tag, layer, motion, scale, and rotation onto each instance and leaves the instance root position alone. That overwrite is the prefab rule.
- `action.target` — the name of another object. F on a `use` looks the name up and pushes a `Tween`.
- `spawnedId` — the pickup a spawner last created. The spawner adds another when that id is gone.
- Lights are not children. `PointLight` values sit in `Scene::lights_`. An emissive shape becomes an extra point light only while `GpuRender.cpp` is uploading, and only while fewer than 8 lights are in use.
- Named cameras are `SceneCamera` values in `Scene::shots_`. They are not entities yet. V during play steps through them.
- The player is not a member of `Scene`. Each step scans for `RoleKind::Player`.

`Action` and `Motion` live on every shape even when unused. `Motion` ping-pongs from a rest pose. `Action` is the one-shot move and rotate. `Tween` in `PlayState` is the running copy of an action: at most `max_tweens`, each `action_duration` long in ticks (defaults 8 and 0.4 s → 12 ticks). `fired` on the action stops a second press.

## Other records

`Material` is a struct of numbers and two texture paths. Glass, metal, and diffuse are not classes. `makeGlass`, `makeMetal`, and `makeDiffuse` fill the same struct. The shader branches on `transmission`, `reflectivity`, and `emission`.

`PointLight` is one struct for a point, a directional, and a spot. `directional` and `spotOuter` are the switches. The shader has one light loop and reads those flags. The name stays `PointLight`.

`Camera` in `engine/include/Camera.hpp` is the ray-generation camera: origin, image-plane vectors, aperture, focus. The editor rebuilds it every shot from `ViewState` via `viewCamera`. `ChaseCamera` is the play orbit (yaw, pitch, distance, first person, the V blend). `ViewState` is the editor camera plus selection, gizmo mode, and the collider checkboxes. Three camera records, one render camera.

`Particle` is a position, velocity, color, life, and size. `Scene` keeps at most 32. They are not shapes and they are not saved.

`PlayState` holds score, health, the message, the room, the tweens, and the look ray (`lookId`, `lookName`, `lookTag`, `lookPoint`). It is not in the scene file. `PlayInput` is move, jump, and use for one step.

Sound is three free functions plus a listener position: `setMasterVolume`, `setSoundListener`, `playGameSound`. Clips are `GameSound::Beep`, `Pickup`, and `Win`. There is no sound object in the scene.

The editor is the namespace `ed` in `editor/src/`. `runEditor` owns the window loop. Panels, play, history, and widgets are free functions. There is no `Editor` class that owns the scene.

## Where the same decision is written again

Adding a fourth shape, or a new role, means editing each of these. They do not call one shared function.

| Decision | Files that branch |
| --- | --- |
| Shape is sphere, plane, or mesh | `Object` dispatches on EnTT shape components. The shader still has one loop per kind |
| Tag is a gameplay role | `Role.hpp` / `Role.cpp` plus `makeRole`. Play uses `RoleKind`. HUD may still read the tag string for display |
| Light is point, directional, or spot | `GpuRender.cpp` upload, the shader light loop, `DebugDraw.cpp` `drawLightBound`, the scene file light line |
| Material is glass, mirror, or emissive | The fragment shader (`transmission`, `reflectivity > 0.35`, `emission > 0.01`), plus the mirror-index upload in `GpuRender.cpp` |

`kind()` is a convenience over component checks. There is no RTTI shape hierarchy.

## What the GPU is doing instead

`GpuRayTracer::render` flattens the scene once per frame (or reuses the previous upload when the fingerprint matches). Spheres, planes, meshes, lights, and mirror indices become uniform arrays. Each uploaded object gets a compact 1-based GPU id for that frame (`uSphereId`, `uPlaneId`, instance row 4, `uSelected`); the 64-bit `EntityId` is not truncated into those ints. The fragment shader then shades without calling back into C++.

That is why a C++ `if` on a tag does not show up in the frame time. The milliseconds are the shader: primary hits, shadow maps, and the mirror-bounce loops, which walk spheres and, when a bounce is kept, the mesh BVH. Settings → Mirror bounces off sets `uMirrorCount` to 0 so that walk does not run. It does not change the C++ types.

A new shape still needs a GLSL branch even if the C++ side becomes a virtual call. The shader is one program (`engine/src/GpuShaderTrace.cpp`), split into raw strings because of the MSVC string-length limit. It is not a set of shader objects.
