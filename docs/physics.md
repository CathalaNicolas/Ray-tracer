# Physics

Play gravity and jumping live on a Jolt `CharacterVirtual` inside `jolt_play::World` (`engine/include/JoltPlay.hpp` / `engine/src/JoltPlay.cpp`), driven from `stepPlay`. Defaults match `EngineSettings`: gravity `-12`, jump `5`, wish speed `4`. Vertical speed is also copied to `PlayState::verticalVelocity`.

Other objects do not fall, push, or sleep. Scene solids become Jolt bodies (`SphereShape`, plane-as-box, or triangle `MeshShape`). A pickup disappears on overlap. Nothing in play has mass, friction, or restitution except the character’s Jolt settings (`mMass` 70, unused against statics).

Jolt Physics is linked into `raytracer_core` with `JPH_DOUBLE_PRECISION` (overlay feature `double-precision`). `jobs::joltJobSystem()` returns Jolt’s own `JobSystemThreadPool` (not an enkiTS adapter). `World` construction needs that pointer. `World::step` calls `PhysicsSystem::Update` once per tick (one collision step) before the capsule `ExtendedUpdate`.

## `jolt_play::World`

After `jobs::init()`, `World` registers Jolt types once, builds a `PhysicsSystem`, and:

- Upserts static/kinematic boxes, spheres, and `MeshShape`s keyed by `EntityId`, plus a 512×0.5×512 box for each plane, plus a `HeightFieldShape` for each terrain tile.
A mesh with active `Motion` (or a live Use tween target) is kinematic. MeshShape cannot compute mass, so kinematic bodies get a dummy mass/inertia override on create. `step` sets linear velocity from the last body pose toward the scene target so ground velocity can carry the capsule (no `SetPosition` snap).
- Spawns a capsule `CharacterVirtual` with `CharacterID(1)` and the shape bottom at the feet.
- `step(dt)`: optimize broadphase if bodies changed, drive kinematics, `PhysicsSystem::Update`, `UpdateGroundVelocity`, then `ExtendedUpdate` the capsule (walk-stairs 0.35 plus 0.05 padding, stick-to-floor 0.5). Character integration splits `dt` so each substep is at most 1/60 s. No GPU.

`stepPlay` stores one `World` on `PlayState::physics` (`unique_ptr`) and syncs solids each tick. Transform edits, tag changes, and create/remove mark entities in `Scene::physicsDirty()`. Sync upserts dirty solids and motion/tween targets; other existing bodies are only touched so they stay alive. A fresh empty world still walks every solid once. Mesh shapes use `MeshGeometry` positions and indices directly in `MeshShapeSettings`, and the shape cache key is `MeshGeometry::contentHash` plus the three scale components (not the geometry pointer). Runtime OBJ/FBX soups are welded once in `finishGeometry` before that. Spheres and planes skip rebuilding a shape when radius/pose already match. Headless spike helpers (`addStaticBox`, `addStaticMesh`, `addStaticHeightField`, `addKinematicBox`) remain for `tests/main.cpp` and `tests/TerrainTests.cpp`.

Save/load uses `PhysicsSystem::SaveState` / `RestoreState` and `CharacterVirtual::SaveState` / `RestoreState` (`StateRecorderImpl` bytes on `SimPlayBlob`). Load rebuilds solids in scene order so BodyIDs match, restores the physics system, spawns the capsule, then restores the character. See `docs/persistence.md`.

## Not built

- Rigid bodies, mass, friction, and restitution on scene objects.
- Joints.
- Continuous collision for fast rigid bodies. The character uses predictive contacts (0.25) and 1/60 substeps.
- Sleeping dynamic bodies.
- Vehicles, cloth, soft bodies, and destruction.
