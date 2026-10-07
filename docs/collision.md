# Collision

Play movement is a Jolt capsule. `stepPlay` in `engine/src/Play.cpp` owns a `std::unique_ptr<jolt_play::World>` on `PlayState`. Each tick it upserts Jolt bodies for scene solids (create once, skip unchanged), drives `CharacterVirtual` from `PlayInput` / `Command`, calls `PhysicsSystem::Update` through Jolt's `JobSystemThreadPool`, then writes the capsule feet back onto the player sphere (center = feet + `(0, radius, 0)`). `castPlayRay` is still the look query. The chase camera is still a scene ray.

`Collision.cpp` is gone. Pickup / use / hazard / goal overlap still uses `Object::overlapsSphere` (`contactSphere` in `Object.cpp` and `MeshBvh.cpp`). `Hit` lives in `engine/include/Hit.hpp`.

If `jobs::init()` was not called, `World` is invalid and `stepPlay` logs an error and returns without moving the player.

## Solids in Jolt

`play_detail::isSolid` (`engine/include/PlayDetail.hpp`) is the same role test as before. Layer 0 blocks the player and the chase camera. Layer 1 blocks the player and lets the camera through. The demo door is layer 1.

| Scene shape | Jolt body |
| --- | --- |
| Player sphere | Capsule at the feet (`CharacterVirtual`, padding 0.02, cylinder half-height 0.05, radius = `bodyRadius`) |
| Other sphere | `SphereShape` at the world center |
| Plane | A thin box (half-extent 512 × 0.5 × 512) with its top on the plane |
| Mesh | `MeshShape` from `MeshGeometry` positions and indices (shared verts from cook/load), scaled by `worldScaleVec()` axes, cached by `contentHash` and the three scale components (kinematic when `Motion` is active or the mesh is a Use tween target; kinematic create supplies a dummy mass because `MeshShape` cannot compute one). Static bodies update when position or rotation changes |
| Terrain | `HeightFieldShape` from `TerrainTile` heights, cell size `tileSize/(sampleCount-1)`, holes as `cNoCollisionValue`. Always static. The visual mesh is not a Jolt body |

The mesh BVH stays for the ray tracer, `castPlayRay`, editor picking, and overlap tests.

A downward `CharacterVirtual` ground state replaces the old `ground_probe` sphere probe. Walk-stairs step-up is `0.35` plus 0.05 of padding. Character dt is split so no substep is longer than 1/60 s. `PhysicsSystem::Update` runs once per `World::step` with one collision step.

Save/load stores capsule feet, velocity, radius, grounded, `CharacterVirtual::SaveState` bytes, and `PhysicsSystem::SaveState` bytes on `SimPlayBlob` (`kSimPlayBitseryVersion` 3). Load drops the old `World`, then `primePlayPhysics` recreates solids, restores the physics system, spawns the capsule (`CharacterID(1)`), and restores character state. If those bytes are missing, it falls back to velocity plus `RefreshContacts`.

## Character step

`stepPlay` finds the first sphere tagged `player`. If there is none, `playerId` stays at invalid `EntityId` 0 and the step does nothing. `SimSession::begin` also sets `playerId` through `syncPlayPlayerId`. A non-empty `PlayState::result` (`won` / `lost`) returns immediately. `PlayState::paused` is unused by the integrator; win/lose do not set it. The HUD snapshot’s `paused` flag is true when `result` is non-empty. Editor Escape pause is `ViewState::paused` and does not set `PlayState::paused`.

Otherwise, with `dt` of `1/30`:

- Wish direction is camera-forward on XZ plus camera-right, then normalized. Speed is 4 (`EngineSettings::moveSpeed`).
- Gravity is `-12`. Jump speed is `5`, only while Jolt reports OnGround.
- Motions advance first so kinematic targets match the scene. Jolt ground velocity carries the player on a moving solid. There is no extra C++ carry.
- After the capsule step, pickups, triggers, the goal, Use, and hazards are tested by sphere overlap at the player center. `castPlayRay` then shoots from the player center along the camera look for `kPlayRayDistance` (3). It keeps the closest object other than the player, skipping tags the player walks through. A `use` or `goal` that the ray hits, and that the player is not already overlapping, gets the same F prompt and the same F result. A `spawn` point turns a hazard or a fall into a teleport (and `setCapsuleFeet` on the next tick). See `docs/gameplay-objects.md`.

`runPlaySelfTests` covers rest-on-plane, a blocking solid, pickup events, and kinematic platform carry through `SimSession::step`. `tests/main.cpp` still has the CharacterVirtual spike (fall, 0.35 curb, carry, mesh floor). `engine/src/SelfTestEntt.cpp` adds the play failure count to the engine total.

Editor picking is a separate ray through `Scene::intersect`. It is not this collision API.

## Not built

- A simplified hull authored in the editor.
- Rigid-body dynamics (mass, joints, sleeping). Platforms are kinematic boxes; other meshes are static or kinematic `MeshShape`.
- Continuous collision.
