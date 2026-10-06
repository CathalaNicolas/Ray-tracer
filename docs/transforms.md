# Transforms

Each kind stores its own placement. There is no shared transform type. A parent id builds a frame from the root down. A mesh ancestor rotates that frame and multiplies its scale. A sphere or plane ancestor only adds its local position.

## Meshes

`Mesh` holds:

- `position` — local translation of the mesh origin. `position()` returns the world translation: this local value plus the world translation of the parent chain. `localPositionValue()` is the stored local value.
- `rotation` — Euler degrees. The order is yaw around Y, then pitch around X, then roll around Z. In matrices that is `R = Rz * Rx * Ry`.
- `scale` — one positive number. Values at or below zero are stored as `0.01`.

`meshRotate` and `meshRotateInverse` in `engine/include/Mesh.hpp` apply that order. All three angles within `1e-8` of zero skip the math and return the vector unchanged. The GPU instance texture stores the same rotation as the three columns of that matrix. The inverse used for reflection rays is the transpose.

World position of a local point is `position()` plus `meshRotate(local * scale, rotation)`. `position()` already includes the parent translation. Reflections still reject a mesh with the box around the eight corners of the local bounds, expanded by `1e-3`. Play collision tests the triangles in local space, then rotates the contact normal back.

Placements that load the same file share one `MeshGeometry` (triangles, BVH, local bounds, source path). The cache key is the canonical path plus the file's last write time. A new write time drops the older entries for that path. Each placement still has its own position, rotation, scale, material, name, id, and tag. The triangle budget counts unique geometries, not placements.

## Spheres and planes

A sphere is a center and a radius. A plane is a point and a unit normal, plus an optional checker scale. Neither has a stored rotation. The stored center and point are local. `center()` and `point()` return the world position: the local value plus the parent's world position. `localCenter()` and `localPoint()` are what the inspector and the scene file store. Moving one in the editor, or moving the player during play, writes that local value. Play can also write those local values from a rest pose plus a motion offset. See `docs/animation.md`. A sphere's radius is its scale for that motion.

## Lights and cameras

A light is a world position. A directional light still stores that position; the renderer treats the direction as coming from it. The editor camera and the chase camera are a look-from point, a look-at point, and an up vector of `(0, 1, 0)`. See `docs/cameras.md`.

## Parent

Every `Hittable` has `parentId`. `0` means no parent. `parentFrame()` walks from the root to the parent and stops when a parent id is `0`, the id is missing, or the id already appeared, including an object parented to itself. Each step looks up the parent with `Scene::find`, which is an `id` map. The result is cached on the object until `Scene::bumpParentFrames()` (after pose or parent edits, after play motions and actions, and before a GPU upload). A mesh in that chain calls `applyParentAxes` to rotate the axes and multiply the scale. The child's world position is that frame applied to its local position (`worldPosition()` / `setWorldPosition()`). A sphere's world radius is its stored radius times the parent scale. A mesh's world scale is its stored scale times the parent scale, and its world axes are the parent axes times its own Euler rotation. A plane's world normal is its stored normal turned by the parent axes. The scene file and the inspector still store the local values. `localScale()` and `localRotation()` stay local.

Rendering, collision, play movement, and the chase camera read the world position. Motion offsets and the Use action write the local placement.

## Not built

- Parent rotation and scale affecting the child.
- Non-uniform scale.
- One transform type shared by spheres, planes, meshes, lights, and cameras.
- Separate local and world matrices.
- Pivots and sockets.
