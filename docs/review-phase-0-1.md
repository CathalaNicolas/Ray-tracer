# Review: Phase 0 and Phase 1

First reviewed on 6 October 2026, branch `engine-editor-restructure`, uncommitted working tree. Measured against `docs/mmo-engine-plan.md`. Re-verified the same day after the fix pass and the Phase 2 work. The Phase 2 review is in `docs/review-phase-2.md`.

## Re-verification

**Phase 1 is now done, with three holes. Phase 0 is still partial for the same reason as before: EnTT sits behind the old `Object` interface.**

The fix pass was real work, not box-ticking. The three things the first review called critical are fixed and stay fixed under measurement. The tests that cover them can now fail. What is left is smaller, but two of the holes are new bugs introduced by the fixes themselves: entity ids after a file load, and the GPU emitter id.

### How this was checked

- Read the changed code again: `TransformMath.hpp`, `SimChannel`, `SimSerialize`, `Play.cpp`, `JoltPlay.cpp`, `Jobs.cpp`, `Object.cpp`, the editor frame loop, and the new tests.
- Built the `windows-msvc` preset. `tests.exe`: 35 of 35 cases, 258 assertions, 4.7 s. `raytracer.exe --self-test`: ok.
- Built the `windows-msvc-debug` preset. Debug `tests.exe`: 35 of 35, 116 s. The Debug editor does not link (see item 9).
- A temporary probe file in `tests`, run, then removed. Nothing from it is left in the tree.

| Probe | First review | Now |
| --- | --- | --- |
| Euler → quaternion → Euler → quaternion, 8125 triples including pitch ±90° | 18° to 122° off | Worst 1.7e-6° |
| 200 random rotations through scene save and load | Not measured | Worst 2.4e-6°, every id kept |
| (30, 45, 20) through clone, save, load, one play tick | 18.1° and 31.6° off | Required equal by `TransformTests.cpp` |
| Mid-jump save, load into a fresh state, continue | 1.62 units off after 60 ticks | Bit-identical after 300 ticks (`SimSaveTests.cpp`) |
| Editor display path against headless, 3000 ticks | Diverged at tick 0 | Bit-identical (`SimReplayTests.cpp`) |
| Tick cost at 1000, 3000, 5000 solid meshes | 2.1, 8.1, 14.5 ms | 1.5, 6.0, 11.2 ms |
| Display interpolation at the same counts | 0.5, 3.6, 9.7 ms | 0.18, 0.67, 1.25 ms |
| Two sessions load the same scene file and each add one entity, 40 pairs | Not measured | 15 pairs issued the same id. 25 pairs issued different ids |

### Item by item

| # | Item | Verdict | What is left |
| --- | --- | --- | --- |
| 1 | Rotations | **Fixed** | Authored degrees are still derived from the quaternion, so the inspector can show a different but equivalent triple after an edit near pitch ±90°. Cosmetic |
| 2 | View writes simulation | **Fixed** | The renderer, frustum, lights, and chase camera read `DisplayTransform`. The editor only writes `ViewState::paused`. `PlayState::paused` is now a dead field that is still serialized in the save blob. Particles still live on `Scene`, though `clone` no longer copies them |
| 3 | Saves do not resume | **Fixed** | `applySimPlay` deletes the extra entities first and only then returns `false` on a missing mesh, so a failed load leaves the scene half replaced. Validate the whole blob before touching the scene |
| 4 | Replay test cannot fail | **Fixed** | Equality is required. 18000 ticks on the demo, plus pickup, goal, hazard, and the display path |
| 5 | Collision | **Mostly fixed** | See below |
| 6 | Jolt jobs | **Fixed, differently** | Jolt runs on its own `JobSystemThreadPool` with `hardware_concurrency - 1` workers, next to enkiTS's own pool. Two pools on one machine oversubscribe the cores once both are busy |
| 7 | Core links FBX | **Fixed** | OBJ moved into core, and FBX is registered by the editor through `setMeshFileLoader`. `tests` links core only. `raytracer_render` still links `raytracer_import` publicly for the self-test |
| 8 | Entity ids | **Partial, with a new bug** | See below |
| 9 | Build hygiene | **Partial** | See below |

**Item 5, collision.** Meshes collide against their triangles again (`MeshShape`). Shapes are cached per geometry, and an unchanged body is no longer touched. That is the fix that was asked for. It still costs 11.2 ms per tick at 5000 meshes, because `syncJoltSolids` walks every object every tick to find out that nothing changed. Four smaller problems remain:

- `upsertSphere` and `upsertPlane` still build a new shape before checking whether the body exists.
- The mesh shape cache is keyed by a raw `MeshGeometry*`. After a hot reload frees a geometry, a new one at the same address gets the stale shape.
- Triangles go to Jolt unindexed, three new vertices per triangle (`meshShapeCached`). Jolt cannot find shared edges, so active-edge detection cannot work, and a character sliding across a seam between two floor triangles can catch on the inner edge. Weld the vertices first; meshoptimizer is already a dependency.
- Collision, like the renderer, uses the largest axis of a non-uniform scale (`worldScale()`). A box scaled (4, 0.2, 4) collides as a 4×4×4 box.

**Item 8, ids.** A new scene starts at a random 32-bit prefix, and the editor no longer passes ids through `int`. Two problems:

- **Ids issued after a file load are neither unique nor deterministic.** `Scene::setNextId` and `Scene::create` adopt any larger id they see, so after `loadScene` the counter is either the file's counter or the new random prefix, whichever is bigger. In the probe, 15 of 40 pairs of sessions loading the same file both issued the same next id. That is the collision the prefix was meant to prevent. The other 25 issued different ids, so a replay that starts from a file and spawns entities is not reproducible across processes. Keep the session prefix for new ids, whatever the file says, and store the counter per prefix.
- **The GPU emitter check broke.** `GpuRender.cpp` writes `float(object.id())` into `uLightAux[i].w`. The shader compares that with `hit.id`, which is the compact GPU id (1 to N). A 64-bit prefixed id does not survive a float (the probe's id 12248621200565075968 comes back as 12248621106075795456), and it never equals a compact id anyway. The three loops in `GpuShaderTrace.cpp` that skip an emissive object's own light, at lines 400, 878, and 1022, no longer skip anything. Use `compactIdOf(object.id())`.

**Item 9, build.** The vcpkg baseline is pinned, and a Debug preset exists. Debug tests pass. The Debug preset is not usable for the editor, though:

- The editor links `libfbxsdk-md.lib` from `lib/x64/release` in every configuration, so the Debug link fails on `_ITERATOR_DEBUG_LEVEL` and `RuntimeLibrary` mismatches.
- Both presets write executables and copied DLLs into the repo root. During this review, the failed Debug link deleted the Release `raytracer.exe`. The Debug `tests` build then replaced seven Release DLLs with Debug DLLs of the same name: `Jolt`, `physfs`, `sqlite3`, `zstd`, `DirectXTex`, `enkiTS`, and `TracyClient`. A Release editor started after that would load Debug DLLs. The Release build was rerun to restore them. Give each preset its own output directory. Link the FBX debug library in Debug.

### Minor items from the first review

| Item | Now |
| --- | --- |
| No vcpkg baseline | Fixed |
| Release only | Partial. See item 9 |
| Euler lerp, quadratic `findPose` | Fixed. Slerp and a hash map |
| Mean scale against max scale | Consistent now, at max scale everywhere. Non-uniform scale is stored and simulated but dropped by the renderer, collision, and the scene file |
| Unbounded recorder | Capped at 2^20 commands with one warning. Recording then stops |
| Restart clone not bit-identical | Fixed. `sceneTransformsEqual(scene, clone)` is required |
| `PlayState` shares Jolt through `shared_ptr` | Fixed. `unique_ptr` |
| Win32 file dialogs | Unchanged |
| `std::cerr` in `addParticle` | Fixed. spdlog |
| EnTT behind `Object` | Unchanged. `Scene::create` still adds 8 components to every entity, there is still no way to create an entity without a shape, and `MeshSockets` and `AudioEmitter` are still empty |
| Physics invalid without `jobs::init` | Now logs an error. The player still stands still |

### Status against the plan, now

| Phase 0 item | Status |
| --- | --- |
| CMake and vcpkg | Done. Baseline pinned. Debug editor does not link |
| `core`, `render`, `audio`, `import` targets | Done. Core links no FBX |
| EnTT entities and components | Partial. Unchanged |
| 64-bit `EntityId` unique across sessions | Partial. Unique until a file is loaded |
| GLM transform | Done for rotation. Non-uniform scale is not honoured outside the simulation. Sockets empty |
| enkiTS with async file reads | Partial. Tile reads run on a pinned task. `runPinned` in the OBJ loader waits, so it is a synchronous read on another thread |
| SDL3, spdlog, Tracy, doctest | Done |

| Phase 1 item | Status |
| --- | --- |
| Simulation separate from the view | Done |
| Jolt `CharacterVirtual`, triangle collision, `Update` | Done. Per-tick sync is O(objects) |
| SQLite save that resumes | Done for the scene it came from. Not atomic on failure |
| Exit: 10-minute bit-for-bit replay | Met, in the suite, headless and through the display path |
| Exit: mid-play save loads to the same state | Met, in the suite |
| Exit: the game plays only through the channel | Met, except that `TileStream` is pumped inside `stepPlay` (see the Phase 2 review) |

### Fix next, in this order

1. Use the compact GPU id for emitters. One line, and it is a visible lighting regression.
2. Keep the session prefix when a file is loaded, so ids stay unique and deterministic.
3. Separate output directories per preset, and the FBX debug library in Debug.
4. Validate the save blob before changing the scene in `applySimPlay`.
5. Weld vertices before building a Jolt `MeshShape`. Key the shape cache by something that outlives the pointer.
6. Track dirty transforms instead of walking every object in `syncJoltSolids`.
7. Honour non-uniform scale in the raster instance, the culling box, and collision, or remove it from the transform.

---

The rest of this page is the first review, unchanged. It is kept because the fix pass above was driven by it.

## Verdict (original)

**Phase 0 is partial. Phase 1 is not done.**

The build move is real, and the headless simulation is genuinely deterministic. Those are the two hardest things to retrofit, and they work.

The parts that later phases stand on do not work yet:

- Rotations are corrupted by the Euler conversion, every frame during play and every time a scene is cloned or saved.
- The view writes into simulation state, which is the one rule the deferred network depends on.
- A save does not resume.
- The core still links the FBX SDK.
- The replay test is written so that it cannot fail.

Do not start Phase 2 until the first three are fixed. Every system added on top of them makes the fix more expensive.

## How this was checked

- Read the new and changed code in `engine/`, `editor/`, `tests/`, and the build files.
- Configured and built the `windows-msvc` preset from the repo-local vcpkg. It builds, with 9 warnings, 7 of them `EntityId` truncated to `int` in the editor.
- `tests.exe`: 8 of 8 test cases, 85 assertions pass. `raytracer.exe --self-test`: ok.
- A temporary probe suite, built into `tests.exe`, run, then deleted. Results below. Nothing from it is left in the tree.

| Probe | Result |
| --- | --- |
| Two freshly built demo scenes, lockstep, 18000 ticks (10 minutes) | Bit-identical |
| Recorded commands replayed with `replayCommands`, 18000 ticks | Bit-identical |
| Demo scene compared with its own `clone()` | Not bit-equal. Yaw-only rotations come back with different low bits |
| Editor display path (`applyDisplay` then `restoreSimPoses` each tick) against headless, 3000 ticks | Differs from tick 0. Tree trunk rotation drifts 1.7e-6° |
| Save 4 ticks after a jump, load into a fresh `PlayState`, run 60 more ticks | Player ends up 1.62 units from where the uninterrupted run is |
| Mesh rotated (30, 45, 20) degrees, cloned once and twice | 18.1° and 31.6° away from the authored rotation |
| Same rotation through the old `meshRotate` and the new quaternion | Different results: (0.544, 0.574, −0.612) against (0.785, 0.296, −0.544) |
| Tick cost with 1000, 3000, 5000 solid cube meshes | 2.1, 8.1, 14.5 ms per tick. Display frame 0.5, 3.6, 9.7 ms |

The 10-minute run had the goal and hazard removed so the round would not end. It never collected a pickup. It covers movement, jumps, the moving platform, Use presses, and the spawner. Determinism of the scoring rules is not proven by it.

## What is good

- CMake, presets, vcpkg manifest, and a Jolt overlay port with double precision. `build.bat` finds the repo-local vcpkg and builds in one go.
- The headless simulation is deterministic over 10 minutes, including Jolt's character controller. That is the property replays, saves, and a future server all need, and it holds.
- Input enters only as tick-stamped `Command`s. The simulation never reads keys or the clock.
- Timers are tick counts, with one conversion from seconds at the edge (`playTicksFromSeconds`).
- Typed `SimEvent`s replaced the string comparison. `playSimEvents` in `editor/src/EditorPlay.cpp` switches on the kind.
- Sound is on miniaudio. `PlaySound` and MCI are gone.
- The docs are honest about the shortcuts: mesh collision as boxes, `PhysicsSystem::Update` not called, the 10-minute replay not measured. That honesty made this review faster.

## Critical

### 1. Euler conversion corrupts rotations

`quatFromEulerDegrees` builds `yaw * pitch * roll`. `eulerDegreesFromQuat` decomposes with `glm::eulerAngles`, which assumes a different order. The two are not inverses (`engine/include/TransformMath.hpp`, lines 20–38). Any rotation with yaw and roll both set changes by 18° to 122° in one round trip, and keeps drifting with each further round trip.

That round trip is not limited to the editor edge, as `docs/transforms.md` says. It runs on:

- every `Scene::clone`: play start, Restart, undo snapshots, prefab sync (`Object::clone`, `engine/src/Object.cpp` line 599)
- every scene save (`MeshBvh.cpp` `writeScene`, `SceneWrite.cpp` line 67)
- every inspector rotation edit (`EditorScene.cpp` lines 360–363)
- Motion rest poses and Use tweens (`Play.cpp` lines 184–191, 242–267)
- every tick and every display frame during play (`SimChannel.cpp` `captureSnapshot` and `applySnapshotPoses`)

The demo scene only uses yaw, so nothing looks wrong in it today. The first scene with a tilted, rolled mesh will twist during play and change on save.

The order also no longer matches the old files. `meshRotate` applied yaw first, then pitch, then roll. The quaternion applies roll first. A version-1 scene with more than one rotated axis now loads rotated differently. `docs/transforms.md` says they match. They do not.

### 2. The view writes simulation state

The plan's Phase 1 work item was "simulation state separate from the render scene, with a sync step." What exists instead is one `Scene` that both sides write. `SimSession::applyDisplay` writes interpolated poses into the simulation's `Transform`s. `restoreSimPoses` writes the last snapshot back before the next tick (`SimChannel.cpp` lines 214–230). The header says so plainly (`SimChannel.hpp` lines 88–90).

Consequences:

- The restore goes through the lossy Euler path above, so what the player plays in the editor is not what a headless replay of the same commands produces. The probe shows divergence at the first tick.
- Every rule that reads a pose between `applyDisplay` and the next `take` reads an interpolated, not simulated, value. Today that is the chase camera and the renderer. Tomorrow it is anything someone adds to the frame loop.
- The editor also writes `PlayState` directly: `paused` on Escape, `playerId` on Restart (`editor.cpp` lines 377, 490). Particles, a view-only effect, live in `Scene` and are copied by `clone`.

This is the rule the deferred network depends on. Skipping it is the most expensive thing in this branch.

### 3. A save does not resume

`captureSimPlay` stores transforms and play fields. It does not store the Jolt character: velocity, ground state, contacts. On load, a fresh `PlayState` gets a new `jolt_play::World`, the capsule spawns with zero velocity, and the run diverges. The probe saved in mid-jump and ended 1.62 units off after 60 ticks.

Loading into the editor's live session is worse. `applySimPlay` does not reset `state.physics`, so the old Jolt world keeps the velocity it had before the load. If the saved position is within 0.5 units, `stepPlay` does not even move the capsule (`Play.cpp` lines 612–619).

Also:

- Materials are not saved. A spawner pickup that exists in the save but not in the scene reloads as a grey sphere (`SimSerialize.cpp` lines 425–428).
- A mesh in the save that is not in the current scene is skipped silently (line 430).
- Prefab fields and lights are not saved. A save only works on top of the scene it came from.

The exit criterion "a save in the middle of play loads back to the same state" is not met.

### 4. The replay test cannot fail

`tests/SimReplayTests.cpp` lines 125–135:

- Pose and play-state equality are only checked inside `if (poseMatch && playMatch)`, which is the case where they are already equal.
- The other branch calls `WARN("replay pose/PlayState bits diverged ...")`. doctest's `WARN` evaluates its argument, and a string literal is always true, so it never prints.
- Only the HUD (score, health, message) is required to match.
- The run is 45 ticks, not 10 minutes.

The determinism holds today, and the probe proves it. But the suite would not notice if it broke tomorrow. A test that cannot fail is worse than no test, because it says "covered."

`tests/SimSaveTests.cpp` compares transforms right after load, which only checks that the copy is a copy. It never steps after loading, so it cannot see item 3.

## Major

### 5. Collision quality regressed, and the sync scales badly

- Every play mesh collides as an oriented box from its triangle bounds (`Play.cpp` lines 102–127, `JoltPlay.cpp` lines 563–590). The old engine collided against the triangles. A door frame, an arch, or a tree crown now blocks as a solid box. The reason given is that walk-stairs were more reliable on boxes. That is a settings problem in `CharacterVirtual` (`mWalkStairsMinStepForward`, `mWalkStairsStepForwardTest`), not a reason to drop triangle collision.
- Planes are 400 by 400 boxes. The ground ends 200 units out.
- The whole collision world is rebuilt every tick. For every solid mesh, `syncJoltSolids` copies every triangle into a vector, recomputes the bounds, and `upsertBox` allocates a new Jolt shape before checking whether the body already exists (`JoltPlay.cpp` line 539). Unchanged solids pay that every tick. At 5000 twelve-triangle cubes that is 14.5 ms of a 33 ms tick, with no AI, no scripts, and one player. Real meshes have far more triangles.
- `PhysicsSystem::Update` is never called because "that path crashed with jobs" (`JoltPlay.hpp` line 11). The crash was avoided, not explained. That leaves the enkiTS adapter for Jolt unverified. Kinematic platforms are moved by setting positions directly, and rigid bodies are unavailable.
- If `jobs::init()` was not called, `World` is invalid, and `stepPlay` leaves the player frozen in place with no log line (`Play.cpp` lines 606–627).

### 6. The core still links the FBX SDK

`Object::loadMesh` in `raytracer_core` calls `mesh_import::load`, which lives in `raytracer_import` (`Object.cpp` lines 4 and 636). `raytracer_import` links `raytracer_core`. The two are a cycle, so nothing can link the core without the importer, and the importer is the FBX SDK.

The headless `tests` target links `raytracer_import` and the FBX libraries (`CMakeLists.txt` lines 211–216). `raytracer_render` links `raytracer_import` publicly. The Phase 0 exit was a core with no window, no OpenGL, no ImGui, and no FBX SDK. The last of those is not met.

### 7. EnTT is a storage backend behind the old interface

`Hittable` was deleted, and `Object` took its place with roughly a hundred methods: shape queries, collision, file writing, mesh loading, and the ray intersect.

- Every accessor is two hash lookups: `handles_` or `entities_`, then the registry.
- No system iterates a registry view. Everything walks `scene.objects()`.
- `Scene::create` emplaces eight components on every entity, including a material (`Object.cpp` lines 110–140). It is private, and the only public ways in are `addSphere`, `addPlane`, and `addMesh`. "An entity can exist with no render component" is not true.
- `MeshSockets` and `AudioEmitter` are empty structs. One is commented "stub for Phase 0 exit." That ticks a box, it does not build a feature.

The move to EnTT is worth having. As it stands, it gives none of the benefits that justified it.

### 8. EntityId is not unique across sessions

It is the old per-scene counter, widened to 64 bits. Two scenes both start at 1. The plan asked for ids unique across sessions and saves.

The editor still passes ids through `int` in seven places: `pickGizmo`, `drawGizmo`, `sceneDebugText`, `chooseObject`, and others. It uses `-1` for "none" while the engine uses `0`. That works while ids are small. It breaks the day ids become what the plan asked for.

### 9. The job system has no production use

`jobs::parallelFor`, `runPinned`, and `schedulePinned` are called only from `tests/main.cpp`. No file read is asynchronous. The only engine user, the Jolt adapter, is the path that crashed. Each Jolt job also allocates a `TaskSet` under a mutex (`Jobs.cpp` lines 104–117).

enkiTS is linked and tested, not adopted.

## Minor

- `vcpkg.json` has no `builtin-baseline`, and `vcpkg-configuration.json` sets no baseline. Library versions are whatever the local vcpkg clone holds. Two machines can build different engines.
- Only a Release preset exists, so Jolt's and EnTT's assertions never run.
- Snapshot interpolation lerps three Euler angles instead of slerping a quaternion. `findPose` is a linear search, so interpolation is quadratic in entity count: 9.7 ms per display frame at 5000 objects.
- Non-uniform scale is stored, but `localScale()` returns the mean of the three axes while collision uses the largest. Two definitions of the same value.
- `CommandRecorder` grows without bound. `kBitseryMaxCommands` is 2^20 commands, about 9.7 hours at 30 Hz. A longer session saves a list that the loader rejects.
- A replay must start from a bit-identical scene. Restart builds its round from `playSnapshot.clone()`, which is not bit-identical (item 1), so a replay from the editor scene does not match a restarted round.
- `PlayState` holds a `shared_ptr` to the Jolt world. Copying a `PlayState` silently shares physics between the copies.
- File dialogs are still Win32 `comdlg32` through the SDL window handle, not SDL3's dialogs.
- `Scene::addParticle` writes to `std::cerr` instead of spdlog.

## Docs that do not match the code

- `docs/transforms.md`: "`meshRotate` still applies that Euler order." It does not. See item 1. The same page says Euler degrees exist only at the file and editor edge. The snapshot path uses them every frame.
- `docs/engine-checklist.md`: Jolt is marked **here** while `PhysicsSystem::Update` is not called and every mesh is a box. That is **partial**.
- `docs/engine-checklist.md`: "stable 64-bit ids." Stable within one scene, yes. Unique, no.
- `docs/persistence.md` describes save and load without saying that a load does not resume physics, or that it needs the original scene loaded first.

## Status against the plan

### Phase 0

| Item | Status |
| --- | --- |
| CMake and vcpkg first, sources unchanged | Done. No version baseline |
| `core`, `render`, `audio`, `import` targets | Partial. Core depends on import, so FBX is in every executable |
| `Hittable` replaced by EnTT entities and components | Partial. Storage moved, the interface did not. No shapeless entities |
| 64-bit `EntityId` unique across sessions | Not done. Widened counter |
| GLM transform with quaternion, non-uniform scale, cached matrices, sockets | Partial. Lossy Euler path, mean scale, empty sockets |
| enkiTS pool with async file reads | Not done. No production call |
| SDL3 window and input on OpenGL | Done |
| spdlog, Tracy, doctest | Done, minimally. Three Tracy zones |
| Exit: headless tests load the demo, step it, pass the play self-tests | Met in letter. FBX is linked, and the demo steps 2 ticks |

### Phase 1

| Item | Status |
| --- | --- |
| Tick at 1/30 s, tests updated | Done |
| In-process channel: commands in, snapshot and events out | Done |
| Simulation state separate from the render scene | Not done. One scene, written by both sides |
| Jolt `CharacterVirtual`, `Collision.cpp` removed | Partial. Capsule works. Meshes are boxes, no `Update`, rebuilt every tick |
| Typed events, miniaudio on the view side | Done |
| Command recorder and replay player | Done, and deterministic headless |
| SQLite save of the play session | Partial. Stores and loads. Does not resume |
| Exit: 10-minute bit-for-bit replay | Met headless, by the probe, not by the suite. Not met in the editor path |
| Exit: mid-play save loads to the same state | Not met |
| Exit: the game plays only through the channel | Mostly. The view still writes simulation state |

## Fix before Phase 2, in this order

1. **Rotations.** Make `eulerDegreesFromQuat` the exact inverse of `quatFromEulerDegrees`: decompose in the same order, for example `glm::extractEulerAngleYXZ` on the rotation matrix. Better still, keep authored degrees on an editor-only component and never derive them from the quaternion. Carry quaternions in `SnapshotPose`, and slerp them. Decide what version-1 files mean, then either convert them with the `meshRotate` order or document the break. Add a test with all three axes set that runs through clone, save, load, and a play frame.
2. **Separate view state.** Give the view its own display pose (a render component or a per-entity display buffer) and delete `restoreSimPoses`. Move `paused` out of `PlayState` into the view, or make it a command. Add a test that the editor frame loop and the headless loop produce bit-identical simulation state over 3000 ticks.
3. **Resumable saves.** Store the character's position, velocity, and ground state, and rebuild the Jolt world from the save on every load. Save materials for spawned entities. Fail loudly on a missing mesh. Add a test that saves, loads into a fresh state, runs 300 ticks, and compares bit for bit with the uninterrupted run.
4. **Make the replay test real.** Delete the `WARN` branch and require equality. Run 18000 ticks; the probe did that in a few seconds. Use a scene that scores pickups, fires the goal, and loses to a hazard. Run a second copy through the editor frame path.
5. **Collision sync.** Create bodies once, and update only what moved or what Motion and tweens drive. Cache Jolt shapes per `MeshGeometry`. Put static meshes on `MeshShape`, and tune walk-stairs instead of replacing geometry with boxes. Log and refuse to play when the physics world is invalid.
6. **Jolt jobs.** Find the `PhysicsSystem::Update` crash, or use Jolt's own `JobSystemThreadPool` until the enkiTS adapter is proven. Do not count enkiTS as adopted until a real caller uses it.
7. **Break the core and import cycle.** Register a mesh loader from the executable, or move OBJ parsing into the core and leave only FBX in `import`. The `tests` target must link no FBX library.
8. **Entity ids.** Generate ids that are unique across sessions (random 64-bit, or a session prefix plus a counter), use one invalid value, and remove every `int` id from the editor.
9. **Build hygiene.** Pin a vcpkg baseline. Add a Debug preset, and run the tests in it once per change.

Items 1 to 3 are the cheap ones today. After Phase 2 adds terrain, streaming, and a renderer that read the same transforms and the same scene, they stop being cheap.
