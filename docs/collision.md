# Collision

Play movement uses a sphere swept against simple shapes. It is not a physics world. The queries live in `engine/include/Collision.hpp` and `engine/src/Collision.cpp`. `stepPlay` in `engine/src/Play.cpp` moves the player. `castPlayRay` is the look query.

## Shapes

`sphereHitSphere`, `sphereHitAabb`, `sphereHitPlane`, and `sphereHitMesh` return a `Hit`: whether it hit, a normal, and a penetration depth. `resolveSphere` pushes the center out along that normal. Intersection helpers cover sphere-sphere and sphere-AABB without producing a normal.

What counts as solid for the player:

| Object | Solid when |
| --- | --- |
| The player sphere | Never |
| `pickup`, `trigger`, `goal`, `use`, `hazard`, `spawn`, or `spawner` | Never |
| Any other sphere | When its tag is not `player` |
| Plane | Always, even with an empty tag |
| Mesh | Tag is `solid` or empty. The shape is the triangles. The player sphere is tested in the mesh's local space through the BVH. A high branch does not fill the empty space under it |

A downward probe of `ground_probe` (default `0.08`) marks the player grounded when a solid hit has a normal whose Y is above `0.5`.

Each object has a layer. `0` is the default and is omitted from the scene file. It blocks the player and the chase camera. `1` is written as `layer 1`. It still blocks the player. `play_detail::isSolid` in `engine/include/PlayDetail.hpp` is the one solid test. The player call leaves `forCamera` false, so layer 1 stays solid. `chaseCameraPosition` calls it with `forCamera` true, which skips every layer other than 0. The demo door is layer 1. The inspector combo is Player and camera, or Player only.

The editor Colliders checkbox draws spheres and mesh boxes with `play_detail::isSolid`. Every plane is drawn as a patch. Light bounds and named-camera frustums are in that same overlay. See `docs/debug-and-performance.md`.

Sphere and plane collision use world radius and world normal, so a parented or scaled shape matches what is drawn. `castPlayRay` skips tags the player walks through (`pickup`, `trigger`, `hazard`, `spawn`, `spawner`, `player`) so a spawn sphere does not steal the look from a `use` or `goal` behind it. Solids still stop the ray.

## Character step

`stepPlay` finds the first sphere tagged `player`. If there is none, `playerId` stays `-1` and the step does nothing. Pause returns immediately.

Otherwise, with `dt` of `1/60`:

- Wish direction is camera-forward on XZ plus camera-right, then normalized. Speed is 4.
- Gravity is `-12`. Jump speed is `5`, and only while grounded. Vertical speed is `PlayState::verticalVelocity`.
- Motion is split into at least 4 substeps, more when the distance would exceed half the radius, and at most 32.
- Each substep moves, then up to 4 resolve passes. A hit slides the velocity along the normal so the player does not stick.
- If that slide stops horizontal motion against a surface no taller than `0.35` above the feet, the step is retried from on top of that surface and kept when it moves farther forward. A jump, while vertical speed is `1.5` or more, does not step. The grey `Step` pebble in the demo is one of these curbs.
- Before the player is integrated, moving solids advance. After events, a Use action may slide its target. If the player is grounded on either kind of solid, that position change is added to the player. See `docs/animation.md`.
- After the substeps, pickups, triggers, the goal, Use, and hazards are tested by overlap. `castPlayRay` then shoots from the player center along the camera look for `kPlayRayDistance` (3). It keeps the closest object other than the player, skipping tags the player walks through. A `use` or `goal` that the ray hits, and that the player is not already overlapping, gets the same F prompt and the same F result. A `spawn` point turns a hazard or a fall into a teleport. See `docs/gameplay-objects.md`.

`runPlaySelfTests` covers these cases and returns the failure count. `engine/src/self_test.cpp` adds that count to the engine total. Failures print. Success prints nothing of its own.

Editor picking is a separate ray through `Scene::intersect`. It is not this collision API.

## Not built

- A capsule.
- A simplified hull. The collider is every triangle.
- A third layer, or a layer that blocks the camera and not the player.
- Sphere casts and box casts. The camera ray is the only gameplay cast.
- Step-up higher than `0.35`.
