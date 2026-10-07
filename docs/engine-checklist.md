# 3D game engine checklist

A menu of systems a 3D game engine can have. This is not a plan. Pick the lines you want; the others can stay out.

Status is against this project today:

- **here** — usable as it stands
- **partial** — a start exists, and a game would still feel the gap
- **missing** — not in the project

Each section has a longer page. Those pages describe how the current code behaves. When a behavior changes, update the status here and the matching page.

| Topic | Page |
| --- | --- |
| Application loop | [application-loop.md](application-loop.md) |
| World and scenes | [world-and-scenes.md](world-and-scenes.md) |
| Transforms | [transforms.md](transforms.md) |
| Assets | [assets.md](assets.md) |
| Rendering | [rendering.md](rendering.md) |
| Materials and shading | [materials.md](materials.md) |
| Lighting | [lighting.md](lighting.md) |
| Cameras | [cameras.md](cameras.md) |
| Animation | [animation.md](animation.md) |
| Collision | [collision.md](collision.md) |
| Physics | [physics.md](physics.md) |
| Gameplay objects | [gameplay-objects.md](gameplay-objects.md) |
| Input | [input.md](input.md) |
| Game GUI | [game-gui.md](game-gui.md) |
| Editor GUI | [editor-gui.md](editor-gui.md) |
| Audio | [audio.md](audio.md) |
| Scripting and game rules | [scripting.md](scripting.md) |
| AI and navigation | [ai-and-navigation.md](ai-and-navigation.md) |
| Effects and juice | [effects.md](effects.md) |
| Persistence | [persistence.md](persistence.md) |
| Terrain and world layout | [terrain.md](terrain.md) |
| Streaming | [streaming.md](streaming.md) |
| Debug and performance | [debug-and-performance.md](debug-and-performance.md) |
| Build, platform, and content | [build-and-platform.md](build-and-platform.md) |

The **primary interactive view** is clustered forward on Diligent (D3D12 on Windows, Vulkan for Linux bring-up): `GfxDevice` / `GfxView` / `GfxCluster` / `GfxFx`, HLSL under `engine/shaders/`. The Whitted OpenGL path (`GpuRayTracer`) remains for progressive stills and `--self-test` on Windows only; GL `GpuLimits` are not raised. CMake + FetchContent builds the Diligent path; see [build-and-platform.md](build-and-platform.md). The editor places spheres, planes, and meshes, saves a version-1 scene, and can play. `raytracer` / `raytracer.exe --game` opens the Diligent view when built with `RAYTRACER_DILIGENT`.

## 1. Application loop

The first version is in place. The lines below it are later work.

- **here** — Window, swap, and an editor frame
- **here** — Play and stop inside the editor, with the scene restored on stop
- **here** — Fixed simulation step (play mode uses 1/30 s)
- **here** — Channel: `SimSession` queues `Command`, ticks through `take` / `step`, publishes `Snapshot` and `SimEvent`. Editor Play and `--game` consume commands; they do not call `stepPlay` from the window loop. One EnTT `Scene` is still mutated by the tick; the view copies interpolated snapshot poses onto it for display
- **here** — The window presents every frame. The simulation consumes 1/30 s steps on that same thread, then one render runs if the view changed
- **here** — Pause, one-step while paused, and a speed scale from 0.25× to 2×
- **here** — `raytracer.exe --game` starts in play and hides the editor
- **here** — Startup shows one Loading frame, then loads the scene and the GPU. Shutdown releases them in reverse
- **here** — At most four simulation steps per window frame. A longer hitch drops the leftover time
- **partial** — enkiTS worker pool behind `jobs::` (`Jobs.hpp` / `Jobs.cpp`): process init/shutdown from `main`, `parallelFor`, and pinned tasks. OBJ mesh file reads call `jobs::runPinned` in `MeshObj.cpp`. `TileStream` schedules tile reads and decode/mesh builds with `schedulePinned`. Play and the window still run on one thread; a long `stepPlay` still blocks the frame
- **missing** — A second thread so a long simulation step does not block the window
- **here** — Play timers (`motionTime`, spawn clock, Use tween time) are tick counts at 1/30 s. Scene/settings durations stay in seconds and convert with `playTicksFromSeconds`
- **here** — Simulation RNG on `PlayState` (`simSeed` default 1, `std::mt19937 simRng`). Same seed after a clear. No `rand()`. Play does not draw from it yet
- **here** — Interpolation between the last two snapshots when the display is faster than 30 Hz. Alpha is the play accumulator over `kPlayStep`. HUD uses the latest snapshot
- **missing** — A choice of vsync or an unlocked frame cap
- **missing** — Loading progress for a large scene
- **missing** — Keeping the dropped hitch time for a deterministic replay

## 2. World and scenes

- **here** — Scene file version 2 persists 64-bit entity ids and `next_id`; version 1 still loads with sequential ids
- **here** — Named EnTT entities with 64-bit ids unique across sessions (random 32-bit session prefix plus counter; 0 is invalid). Loading a scene file keeps the session prefix for newly issued ids. Play restore adopts the saved prefix via `restoreIdState`. Stable ordered iteration. GPU upload assigns a compact per-frame int for shader ids (including emissive light skip); debug dump and bounce skip use `EntityId`
- **partial** — One scene in memory
- **partial** — Two demo rooms. The goal in room 1 advances to room 2 and the round continues. The goal in room 2 wins. Stop asks whether to keep the scene or restore the play snapshot. The demo does not stream tiles
- **partial** — Prefabs. Make prefab marks the selection. Add → Prefab places a copy, including children. Editing the source updates every placement except the copy's position. There is no prefab file separate from the scene
- **partial** — One string tag per object. The file still stores that word. `makeRole` builds a `Role` beside the shape (`Solid`, `Player`, `Pickup`, `Trigger`, `Use`, `Goal`, `Hazard`, `SpawnPoint`, `Spawner`). Play dispatches on `RoleKind`. A layer is a separate integer: 0 or 1
- **partial** — A pickup can be removed during play, and a spawner can add one while the round is running. Discard on Stop restores the snapshot. Keep leaves the scene as it is
- **partial** — Streaming a large level in pieces: `TileStream` loads/unloads `.rtt` tiles in a Chebyshev ring (`docs/streaming.md`). Sim layer uses `ensureReady` inside `stepPlay` (deterministic wait on the focus ring; tile ids from coordinates; terrain saved as `SimShapeKind::Terrain`). Render layer pumps on the view side with an apply time budget. Decode/mesh run off the main thread; one LOD per tile; unload hysteresis. Off unless a stream is set. The demo does not enable it
- **here** — Undo and redo in the editor, 32 deep, while not playing. Play does not record, and undo does not restore play-mode simulation

## 3. Transforms

- **here** — One GLM `Transform` component on every entity, with quaternion rotation and cached local/world matrices
- **here** — Shared mesh geometry with a transform per placement
- **here** — Parent and child hierarchy. Sphere, plane, and mesh children all inherit parent translation, rotation, and scale
- **partial** — Non-uniform scale is stored on `Transform::scale` and snapshots; `localScale()` and collision use the largest axis; the inspector still edits one number
- **partial** — A single transform type for scene entities; lights and cameras remain separate records
- **here** — Cached local and world matrices
- **here** — Play interpolates local `SnapshotPose` (position, quaternion slerp, `dvec3` scale) onto `DisplayTransform`. Simulation `Transform` is not written by the view
- **missing** — Pivots and attachment points (hand, muzzle, socket)

## 4. Assets

- **here** — OBJ and FBX meshes, static, triangulated, one material per file
- **here** — Image textures, normal maps, environment maps
- **partial** — Load from a path stored in the scene
- **here** — An asset list of `assets/`. Add mesh, albedo, normal, and the environment map use the selected file. A changed mesh, albedo, normal, or environment file reloads on a check that runs at most four times a second
- **here** — Kenney Nature Kit 2.1 (CC0) in `assets/kenney-nature-kit/Models/OBJ/` (329 vertex-colored meshes). Not wired into `createDemoScene` yet
- **partial** — Cooked mesh format RTM1 (`MeshCooked.hpp`): shared float vertices + uint32 indices, materials/submeshes, zstd, magic `RTM1`, payload v4 (v1–3 still decode). Runtime `MeshGeometry` is indexed; BVH triangles are derived. `loadMeshFile` mirrors sources under `cooked/` and checks the `.d` sidecar; shipping builds refuse source formats (`RAYTRACER_SHIPPING`). GPU still draws one placement material. `cooker.exe` uses Assimp + meshoptimizer + SHA-256 `.d` sidecars
- **partial** — Mesh LODs: cooker `meshopt_simplify` 50%/25% stored as shared-vertex index lists on `MeshGeometry::extraLods`; GPU/play still draw lod 0
- **here** — PhysFS mounts `cooked/` then `cooked/pak0.zip`; `cooker pack` STORE zip; `cooker manifest` SHA-256 `manifest.txt`
- **here** — DirectXTex DDS cook in `raytracer_cook` only; runtime `loadDdsFile` keeps BC mips; GL uploads compressed when layers match
- **here** — FlatBuffers catalog (`schemas/content.fbs`, `Catalog.cpp`); `cooker tables` via `flatc -b`; `contentInit` loads `cooked/catalog.bin`
- **partial** — Texture compression and mipmaps: cooker writes BC+mips; runtime uploads BC with mips when possible, else RGBA8 scale to `kAlbedoEdge`. HDR cook is missing
- **partial** — The editor warns after a GPU cap is passed (256 placements, 262144 unique triangles, 64 spheres, 16 planes, 8 lights, 16 textures). It also warns when GLSL job stack, trace limit, or mesh stack settings look too low for the scene (BVH depth, glass). It does not show remaining room beforehand

## 5. Rendering

- **here** — Primary mesh raster, ray-traced spheres and planes, mesh reflections through the BVH, shadow maps
- **here** — One shading shader, progressive samples for still images, one sample while playing
- **here** — Reinhard tone mapping on the display path, linear output for tests
- **here** — Depth of field when aperture is above zero
- **missing** — Skybox or skydome as a real object, not only a gradient plus an environment map
- **partial** — Distance fog: a color and a density on the scene. Density 0 is off and is omitted from the file. No volumetric light
- **here** — Glass transmission traces meshes. A floor reflectivity of 0.14 still does not
- **missing** — Decals and projected textures
- **partial** — Particles: eight gold discs on `SimEventKind::Pickup` and eight red discs on `Effect`, spawned in `playSimEvents`, advanced on the window frame. They live in the shading shader (cap 16 on GPU). Spawn cap is `maxParticles * particleDensity` (clamped 1..256). No trails
- **missing** — Skeletal meshes and morph targets
- **here** — Instanced mesh raster on the GL path: unique local vertices once per geometry, `glDrawElementsInstanced` per shared mesh (`GpuRaster.cpp`). Shadow maps use the same instance buffer
- **partial** — Instanced opaque mesh draw on the Diligent path (`GfxView`); CPU frustum cull of mesh instances for the GL color pass (`Frustum.hpp`). View distance drops spheres and meshes from the GL GPU upload. Occlusion and portal culling are not built; Diligent frustum cull is not wired yet
- **partial** — Diligent clustered forward is the primary interactive look when `RAYTRACER_DILIGENT` is on; GL Whitted remains for stills / self-test and for non-Diligent builds
- **partial** — Bloom: DiligentFX via `GfxFx` + quality settings on the Diligent path; GL still bloom (bright-pass blur after tone mapping, skipped when linear output is on). No color grading, vignette, motion blur, or extra anti-aliasing
- **missing** — Render layers (world, effects, UI)
- **missing** — Screenshot and video capture as a feature, not only Save PNG

## 6. Materials and shading

- **here** — Albedo, ambient, diffuse, specular, shininess, roughness, reflectivity, transmission, index of refraction, emission
- **here** — Albedo image and normal map
- **missing** — Material instances that share a shader and override a few values
- **missing** — Masks, detail maps, emissive maps
- **missing** — A material editor that is not a list of sliders
- **missing** — Shader variants or a second shading model (toon, unlit)

## 7. Lighting

- **here** — Ambient, point lights, directional lights, radius and softness
- **here** — Emissive surfaces light themselves and nearby surfaces as extra point lights, up to the 8-light cap. Mesh shadow maps block that light. The glowing mesh is left out of its own shadow map
- **here** — Spot lights: a direction plus inner and outer half-angles in degrees. Outer angle 0 is not a spot. No cookies
- **partial** — One light bounce off a sphere whose reflectivity is above 0.35 and whose transmission is 0, on the camera's first hit only. Up to 8 spheres. The mirror the surface faces, among the first four, can send that light on from each of the others. The solver stops once the reflection matches. The bounce is the light's intensity faded from the last bounce point to the surface, times each sphere's albedo and reflectivity, and it is kept only when the reflected ray hits the surface. An object between bounce points leaves a shadow that moves with the spheres. The mirrors and the emissive object that made the light are not blockers of that bounce. Planes, meshes, and later reflection rays do not bounce light. Settings → Mirror bounces writes `mirrors` in `raytracer-settings.txt`. Off uploads `uMirrorCount` 0. A file without that line stays on
- **missing** — Light cookies and colored shadows
- **missing** — Baked lightmaps
- **partial** — Diligent path: clustered light lists (no 8-light rank). GL still path caps at 8; past that, `rankGpuLights` keeps directional lights and the strongest remaining by `intensity / (1 + falloff * distance²)` to the camera
- **partial** — DiligentFX atmospheric sky wrapper (`GfxFx`); no full day/night clock yet

## 8. Cameras

- **here** — Editor orbit, pan, zoom, field of view, aperture, focus
- **here** — A chase camera while playing, when a player sphere exists
- **here** — The chase camera stops short of the first solid along the line from the player
- **partial** — First person exists as a C toggle during play. A fixed camera and a rail stay missing
- **here** — Named cameras saved with the scene. V steps through them during play, then back to chase. The step blends for 0.35 seconds
- **partial** — A cut is that V step. It blends instead of swapping at once. There is still no editor cut track
- **missing** — Split screen

## 9. Animation

- **here** — Transform animation (move, rotate, and scale ping-pong from a rest pose)
- **missing** — Skeletal clips, blending, and a state machine
- **missing** — Root motion
- **missing** — Inverse kinematics
- **missing** — Vertex animation and shape keys
- **partial** — UV scroll: a material has horizontal and vertical speeds in UV units per second. No other material animation
- **missing** — Timeline for one-shot sequences

## 10. Collision

Separate from a physics engine. The player controller is the part that exists.

- **here** — Capsule `CharacterVirtual` in play (`jolt_play::World`). Sphere overlap remains for pickups; the unused sphere solver is gone
- **here** — Mesh triangles through the BVH for the ray tracer, `castPlayRay`, and overlap. Play movement collides with a welded Jolt `MeshShape` (`contentHash` cache, non-uniform `ScaledShape`)
- **partial** — Layer 0 blocks the player and the chase camera. Layer 1 blocks the player and lets the camera through. The demo door is layer 1
- **partial** — Overlap for pickups, triggers, the goal, Use, and hazards
- **partial** — `castPlayRay` from the player along the camera look, up to 3 units, and the chase-camera ray. Use and the goal answer that look when the player is not already overlapping them. No sphere or box cast
- **here** — CharacterVirtual: slide, ground, jump, walk-stairs 0.35, kinematic platform carry
- **partial** — Picking in the editor is a ray against the scene, not a game query

## 11. Physics

- **here** — Gravity and a jump impulse on the player capsule (`CharacterVirtual`)
- **missing** — Rigid bodies, mass, friction, restitution
- **partial** — A `platform` tag is a kinematic Jolt box the player does not push. Ground velocity carries a standing player. No other kinematic body
- **missing** — Joints: hinge, spring, fixed
- **missing** — Continuous collision so fast objects do not tunnel
- **missing** — Sleeping bodies
- **missing** — Vehicles, cloth, soft bodies, destruction
- **here** — Jolt (`JPH_DOUBLE_PRECISION`, `JobSystemThreadPool` via `jobs::joltJobSystem`). `stepPlay` drives `CharacterVirtual` through `jolt_play::World` and calls `PhysicsSystem::Update`. `Collision.cpp` is removed

A first game can ship with collision and a character controller and never take a rigid-body solver.

## 12. Gameplay objects

- **here** — Scene owns EnTT entities; `Object` is a non-owning `{Scene*, EntityId}` handle
- **partial** — Entities can carry metadata independently, though factories currently create one render shape
- **partial** — Transform, shape, material, identity, role, motion, action, prefab, spawner, and audio-stub components
- **missing** — Prefab variants
- **partial** — A spawner adds a pickup on a timer during play. Discard on Stop removes it with the snapshot. Keep leaves it
- **here** — A player sphere, the first object tagged `player`, distinct from the editor selection
- **partial** — Score, health, a HUD message, and a win or loss result. A pickup adds 1 health up to 3. No inventory
- **here** — Pickup on overlap, a trigger message, Use on F, a goal that wins, and a hazard or a fall that loses. A `spawn` tag turns that loss into a respawn. No timer rule besides a spawner interval

## 13. Input

- **here** — Keyboard in play mode, AZERTY ZQSD, Space or E to jump, Esc to pause
- **here** — Mouse look while playing, and orbit, pan, zoom, and picking while editing
- **here** — `PlayInput` is move, jump, and use. `readPlayInput` plus the camera look fill a `Command` (`tick`, `look`) that `SimSession` queues. Settings lists the keys and writes them to `raytracer-settings.txt`
- **here** — Rebinding for forward, back, left, right, jump, a second jump, and use
- **partial** — First connected SDL3 gamepad: left stick move, right stick look, South jump, West use. No rebinding, no second pad
- **missing** — Several local players

## 14. Game GUI

This is the HUD and menus, not the editor.

- **partial** — `--game` opens a title with Play, Volume, and Quit. Pause still has Resume, Step, Speed, and Quit. A win or loss offers Restart. No settings page in the game window
- **partial** — HUD text for score, the last message, or "No player"
- **partial** — ImGui follows the window. There is no layout system of our own
- **missing** — Fonts and a way to show text without ImGui
- **missing** — Buttons, sliders, lists, and focus for keyboard or gamepad
- **missing** — Drag and drop, inventory grids
- **partial** — The Use key is drawn at the play-ray hit on a `use` or `goal`. No nameplates
- **missing** — Localization, at least French and English strings
- **partial** — The cursor is hidden and the mouse is relative/grabbed during play, and shown again while editing, paused, or when the round is over. There is no separate free mode during play

## 15. Editor GUI

- **here** — ImGui: outliner, inspector, add, delete, duplicate, save, load
- **here** — Play and stop, with the scene restored on stop
- **partial** — Move, rotate, and scale gizmos in the view. Move drags any object along a world axis. Rotate turns a mesh's Euler angles or a plane normal. Scale changes a mesh scale or a sphere radius
- **partial** — Hold Ctrl and a move or a position slider lands on the Snap spacing. Settings → Save settings writes that spacing into `raytracer-settings.txt`
- **partial** — Multi-select. Shift-click adds objects. Move drags the group. Rotate, scale, and groups of lights stay on one object
- **here** — Search in the outliner. A box filters objects and lights by case-insensitive name substring. Empty shows every row. A hidden selection stays selected and is drawn again when the box is cleared.
- **missing** — Console for errors and logs
- **missing** — Play-mode tweaks that can be kept or discarded on purpose
- **missing** — A layout you can dock and save

## 16. Audio

- **partial** — Pickup, win, and beep load `assets/pickup.wav`, `assets/win.wav`, and `assets/beep.wav` when those files exist. A generated tone is the fallback. `playGameSound` plays them with miniaudio. Master volume is a slider written by Settings → Save settings. Buses and variation stay missing
- **partial** — One looping music WAV during play (`startMusic` / `ma_sound` on `assets/music.wav`), plus the three one-shots. No second effects clip and no music bus separate from master volume
- **partial** — One master volume on the miniaudio music `ma_sound` and as a scale on each one-shot. No music or effects buses
- **here** — The listener is the camera. Pickup, win, and beep fade linearly (`soundDistanceFade`) and are silent at 16 units. Music stays at master volume. miniaudio spatialization is off
- **missing** — Random variation so a footstep is not one file forever

## 17. Scripting and game rules

- **here** — C++ gameplay lives in `stepPlay`. Each tick emits `SimEvent` (Pickup, Win, Sound, Effect). The editor plays clips and particle bursts from those kinds, not by comparing `PlayState::message`. Message text remains for the HUD
- **missing** — A small data language for rules (trigger, timer, key) if C++ is too heavy for every idea
- **missing** — Hot reload
- **missing** — A visual script graph

For this project, a few C++ components plus scene tags will go further than embedding a language.

## 18. AI and navigation

- **missing** — Navmesh or a grid
- **missing** — Pathfinding
- **missing** — Steering: seek, flee, arrive
- **missing** — A behavior tree or a state machine
- **missing** — Perception: sight cone, hearing
- **missing** — Squads, cover, combat

## 19. Effects and juice

- **missing** — Screen shake, hit pause, camera punch
- **here** — Particle bursts: gold on pickup, red on hazard/fall, from `SimEvent` in `playSimEvents` (8 discs, life 0.45 s). Not spawned in `stepPlay`
- **partial** — Up to `max_tweens` Use tweens (default 8). Each moves and rotates a named object over `action_duration` (default 0.4 s, 12 ticks at 30 Hz) and then leaves it there. There is no timeline
- **missing** — Trails and simple decals
- **missing** — Rumble

## 20. Persistence

- **here** — Save and load the edited scene
- **here** — bitsery encode/decode of `Command`, `Snapshot`, `SimEvent`, `SimEntityRecord`, and `SimPlayBlob` (transforms as quats, materials, shapes, motion, HUD/tick/seed/rng, Jolt capsule feet/velocity/radius/grounded, `CharacterVirtual` and `PhysicsSystem` SaveState bytes). Mesh soup stays on disk; the blob keeps `sourcePath`. `applySimPlay` validates every mesh path before mutating the scene, then fails without changes if a file is missing
- **here** — SQLite play-session save/load (`SimSave.hpp`): one file `raytracer-play.sqlite` in the working directory (same place as `raytracer-settings.txt`), WAL, one IMMEDIATE transaction. `world` holds tick, seed, `encodeSimPlayBlob`, and `encodeCommandList`; `entity` / `character` rows are keyed by `EntityId` with bitsery blobs. Load rebuilds the Jolt world and restores physics plus character state. File → Save/Load play session while playing; `tests.exe` saves mid-jump and compares 300 further ticks to the uninterrupted run
- **missing** — Slots, autosave, and a save-file version separate from `kPlaySessionDbVersion` (currently 1)
- **here** — `raytracer-settings.txt` stores master volume, render size, samples, bounces, the play keys, and every `EngineSettings` key (play, GPU stacks, bloom, view distance, shadow map size, particle density, frustum, snap, mirrors). The editor loads that file at startup and Settings edits the values. A numeric value that does not parse is skipped, and `bounces 0` still loads. A missing `mirrors` line stays on. There is no second bindings page

## 21. Debug and performance

- **here** — Self-test for the renderer, the scene file, and the play step
- **partial** — Frame time in the editor. Debug dump writes `raytracer-debug.txt` with objects, lights, camera, the point-light shadow fit, and mirror-bounce rays. Bounce rays can be drawn on the view
- **partial** — spdlog console + `raytracer.log` via `logging::init` / `logging::shutdown`. Tracy `EditorFrame` / `FrameMark` and `stepPlay` / `SimSession::take` zones when linked (no capture required). Headless `tests.exe` runs doctest + `runPlaySelfTests` through `SimSession::step`. No on-screen profiler
- **missing** — On-screen profiler (CPU, GPU, draw, simulation)
- **here** — Colliders draws sphere circles, mesh boxes, plane patches, each scene light's bound, and a frustum for each named camera other than the view
- **missing** — A console command line
- **missing** — Crash logs
- **here** — Command recorder on `SimSession::enqueue` and tick-by-tick replay (`SimReplay.hpp`). Hitch leftover stays dropped; replay is the command list, not wall clock. `tests.exe` requires bit-identical poses over 18000 ticks, an editor-display path over 3000 ticks, and separate replay cases for pickup score, goal win, and hazard loss

## 22. Build, platform, and content

- **here** — Windows CMake presets (`windows-msvc` Release, `windows-msvc-debug` Debug), MSVC 2022, C++20, vcpkg manifest with `builtin-baseline` and the same git baseline in `vcpkg-configuration.json`, FBX SDK, OpenGL 3.3; `build.bat` builds `raytracer.exe`
- **here** — CMake splits core, render, audio, import, and cook libraries; EnTT and GLM are linked in core. Headless `tests` links core only (OBJ, `.rtm`, DDS, catalog, PhysFS, terrain `.rtt`, `TileStream`; no Assimp/FBX). `cooker.exe` links Assimp and meshoptimizer
- **here** — SDL3 window, OpenGL 3.3 context, and input on `raytracer` only; `tests` does not link SDL
- **here** — Phase 1 vcpkg: bitsery, sqlite3, Jolt (`JPH_DOUBLE_PRECISION` overlay feature), miniaudio; Jolt `JobSystemThreadPool` (not the enkiTS adapter). Phase 2 assets: assimp, meshoptimizer, zstd, physfs, directxtex, flatbuffers. `Sound.cpp` plays through miniaudio (no winmm). Play movement is Jolt CharacterVirtual
- **missing** — A packaged folder a friend can run without the repo
- **missing** — Asset paths that still work after that package
- **missing** — Another OS
- **missing** — Installer or a single zip with the exe, assets, and the runtime DLLs
- **here** — Kenney Nature Kit 2.1 (CC0) under `assets/kenney-nature-kit/` with `License.txt`. Autodesk FBX SDK remains a separate Autodesk license on `raytracer_import`

## 23. Terrain and world layout

- **partial** — Heightmap tiles, 65 samples (2^n+1) on 64 units, `RTT1` (`Terrain.hpp` / `Terrain.cpp`), LOD skirts. `cooker terrain` writes a procedural hill. `loadTerrain` builds a mesh for rays/GPU. Neighbour LOD stitching beyond skirts is not built
- **partial** — Chunked LOD at stride 1/2/4 on `MeshGeometry::extraLods` with edge skirts; sample count 65 (2^n+1) so LODs reach the tile edge; draw and rays use stride 1
- **here** — Per-sample holes; Jolt `HeightFieldShape` uses `cNoCollisionValue`; the mesh skips hole quads
- **partial** — Four splat weight layers stored on the tile; the current shader does not blend them
- **partial** — Liquid volumes (`water` / `lava`, AABB + surface height) on the scene; no swim and no water draw
- **here** — Map record: id, name, continent or dungeon, tile size, tile counts. One map in the live scene
- **missing** — Interior cells, portals, scatter brushes, or editor sculpting
- **here** — Play upserts a heightfield for each terrain object (`JoltPlay.cpp`). Headless tests stand on a hill and fall through a hole

## 24. Streaming

- **here** — Load and unload terrain tiles around a focus in a Chebyshev ring; look-ahead is two seconds of 3× walk speed (`streamTileRadius`); unload hysteresis is load radius + 1
- **here** — `StreamLayer::Sim` skips the render mesh so a server path can load collision only; `Render` builds only the needed LOD
- **partial** — Mesh LOD by ring distance (build one of lod 0/1/2). Texture mip streaming is missing
- **here** — Tile count and byte budgets evict the farthest tile first. At most two IO starts per `pump`. `setApplyBudgetMs` caps apply work (default 4 ms)
- **here** — Async `.rtt` reads and decode/mesh builds through `jobs::schedulePinned`; `contentRead` is mutexed
- **missing** — Streaming the demo/editor camera or navmesh tiles

## Next

Nothing is queued. The last four items are in the editor.

## First cut

The first playable room is in the project:

1. Tags and a player object
2. Collision primitives and a character controller
3. A chase camera that follows the player
4. Mouse look, and move / jump input
5. HUD text and a pause menu
6. One beep when the score increases
7. Pickup and goal triggers
8. `raytracer.exe --game`, which hides the editor

Physics, skeletal animation, navigation, and networking can wait until a game actually needs them.
