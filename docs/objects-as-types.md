# What is worth turning into a type

[objects-today.md](objects-today.md) is the inventory. This page says which of those piles of fields and `if`s would pay off as types, and which would only move the branch.

The frame time is the fragment shader. A virtual call on a few dozen scene records does not change that. The gain is that a new shape or a new role is added in one place, and a second copy of the rule cannot drift. `DebugDraw.cpp` already has its own `isSolid` that drops the `platform` tag `Play.cpp` still honors. That is the failure mode these types would close.

Do this only when a feature needs a new shape or a new role. A pass that renames structs and adds virtuals without a caller to delete is not that.

## Shape: one surface, still three classes — done

`Sphere`, `Plane`, and `Mesh` stay three classes. Shared ops live on `Hittable` as virtuals: transform, collision, `blocksPlayer` / `blocksCamera`, `colliderSketch`, `writeScene`, `contributeGpu`, `copyShapeFrom`, `mixShapeHash`, `applyParentAxes`. Callers that used to cast for those now dispatch once. A fourth shape implements the virtuals; the shader still needs its own loop.

The inspector still casts for shape-only widgets (plane checker, mesh path). Mirror-bounce debug still reads sphere radii for its dump. Roles sit beside the shape: `makeRole(tag)` builds a `Role` (`Solid`, `Player`, `Pickup`, …). Play uses `RoleKind`, not tag strings. The scene file still stores the tag word.

`Scene::find` is an `id` map. `parentFrame()` is cached until `bumpParentFrames()`. A shared transform matrix for lights and cameras is still missing on the checklist.

## Role: a small type beside the shape — done

A `SpherePickup` and a `MeshPickup` would duplicate the pickup rule and freeze the demo's accident that pickups happen to be spheres. The spawner already creates a sphere. A mesh pickup should be the same rule. Tags and shapes are independent on purpose.

A role is attached to any `Hittable`. `makeRole` builds it from the tag string. The file still writes that word. `collectPlayEvents` switches on `RoleKind`. `blocksPlayer` asks `passThroughSolid` / `meshBlocks`. `platform` is `Solid` with `carriesPlayer`.

## Light: keep one record

`PointLight` already is the object. Point, directional, and spot are flags because the shader has one light loop and the file has one light line. Splitting them into three C++ classes would add a switch at the upload boundary so the shader can pack the same uniform struct it packs now.

What is worth adding on the existing struct is the drawing and the range the overlay already computes: `sqrt(3 / falloff)` for the amber circle, the soft-shadow radius, the spot cone, and the directional arrow. That is a method. It does not need a subclass.

Emissive shapes becoming lights is an upload rule, not a light subclass. It should stay in `GpuRender.cpp`. A glowing mesh is still a mesh.

## Material: keep the struct

Glass, mirror, and diffuse are thresholds in one shader (`transmission`, `reflectivity > 0.35`, `emission > 0.01`). A `GlassMaterial` class would not move those branches out of GLSL. `makeGlass` and the inspector sliders already build one `Material`. Leave it.

## Camera: name the three jobs, do not merge them

| Record | Job | Leave it |
| --- | --- | --- |
| `Camera` | Rays for one shot | The render input. Built, used, discarded |
| `SceneCamera` | A named shot in the file | Data. V only reads it |
| `ViewState` + `ChaseCamera` | What the editor and play are doing with the view | Editor state |

A common position and direction helper would remove repeated `lookAt - lookFrom` normalizes. A `Camera` base with virtual `render` would not. There is one renderer, and play already produces a `Camera` for it.

## Sound and the editor

`playGameSound` can stay a function. A `SoundPlayer` class would own the listener, the master volume, and the three clips. That groups `Sound.cpp`. It does not change who calls it: `playScoreBeep` after a play step, with the camera as the listener and `PlayState.eventAt` as the source.

The editor as one class would gather `ViewState`, the history, and the window. The panels would still be functions. Virtual editor tools are not needed until a second tool exists beside the gizmo. The current cost is file size in `EditorScene.cpp` and `EditorUi.cpp`, which is a split by panel, not a missing base class.

## What to leave as branches

- The fragment shader. One program, data in uniforms. Mirror bounces, glass, and mesh traces stay there.
- `Vec3`, `Ray`, `HitRecord`, and the collision math functions. They are the numbers the types would call.
- Prefab sync. It copies fields on purpose, including onto the instance. A type does not change that rule.
- Particles. They are a capped array with a lifetime. A class per speck adds nothing.
- ImGui. The HUD and the editor are calls, not widgets we own.

## If a change starts

The order that removes real duplication first:

1. Move `contact` and world position onto `Hittable`, and delete the shape switches in `Play.cpp`. Point `DebugDraw.cpp` at `play_detail::isSolid` so the platform line cannot diverge again. That is the reliability fix, and it does not need roles yet.
2. Add the role type when the next gameplay object is not one of the current tags. Until then, one more `else if` on `tag` is smaller than the attachment machinery.
3. Move file write and GPU upload behind one function per shape when a fourth shape is actually added. Until then the cast chains are ugly and they match.

None of these steps change the scene-file version, the shader's numeric results, or the frame time. They change how many files a new shape or a new role has to touch.
