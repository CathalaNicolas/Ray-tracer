# Engine plan for a World of Warcraft–like game

This is a plan, not a description of the code. It lists the engine work between this project today and a large persistent world in the style of World of Warcraft: one large continent, many characters on screen, and a world that persists.

Networking is deferred. The game runs in one process for now. Phase 1 builds the simulation as if a server ran it, so that the network can be added later without rewriting the simulation. See [Deferred: networking](#deferred-networking).

Out of scope: game UI, quests, classes, combat rules, economy, and other gameplay. Those sit on top of what is listed here. The editor appears only where world content cannot be built without it.

Current status comes from `docs/engine-checklist.md` and the pages it links. Numbers marked **target** are goals to measure against, not facts about the code.

## What the game asks of an engine

- A seamless outdoor continent, several kilometers on a side, walked without loading screens.
- Interiors (cities, caves, buildings) inside that continent, and separate instanced maps for dungeons.
- Hundreds of animated characters in view, each with equipment attached.
- One simulation that owns the truth for thousands of entities, and a view that only displays it.
- Characters and world state that survive a crash, a quit, and a patch.
- Content shipped as packed data that a patch can update.

## Where the engine stands

| Area | Today | Gap for this game |
| --- | --- | --- |
| Object model | `Hittable` is one class that renders (`contributeGpu`), collides (`contactSphere`), saves (`writeScene`), and carries the tag | No entities, no components, no invisible objects |
| World | One `Scene` in memory, a text file at version 1 | No terrain, tiles, streaming, or second map |
| Scale | GPU caps of 256 placements, 262144 triangles, 64 spheres, 16 planes, 8 lights, 16 textures | A single town is past those caps |
| Renderer | One GLSL 330 fragment shader. Spheres and planes ray traced, meshes rasterized per placement | No culling, LOD, instancing, or full-scene raster |
| Transforms | Uniform scale, Euler angles, parenting that differs by kind | No shared transform type, no sockets |
| Animation | Transform ping-pong from a rest pose | No skeletons |
| Assets | OBJ and FBX parsed at load. `Mesh.cpp` links the FBX SDK | No cooked format, no archives, no streaming |
| Collision | One player sphere against spheres, planes, and mesh triangles through the BVH | No capsule, terrain, or water |
| Simulation | `stepPlay` at 1/60 s on the window thread, at most four steps per frame. It takes `PlayInput` and the camera direction as arguments and edits the rendered `Scene` in place | No threads, no headless run, no separate simulation state |
| Events | The editor picks a sound by comparing `PlayState::message` to strings such as `"You win"` and by watching the score | No typed events out of the simulation |
| Persistence | Scene file and `raytracer-settings.txt` | No play-session save |
| AI | Nothing moves but the player | No navmesh or path |
| Audio | WAV one-shots and one looping music file | No streaming, no zone ambience |
| Build | Windows, MSVC, OpenGL 3.3, run from the repo | No package or patch |
| Network | Nothing | Deferred |

What carries over: the fixed simulation step, `PlayInput` as the only way input enters play, double-precision `Vec3`, the play rules and their self-tests, stable object ids, prefab sharing, the ImGui editor panels, and the settings file.

## Decisions

A library comes first when one does the job. Each one sits behind an engine interface narrow enough to swap it, and each has a written condition for dropping it. Our own code is kept for what no library covers and for the shape of the engine itself.

Every library below is permissive (MIT, BSD, zlib, Apache 2.0, or public domain). None of them requires releasing our source.

### Build: CMake, vcpkg, C++20

The Makefile lists every object file by hand, and its non-Windows branch is stale. It will not carry twenty libraries.

- CMake with presets, MSVC 2022, C++20. `build.bat` calls the preset.
- vcpkg in manifest mode (`vcpkg.json`) for every library that has a port. CMake `FetchContent` for the rest, pinned to a commit.
- Targets: `core` (static library, no window), `render`, `audio`, `game` (replaces `raytracer.exe --game`), `editor`, `cooker`, and `tests` (headless).

### Renderer: clustered forward raster

Clustered forward (forward+) is the primary path. A WoW-like world is opaque terrain, alpha-tested foliage, and a lot of transparent water and effects. Forward handles transparency and MSAA in the same pass and does not need a G-buffer per material.

The current OpenGL ray tracer stays as the editor's still renderer until the raster path draws the demo room. It is not ported. Ray-traced reflections can come back later as an optional pass on the new API.

### Graphics API: Diligent Engine on Direct3D 12

- [Diligent Engine](https://github.com/DiligentGraphics/DiligentEngine) (Apache 2.0): one API over Direct3D 11, Direct3D 12, Vulkan, and OpenGL. HLSL shaders for every backend. Bindless resources, compute, indirect draws, and ray tracing.
- DiligentFX on top: PBR shading, shadow-map cascades, post effects (bloom, TAA, SSAO), and atmospheric scattering for the sky. We do not write those.
- Direct3D 12 is the backend on Windows. Vulkan is there for a later Linux build.
- bgfx was the lighter choice. It has no bindless resources and no ray tracing, so it would be the one to outgrow.
- Drop when: a frame profile shows Diligent's CPU overhead as the largest render cost, or a needed feature is blocked. The fallback is NVRHI or direct Vulkan behind the same render interface.

### Window and input: SDL3

[SDL3](https://github.com/libsdl-org/SDL) (zlib): window, keyboard, mouse, gamepad, high-DPI, and file dialogs. It replaces the Win32 window code and fills the gamepad line. Diligent takes the native handle. ImGui has an SDL3 backend, and Diligent ships an ImGui renderer.

### World coordinates: double in the simulation, camera-relative on the GPU

`Vec3` is already double. Simulation positions stay double, and Jolt is built with `JPH_DOUBLE_PRECISION`. The renderer subtracts the camera position on the CPU and uploads floats, so precision is centered on the viewer. Terrain and navmesh tiles store floats relative to the tile origin. No origin rebasing.

### Simulation: one fixed tick at 30 Hz

One rate keeps replays and saves simple. 30 Hz is half the cost of 60 per creature, and the view's interpolation hides the difference. Distant entities update every second, fourth, or eighth tick through simulation LOD. Up to 33 ms of input latency is fine for a tab-target RPG.

If the player controller feels late, run the local player's controller in substeps. Do not raise the whole world.

### Entities: EnTT

[EnTT](https://github.com/skypjack/entt) (MIT): sparse-set entities and components, views, signals, and a snapshot API. The runtime handle is `entt::entity`, a 32-bit index with a version, never a pointer. A separate 64-bit `EntityId` component is what saves, scripts, and a later network refer to, with a map back to the handle.

flecs is the alternative if relationship queries become the bottleneck.

### Jobs: enkiTS

[enkiTS](https://github.com/dougbinks/enkiTS) (zlib): task sets, pinned tasks for the main and IO threads, and priorities. Jolt's job interface and navmesh queries run on it.

### Math: GLM

[GLM](https://github.com/g-truc/glm) (MIT): `dvec3`, `dquat`, and `dmat4` in the simulation, float types for the GPU. `Vec3` stays in the old code until that code moves.

### Collision and movement: Jolt Physics

[Jolt](https://github.com/jrouwe/JoltPhysics) (MIT):

- `CharacterVirtual`: a capsule controller with slopes, steps, and moving platforms.
- A heightfield shape for terrain, mesh shapes for buildings and props.
- Ray casts and shape casts for line of sight and Use.
- Rigid bodies when a design needs falling rubble or a swinging trap.
- The same inputs give the same result on the same build, which the replay check needs.

It replaces `Collision.cpp` and the sphere controller. The mesh BVH stays for the ray tracer only.

### Navigation: Recast and Detour

[Recast Navigation](https://github.com/recastnavigation/recastnavigation) (zlib): a tiled navmesh built in the cooker from the same collision geometry, Detour for path queries, and DetourCrowd for local avoidance.

### Animation: ozz-animation

[ozz-animation](https://github.com/guillaumeblanc/ozz-animation) (MIT): sampling, blending, additive and masked layers, and two-bone and aim IK for feet and heads. Skinning runs in our own shader. The cooker fills ozz's raw skeleton and clip types from the imported file. The animation state graph above it is ours.

### Import and cooking

- [Assimp](https://github.com/assimp/assimp) (BSD-3) reads FBX, OBJ, and glTF in the cooker. It retires the FBX SDK, which is proprietary and is the only dependency that blocks a clean package. glTF 2.0 from Blender is the source format for new content. Keep the FBX SDK in the cooker only if Assimp mis-imports a skinned FBX.
- [meshoptimizer](https://github.com/zeux/meshoptimizer) (MIT): LOD simplification, vertex-cache and overdraw order.
- [DirectXTex](https://github.com/microsoft/DirectXTex) (MIT): mips, BC1–BC7 compression, and DDS files. Diligent loads DDS at runtime.
- [FlatBuffers](https://github.com/google/flatbuffers) (Apache 2.0): cooked asset headers and static data tables. Authors write JSON, and `flatc` turns it into binary against the schema. Reads need no parse, and a new field does not break old files.
- [zstd](https://github.com/facebook/zstd) (BSD): compression for cooked meshes, data, and saves. Textures stay BC-compressed.
- [PhysFS](https://github.com/icculus/physfs) (zlib): mounts zip archives and loose folders in order. A patch archive mounted later overrides the base archive. Drop when: load profiling shows archive reads as the stall. The replacement is our own pack format with content hashes, behind the same file interface.

### Saves and replays: bitsery and SQLite

- [bitsery](https://github.com/fraillt/bitsery) (MIT): versioned binary serialization of components. One code path for save records, replays, and a later network packet.
- [SQLite](https://sqlite.org) (public domain): the save is one SQLite file in WAL mode. Tables by owner (character, map, world), rows keyed by `EntityId`, each holding a bitsery blob. A transaction makes a crash leave either the old save or the new one. The same tables can move to PostgreSQL when a server exists.

### Scripting: Luau

[Luau](https://github.com/luau-lang/luau) (MIT): Lua syntax, a sandbox by design, an interrupt callback for instruction limits, and optional types that catch content mistakes before they run. Bindings through [LuaBridge3](https://github.com/kunitoki/LuaBridge3) (MIT). The fallback is Lua 5.4 with sol2.

### Audio: miniaudio

[miniaudio](https://github.com/mackron/miniaudio) (public domain or MIT-0): mixing, 3D positioning, streaming from disk, and sound groups that act as music and effects buses. Vorbis through `stb_vorbis` for music and ambience. It replaces `PlaySound` and MCI in `Sound.cpp`.

FMOD Studio is the upgrade if a sound designer needs authoring tools. It is commercial, with a free tier for small studios.

### Tools around the code

- Dear ImGui stays for the editor, on the docking branch. That fills the dockable layout line.
- [Tracy](https://github.com/wolfpld/tracy) (BSD-3): CPU and GPU zones, memory, and locks.
- [spdlog](https://github.com/gabime/spdlog) (MIT): logs to a file and to an editor console.
- [doctest](https://github.com/doctest/doctest) (MIT): the headless `tests` target, wrapping `runPlaySelfTests` and the existing self-tests.
- [sentry-native](https://github.com/getsentry/sentry-native) (MIT) with crashpad: crash reports with minidumps.

### What we write ourselves

- Terrain tiles: chunked LOD, splatting, holes, and water surfaces.
- Tile streaming and memory budgets.
- The simulation and view channel, interpolation, typed events, and replay.
- Culling: frustum and portals first, GPU occlusion later.
- Character customization compositing.
- Simple GPU particles. Effekseer (MIT) if an artist needs an effect editor.
- Editor terrain and placement tools.
- The glue between EnTT, Jolt, Recast, ozz, and Luau.

### What happens to the current code

| Code | Fate |
| --- | --- |
| Play rules in `Play.cpp`: pickups, triggers, Use, tweens, hazards, spawns, rooms | Ported onto components. Their tests are kept |
| `Collision.cpp` and the sphere controller | Replaced by Jolt |
| `Sound.cpp` | Replaced by miniaudio |
| `GpuGl`, the Win32 window, the ImGui Win32 and OpenGL backends | Replaced by SDL3, Diligent, and Diligent's ImGui renderer |
| The OpenGL ray tracer | Editor still renderer until the raster path draws the demo room, then retired |
| `Mesh.cpp` OBJ and FBX loading | Moves into the cooker on Assimp |
| Scene file version 1 | Becomes an importer into the entity world, so the demo rooms survive |
| `raytracer-settings.txt` | Kept, with new keys |
| Mesh BVH | Kept for the ray tracer only |

### Adoption by phase

| Phase | Libraries |
| --- | --- |
| 0 | CMake, vcpkg, EnTT, GLM, enkiTS, spdlog, doctest, Tracy, SDL3 |
| 1 | bitsery, SQLite, Jolt, miniaudio |
| 2 | Diligent, DiligentFX, Assimp, meshoptimizer, DirectXTex, FlatBuffers, zstd, PhysFS |
| 3 | ozz-animation |
| 4 | Recast and Detour, Luau, LuaBridge3 |
| 5 | sentry-native |

SDL3 comes in Phase 0 on an OpenGL context, so the old renderer keeps running while the window code is replaced. Diligent replaces that context in Phase 2.

## Phase 0 — A core that can run without a window

A cooker, a headless test run, and a later server all need the simulation without a window, OpenGL, ImGui, or the FBX SDK. Today the simulation and the renderer share one object type.

- Move the build to CMake and vcpkg first, with the current sources unchanged, and check that `raytracer.exe` behaves the same.
- Split `engine/` into the `core`, `render`, `audio`, and `import` targets. `import` holds OBJ and FBX loading and links only into the editor and the cooker.
- Replace `Hittable` with EnTT entities and components: transform, render mesh, collider, motion, tag or role, audio emitter. An entity can exist with no render component.
- A 64-bit `EntityId` that stays unique across sessions and saves, in place of the per-scene integer counter.
- One transform component for every entity on GLM: double position, quaternion rotation, non-uniform scale, a parent, and cached local and world matrices. Named sockets on meshes for attachments.
- enkiTS as the worker pool, with async file reads as pinned tasks.
- SDL3 for the window and input, still on an OpenGL context so the ray tracer keeps drawing.
- spdlog for logs, Tracy zones in the frame and the play step, doctest for the `tests` target.
- Exit: the `tests` executable loads the demo scene with no GPU, steps it headless, and passes the play self-tests.

## Phase 1 — A simulation that could be a server

The game stays local. The simulation and the view talk as if a network were between them, through an in-process channel. Adding the network later replaces that channel and leaves the simulation as it is.

Rules the simulation keeps:

- **Input enters as commands.** Every player intent is a command stamped with a tick number, placed in one queue. `PlayInput` plus the camera direction already work this way. Keep it that way for every new action, including abilities, interaction, and movement modes.
- **The simulation owns its state.** Simulation components (transform, health, motion, AI) live apart from render components. The view reads a snapshot after each tick. It never writes simulation state. Today `stepPlay` edits the `Scene` the renderer draws, and that is the split to make.
- **Events leave as typed messages.** Pickup, damage, death, sound, and effect events are values with an entity id, a position, and a tick. The view turns them into sounds and particles. That replaces the string comparison in `playScoreBeep`.
- **Entities refer to each other by id**, never by pointer, so a reference survives a save, a reload, and later a copy on another machine.
- **Time is the tick count.** No simulation code reads the frame time or the wall clock. Timers are in ticks.
- **Randomness is seeded and owned by the simulation.** No `rand()` and no unseeded engine. The same seed and the same commands give the same result.
- **State is plain data.** Each simulation component can be written and read as bytes with bitsery, with a version number. The same code serves saves, replays, and later replication.
- **The view interpolates.** It draws between the last two ticks. At a 30 Hz tick that is required, not optional, and it is the same mechanism a client later uses for remote entities.

Work:

- The tick moves from 1/60 s to 1/30 s. The play tests that count steps are updated to the new rate.
- The in-process channel: a command queue in, a snapshot and an event list out.
- Simulation state separate from the render scene, with a sync step that copies transforms into render components once per tick.
- The player becomes a Jolt `CharacterVirtual`. Solids, platforms, and the curb step move onto Jolt shapes, and `Collision.cpp` is removed once the play tests pass on Jolt.
- Typed events, and a view-side listener that plays them through miniaudio and the particle burst.
- A command recorder and a replay player.
- A save of the play session in SQLite: every simulation component, the tick, and the seed.
- Exit: a recorded session replays to the same state, compared bit for bit after 10 minutes of play (**target**). A save in the middle of play loads back to the same state. The game plays only through the channel.

## Phase 2 — A continent

### Terrain and world layout

- A heightmap terrain cut into fixed tiles, each with its own chunked LOD. WoW uses 64 by 64 tiles of about 533 yards per continent. Pick our own tile size.
- Texture splatting: four to eight layers per chunk, with alpha maps.
- Water and liquid volumes with a surface level and a type.
- Holes in the terrain for cave entrances.
- Placed objects: small doodads (instanced props) and large buildings with interiors.
- Interiors divided into cells joined by portals, for culling and for indoor lighting and ambience.
- Map definitions: a continent is one map, each dungeon is a separate map that can be instanced.

### Streaming

- Load and unload tiles around the camera on a background thread, in rings by distance.
- A memory budget per resource type. Evict the farthest data first.
- Texture and mesh streaming by mip level and LOD level.
- A load-ahead distance that covers the fastest travel speed (**target**: a mount at 3× run speed never sees a hole).
- The simulation loads the same tiles for collision and navigation, with no render data. Keep that load path separate from the render one so a server can use it alone.

### Assets

- A cooker that reads glTF, FBX, and OBJ through Assimp and images through `stb_image`, and writes binary runtime formats. The game never parses a source file.
- Zip archives mounted through PhysFS, with a manifest of content hashes, so a patch archive replaces only changed entries.
- Block-compressed textures (BC1, BC3, BC5, BC7) with a full mip chain, written by DirectXTex as DDS.
- Mesh LODs and vertex-cache and overdraw order from meshoptimizer, compressed with zstd.
- Dependency tracking in the cooker, so changing one texture recooks only what uses it.
- Static data tables (spells, items, creature templates, zones) as FlatBuffers schemas, authored in JSON and built by `flatc`. The engine owns the schemas and the loader, not the rows.

### Renderer

- A clustered forward path on Diligent, Direct3D 12, as the primary view.
- No fixed caps. Placements, lights, and textures come from GPU buffers sized at runtime and bindless texture tables, not from constant arrays.
- Frustum culling on the CPU, portal culling for interiors, and occlusion culling (hierarchical Z) later.
- Instanced and indirect drawing, so a forest of one tree is one draw.
- Clustered light culling for hundreds of local lights.
- Cascaded shadow maps for the sun from DiligentFX. Point and spot shadows only for lights near the camera.
- A sky from DiligentFX atmospheric scattering, a sun and moon on a day clock, and weather (rain, snow, fog banks).
- Transparency sorting for water, glass, and effects.
- A GPU particle system.
- Material shaders with variants (terrain, opaque, alpha-tested foliage, transparent, character, unlit), not one shader with every feature.
- Graphics quality settings that scale view distance, shadows, and density.
- Exit: walk 2 km across the test continent at 60 fps on the minimum spec, with no hitch longer than 50 ms (**targets**).

## Phase 3 — Characters

- Skeletal meshes, skinned on the GPU. Skeletons and clips cooked into ozz-animation formats.
- Animation clips with blending, additive layers, and separate upper and lower body, sampled by ozz.
- An animation state graph driven by simulation state (moving, falling, swimming, casting), not by a timeline. The view picks the clip. The simulation never waits on an animation.
- Attachment sockets for weapons, shields, and helmets, and a mount socket that carries the rider.
- Character customization: swappable mesh parts, texture compositing for skin and armor, and color tints.
- Foot IK on slopes and stairs.
- Animation LOD: lower update rate and fewer bones for distant characters.
- A crowd path: instanced skinning or a cached pose per animation frame for distant characters.
- Exit: 300 animated, equipped characters in view at 60 fps on the minimum spec (**target**).

## Phase 4 — The simulated world

### Collision and movement

- The Jolt capsule controller from Phase 1, now on streamed tiles.
- Collision against terrain as Jolt heightfield shapes, and buildings and doodads as mesh shapes, added and removed with their tile.
- Swimming and flying volumes, falling, and fall damage height as engine data.
- Line-of-sight queries as Jolt ray casts, cheap enough to run per ability cast.
- Jolt rigid bodies stay off unless a design needs them. A WoW-like game ships on a character controller plus queries. See `docs/physics.md`.

### Spatial grid and simulation LOD

- A spatial grid over the loaded tiles. Range queries for AI, triggers, and audio go through it.
- Creatures with no player nearby sleep and wake when a player comes in range.
- Update rate by distance: near entities every tick, far entities less often.
- The same grid answers "which entities can this player see" when a network arrives.

### AI and navigation

- A Recast navmesh per tile, generated in the cooker from the cooked collision, loaded with the tile.
- Detour path queries on enkiTS workers, with a per-tick budget.
- DetourCrowd for steering and local avoidance, so a pack of creatures does not stack on one spot.
- A behavior runtime (state machine or behavior tree) that scripts can drive.
- Spawn points and respawn timers as engine data, in ticks.

### Scripting runtime

- Luau for creature behavior, triggers, and encounter logic. Scripts run inside the simulation tick and see only simulation state and the command queue.
- LuaBridge3 bindings to entities, timers, events, and queries. The Luau interrupt callback enforces an instruction limit, so one script cannot stall a tick.
- Hot reload of scripts while the game runs.

### Persistence

- A save of characters, inventory, and world state in the Phase 1 SQLite file, built on the bitsery component serialization.
- Tables by owner (character, map, world), rows keyed by `EntityId`, so each table can later move to a server database without changing the format of a record.
- Writes on a background thread, one transaction per save, so a crash leaves either the old save or the new one.
- Autosave on a cadence and on quit, so a crash loses at most a few seconds (**target**).
- A save version number separate from the scene and asset versions, with migrations from older saves.

- Exit: the simulation holds 5000 creatures and the player at its tick rate within its CPU budget (**target**), measured headless.

## Phase 5 — Shipping it

- Packaging: a game folder of the exe, archives, and runtime libraries with no path back to the repo.
- A patcher that downloads changed archives against the hash manifest and mounts them over the base through PhysFS.
- Crash reports through sentry-native, with symbols kept per build.
- Tracy captures for CPU and GPU frame time, draw calls, simulation tick time, and streaming stalls, and an in-game overlay of the same counters.
- A build pipeline that compiles the game and the headless test executable, runs self-tests and a replay check, and cooks content on every commit.

## Editor work the content needs

These are tools, not game UI. A continent cannot be authored with sliders in an inspector.

- Terrain sculpting and texture painting on tiles.
- Placement at scale: scatter brushes for foliage and rocks, snapping to terrain.
- Portal and interior cell editing.
- Navmesh preview, light preview, and a streaming-budget overlay.
- Several people editing different tiles of the same map without overwriting each other.

## Order and dependencies

Phase 0 blocks everything, and the CMake move is its first step. Phase 1 comes right after it, before the content grows, because every system added later then follows its rules from the start. Phase 2 and Phase 3 can run side by side after Phase 0. Phase 4 needs Phase 1 (commands, events, serialization) and the cooked tiles from Phase 2. Phase 5 runs alongside from Phase 2 on.

## Deferred: networking

Nothing in this section is planned yet. It is listed so the earlier phases leave room for it.

The network can be added without redoing the engine as long as Phase 1 holds. A rewrite is forced when:

- simulation code reads devices, the camera, the frame time, or the GPU,
- the view writes simulation state,
- the view infers events from messages or state changes,
- entities point at each other by address,
- randomness or timers live outside the simulation,
- or a component cannot be written as bytes.

What the earlier phases leave ready:

| Network need | Already in place from |
| --- | --- |
| A server process with no window | Phase 0 headless core |
| Client input sent to the server | Phase 1 command queue |
| State sent to clients | Phase 1 bitsery component serialization |
| Remote entities drawn smoothly | Phase 1 interpolation |
| Sounds and effects on clients | Phase 1 typed events |
| Choosing what each player receives | Phase 4 spatial grid |
| Map loading on a server | Phase 2 collision and navigation tile path |
| Character storage | Phase 4 SQLite tables grouped by owner |
| Movement the same on client and server | Phase 1 Jolt controller inside the simulation |

What is still new work, and adds to the engine without replacing any of it:

- **Transport and sessions.** The first choice to make when this section starts. GameNetworkingSockets (BSD-3) is the library-first answer: reliable and unreliable messages over UDP, encryption, and connection handling. Framing, handshake, protocol version, heartbeat, and reconnect sit on top.
- **Replication.** Spawn, delta, and despawn per component, with dirty-field tracking and a bandwidth budget per client.
- **Prediction.** The client runs its own movement ahead of the server and corrects when the server disagrees. That needs the controller to be the same code on both sides, which the Jolt controller in the simulation already is.
- **Clock synchronization** between server and client.
- **Interest management** on the Phase 4 grid, with update rates by distance and a view radius (WoW uses about 100 yards).
- **Server validation** of speed, teleport, line of sight, and every client message.
- **A database** for accounts and characters, with transactions that cover a trade on both sides, replacing the local save groups.
- **Server infrastructure.** Authentication, a realm list, a world service that routes players to map servers, handoff between maps, dungeon instances on demand, layers for crowded zones, and cross-server messaging.
- **Operations.** Server deployment, rolling restarts, logs, metrics, encryption after login, and rate limits.
- **Tools.** A lag and packet-loss simulator, a network stats overlay, headless bot clients, and load tests with thousands of bots.

## Checklist lines this plan needs that are not there today

`docs/engine-checklist.md` has no section for terrain, a play-session save format, or networking. When work starts on one of those, add a section and a page for it there, in the same form as the others.
