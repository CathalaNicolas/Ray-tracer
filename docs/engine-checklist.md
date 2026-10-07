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
| Debug and performance | [debug-and-performance.md](debug-and-performance.md) |
| Build, platform, and content | [build-and-platform.md](build-and-platform.md) |

The **primary interactive view** is clustered forward on Diligent (D3D12 on Windows, Vulkan for Linux bring-up): `GfxDevice` / `GfxView` / `GfxCluster` / `GfxFx`, HLSL under `engine/shaders/`. The Whitted OpenGL path (`GpuRayTracer`) remains for progressive stills and `--self-test` on Windows only; GL `GpuLimits` are not raised. CMake + FetchContent builds the Diligent path; see [build-and-platform.md](build-and-platform.md). The editor places spheres, planes, and meshes, saves a version-1 scene, and can play. `raytracer` / `raytracer.exe --game` opens the Diligent view when built with `RAYTRACER_DILIGENT`.

## 1. Application loop

The first version is in place. The lines below it are later work.

- **here** — Window, swap, and an editor frame
- **here** — Play and stop inside the editor, with the scene restored on stop
- **here** — Fixed simulation step (play mode uses 1/60 s)
- **here** — The window presents every frame. The simulation consumes 1/60 s steps on that same thread, then one render runs if the view changed
- **here** — Pause, one-step while paused, and a speed scale from 0.25× to 2×
- **here** — `raytracer.exe --game` starts in play and hides the editor
- **here** — Startup shows one Loading frame, then loads the scene and the GPU. Shutdown releases them in reverse
- **here** — At most four simulation steps per window frame. A longer hitch drops the leftover time
- **missing** — A second thread so a long simulation step does not block the window
- **missing** — Interpolation between simulation steps when the display is faster than 60 Hz
- **missing** — A choice of vsync or an unlocked frame cap
- **missing** — Loading progress for a large scene
- **missing** — Keeping the dropped hitch time for a deterministic replay

## 2. World and scenes

- **here** — Scene file, version 1, still loads older files
- **here** — Named objects with stable ids
- **partial** — One scene in memory
- **partial** — Two demo rooms. The goal in room 1 advances to room 2 and the round continues. The goal in room 2 wins. Stop asks whether to keep the scene or restore the play snapshot. Streaming stays missing
- **partial** — Prefabs. Make prefab marks the selection. Add → Prefab places a copy, including children. Editing the source updates every placement except the copy's position. There is no prefab file separate from the scene
- **partial** — One string tag per object. The file still stores that word. `makeRole` builds a `Role` beside the shape (`Solid`, `Player`, `Pickup`, `Trigger`, `Use`, `Goal`, `Hazard`, `SpawnPoint`, `Spawner`). Play dispatches on `RoleKind`. A layer is a separate integer: 0 or 1
- **partial** — A pickup can be removed during play, and a spawner can add one while the round is running. Discard on Stop restores the snapshot. Keep leaves the scene as it is
- **missing** — Streaming a large level in pieces
- **here** — Undo and redo in the editor, 32 deep, while not playing. Play does not record, and undo does not restore play-mode simulation

## 3. Transforms

- **here** — Position, uniform scale, and Euler rotation on meshes
- **here** — Shared mesh geometry with a transform per placement
- **partial** — Parent and child hierarchy. A mesh parent rotates and scales the child via `applyParentAxes`. A sphere or plane parent only translates. `Scene::find` is an id map. `parentFrame()` is cached until `bumpParentFrames()`. There is still no shared transform type
- **missing** — Non-uniform scale
- **missing** — A single transform type for spheres, planes, meshes, lights, and cameras
- **missing** — Local versus world matrices
- **missing** — Pivots and attachment points (hand, muzzle, socket)

## 4. Assets

- **here** — OBJ and FBX meshes, static, triangulated, one material per file
- **here** — Image textures, normal maps, environment maps
- **partial** — Load from a path stored in the scene
- **here** — An asset list of `assets/`. Add mesh, albedo, normal, and the environment map use the selected file. A changed mesh, albedo, normal, or environment file reloads on a check that runs at most four times a second
- **missing** — Cooked runtime assets so play does not parse FBX
- **missing** — LODs
- **missing** — Texture compression and mipmaps beyond what the upload does today
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
- **partial** — Particles: eight billboards burst when a pickup is collected and when a hazard costs health. They live in the shading shader. No trails
- **missing** — Skeletal meshes and morph targets
- **partial** — Instanced opaque mesh draw on the Diligent path (`GfxView`); GL still path still shares triangle storage without instances
- **missing** — Frustum culling and occlusion culling on Diligent (CPU frustum helpers exist for later)
- **partial** — Diligent clustered forward is the primary interactive look; GL Whitted remains for stills / self-test
- **partial** — DiligentFX bloom / CSM / sky wired through `GfxFx` + quality settings; GL still bloom remains for progressive frames
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
- **partial** — Diligent path: clustered light lists (no 8-light rank). GL still path still caps at 8
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

- **partial** — Sphere against sphere, plane, and mesh triangles. No capsule
- **here** — Mesh collider uses the triangles, queried through the BVH
- **partial** — Layer 0 blocks the player and the chase camera. Layer 1 blocks the player and lets the camera through. The demo door is layer 1
- **partial** — Overlap for pickups, triggers, the goal, Use, and hazards
- **partial** — `castPlayRay` from the player along the camera look, up to 3 units, and the chase-camera ray. Use and the goal answer that look when the player is not already overlapping them. No sphere or box cast
- **here** — Character controller: slide along walls, ground check, jump, and a step onto a curb up to 0.35 tall
- **partial** — Picking in the editor is a ray against the scene, not a game query

## 11. Physics

- **partial** — Gravity and a jump impulse on the player sphere only
- **missing** — Rigid bodies, mass, friction, restitution
- **partial** — A `platform` tag is a solid the player does not push. Motion on that solid carries the player who is standing on it. No other kinematic body
- **missing** — Joints: hinge, spring, fixed
- **missing** — Continuous collision so fast objects do not tunnel
- **missing** — Sleeping bodies
- **missing** — Vehicles, cloth, soft bodies, destruction
- **missing** — A third-party library (PhysX, Jolt, Bullet) versus a few hundred lines of our own

A first game can ship with collision and a character controller and never take a rigid-body solver.

## 12. Gameplay objects

- **partial** — Objects are renderables with a name and one tag
- **missing** — Entities or actors that exist even when they are invisible
- **missing** — Components (render, collider, script, audio) on one object
- **missing** — Prefab variants
- **partial** — A spawner adds a pickup on a timer during play. Discard on Stop removes it with the snapshot. Keep leaves it
- **here** — A player sphere, the first object tagged `player`, distinct from the editor selection
- **partial** — Score, health, a HUD message, and a win or loss result. A pickup adds 1 health up to 3. No inventory
- **here** — Pickup on overlap, a trigger message, Use on F, a goal that wins, and a hazard or a fall that loses. A `spawn` tag turns that loss into a respawn. No timer rule besides a spawner interval

## 13. Input

- **here** — Keyboard in play mode, AZERTY ZQSD, Space or E to jump, Esc to pause
- **here** — Mouse look while playing, and orbit, pan, zoom, and picking while editing
- **partial** — `PlayInput` is move, jump, and use. Settings lists the keys and writes them to `raytracer-settings.txt`
- **here** — Rebinding for forward, back, left, right, jump, a second jump, and use
- **missing** — Gamepad
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
- **partial** — The cursor is hidden and clipped to the window during play, and shown again while editing, paused, or when the round is over. There is no separate free mode during play

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

- **partial** — Pickup, win, and beep load `assets/pickup.wav`, `assets/win.wav`, and `assets/beep.wav` when those files exist. A generated tone is the fallback. Master volume is a slider written by Settings → Save settings. Music, buses, and variation stay missing
- **partial** — One looping music WAV during play, plus the three one-shots. No second effects clip and no music bus separate from master volume
- **partial** — One master volume. No music or effects buses
- **here** — The listener is the camera. Pickup, win, and beep fade linearly and are silent at 16 units. Music stays at master volume
- **missing** — Random variation so a footstep is not one file forever

## 17. Scripting and game rules

- **partial** — C++ gameplay lives in `stepPlay`
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
- **missing** — Particle bursts
- **partial** — Up to `max_tweens` Use tweens (default 8). Each moves and rotates a named object over `action_duration` (default 0.4 s) and then leaves it there. There is no timeline
- **missing** — Trails and simple decals
- **missing** — Rumble

## 20. Persistence

- **here** — Save and load the edited scene
- **missing** — Save a play session (position, inventory, flags)
- **missing** — Slots, autosave, and a version for save files separate from the scene version
- **here** — `raytracer-settings.txt` stores master volume, render size, samples, bounces, the play keys, and every `EngineSettings` key (play, GPU stacks, bloom, frustum, snap, mirrors). The editor loads that file at startup and Settings edits the values. A numeric value that does not parse is skipped, and `bounces 0` still loads. A missing `mirrors` line stays on. There is no second bindings page

## 21. Debug and performance

- **here** — Self-test for the renderer, the scene file, and the play step
- **partial** — Frame time in the editor. Debug dump writes `raytracer-debug.txt` with objects, lights, camera, the point-light shadow fit, and mirror-bounce rays. Bounce rays can be drawn on the view
- **missing** — On-screen profiler (CPU, GPU, draw, simulation)
- **here** — Colliders draws sphere circles, mesh boxes, plane patches, each scene light's bound, and a frustum for each named camera other than the view
- **missing** — A console command line
- **missing** — Crash logs
- **missing** — Deterministic replay of a play session

## 22. Build, platform, and content

- **here** — Windows, MSVC, FBX SDK, OpenGL 3.3
- **missing** — A packaged folder a friend can run without the repo
- **missing** — Asset paths that still work after that package
- **missing** — Another OS
- **missing** — Installer or a single zip with the exe, assets, and the runtime DLLs
- **missing** — A content license note for third-party meshes and the FBX SDK

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
