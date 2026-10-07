# Transforms

Every entity has one GLM `Transform`: `dvec3` position, `dquat` rotation, `dvec3` scale, and cached local/world `dmat4` values. Euler degrees exist only at the scene-file and editor inspector edge (`localRotation` / `setLocalRotation`). Play, clone, snapshots, and collision use the quaternion. Sockets are an empty `MeshSockets` stub.

Play display is a separate `DisplayTransform` component. `SimSession::applyDisplay` writes that component only. Simulation `Transform`s are not written by the view. `restoreSimPoses` is gone.

## Local pose

`Transform::position` is local translation. `Object::worldPosition()` is the translation of the cached world matrix. `localRotation()` / `setLocalRotation()` convert Euler degrees (yaw Y, then pitch X, then roll Z) with `quatFromEulerDegrees` / `eulerDegreesFromQuat` in `TransformMath.hpp`. Those two are inverses of each other. `q = qRoll * qPitch * qYaw`, matching `meshRotate` in `MeshGeometry.hpp` (yaw the vector, then pitch, then roll). Version-1 scene files use that same order.

`localScale()` is the largest of the three axes (handy for a single inspector number). `setLocalScale` writes the same value on X, Y, and Z (values at or below zero become `0.01`). Non-uniform scale is stored on `Transform::scale` and on snapshots. The raster instance, cull AABB, GPU mesh instance rows, and Jolt `ScaledShape` use the three axis lengths from the world matrix (`worldScaleVec` / matrix columns), so a box scaled `(4, 0.2, 4)` is flat. The editor still edits one number.

The GPU instance texture stores world matrix columns from `displayWorldMatrix()` while playing (falling back to `worldMatrix()` when no display pose is set). Scale is baked into those columns; the uniform scale channel is 1.

Placements that load the same file share one `MeshGeometry` (indexed verts, derived triangles for the BVH, local bounds, source path, `contentHash`). OBJ parsing lives in core (`MeshObj.cpp`); FBX stays in `raytracer_import` and is registered from `editor/src/main.cpp` with `setMeshFileLoader`. The cache key for FBX is the canonical path plus the file's last write time. Each placement still has its own transform, material, name, id, and tag.

## Lights and cameras

A light is a world position. A directional light still stores that position; the renderer treats the direction as coming from it. The editor camera and the chase camera are a look-from point, a look-at point, and an up vector of `(0, 1, 0)`. See `docs/cameras.md`. Chase follow uses `displayWorldPosition()` while playing.

## Parent

Every `Transform` has a parent `EntityId`; 0 means no parent. `Object::worldMatrix()` walks ancestors, stops on missing or repeated ids, and caches against `Scene::bumpParentFrames()`. Every shape inherits the complete parent matrix: sphere and plane children rotate and scale with parents exactly as mesh children do. Normals use the inverse-transpose matrix. Display matrices walk `DisplayTransform` when present, otherwise `Transform`.

Motion offsets and the Use action write the local simulation placement.

While playing, after each tick `captureSnapshot` stores every entity's local position, quaternion, and `dvec3` scale (`kSnapshotBitseryVersion` 2). Between ticks the editor calls `interpolateSnapshots` (alpha = leftover accumulator / `kPlayStep`) which lerps position/scale and slerps rotation, then `applyDisplay` writes `DisplayTransform`. Ids are hashed for interpolation, not scanned. Edit-mode gizmos still write `Scene` while play is off.

`Scene::clone` copies quaternions and scale vectors. A 3-axis rotation survives clone, scene save/load, and a play tick (`tests/TransformTests.cpp`).

`EntityId` 0 is invalid. New scenes pick a random 32-bit prefix in the high half of `sessionPrefix_` / `nextId_`, then a counter in the low half, so two sessions do not start at 1. Loading a scene file keeps the caller's session prefix (`retainSessionIds` after the parsed scene is moved in): file entities keep their stored ids, and `setNextId` from `next_id` only advances the counter when it shares the session prefix. Play-session restore calls `restoreIdState` so later spawns match the saved counter and prefix.

## Not built

- Transform components on lights and cameras.
- Editor controls for non-uniform scale.
- Pivots and sockets.
