# Debug and performance

## Self-test

`raytracer.exe --self-test` runs `engine/src/SelfTestEntt.cpp` and `runPlaySelfTests`. It prints `self-test: ok` when every check passes. A failure prints `FAIL` and a name, and the process returns non-zero. On Windows the renderer checks create an offscreen OpenGL context through `RayTracer`. GPU and FBX checks stay on this path, not in `tests.exe`.

The renderer checks cover scene IO, a tail parse of `layer 1` and `prefab "Gem"` without writing a scene file, mesh sharing, unclamped emission, Reinhard on the display path, normal maps, mesh yaw, and the lens fields. Self-test also checks `soundDistanceFade` against `sound_far` and that `applyEngineSetting` accepts known keys. The play checks cover movement, gravity, jump, solids, pickups, and triggers. Success from the play tests is silent. They only print on failure.

The opaque non-glass comparison stays valid when aperture is 0 and linear output is on. Do not clamp that path to 1.

## Headless `tests`

`tests.exe` (CMake target `tests`) is a doctest binary with no editor, ImGui, or GPU translation units. It initializes logging and jobs, then runs:

- `runPlaySelfTests()` through `SimSession::step` (same play checks as above, plus a pickup `SimEvent` and pose interpolation smoke)
- bitsery round-trip of `Command` / `Snapshot` / `SimEvent` and a headless command replay (`tests/SimReplayTests.cpp`)
- SQLite play-session save/load (`tests/SimSaveTests.cpp`; mid-jump save then 300 ticks versus the uninterrupted run)
- command replay over 18000 ticks plus pickup / goal / hazard scoring (`tests/SimReplayTests.cpp`)
- `createDemoScene()` plus two `SimSession::step` ticks (OBJ meshes via `raytracer_import`)
- a small jobs smoke case (`parallelFor` plus a pinned read of `vcpkg.json`)
- CPU frustum / view-distance tests and `rankGpuLights` (`tests/FrustumTests.cpp`)

Run it from the project root so `assets/` and `vcpkg.json` resolve. It does not create a window or an OpenGL context.

## Logging (spdlog)

`logging::init` in `engine/src/Log.cpp` installs a color console sink and a `raytracer.log` file sink (truncated each run), sets the default logger name `raytracer`, and writes a start line. `logging::shutdown` flushes and tears the logger down. `editor/src/main.cpp` calls both around the process lifetime. Startup paths log editor / game / offline render at info level.

## Tracy

`raytracer` and `tests` link `Tracy::TracyClient` (`TRACY_ENABLE` comes from that target). Capture is optional: zones are no-ops when no Tracy server is attached.

- `editor/src/editor.cpp`: `ZoneScopedN("EditorFrame")` for each window-loop iteration, `FrameMark` after `SwapBuffers`
- `engine/src/Play.cpp`: `ZoneScopedN("stepPlay")` at the top of `stepPlay`

## In the editor

The last GPU render time is shown in milliseconds. The GPU limit warning from `docs/assets.md` appears when a cap is exceeded.

The toolbar checkbox **Colliders** sits after Play and before Save scene. It is off until checked, and it stays clickable while playing. When it is on, `drawColliders` in `editor/src/DebugDraw.cpp` strokes the view window's draw list over the view image, using the same `view.lookFrom`, `view.lookAt`, and field of view that `viewCamera` just passed to the shot.

Solid spheres are green, `IM_COL32(72, 220, 120, 255)`. The player sphere is orange, `IM_COL32(255, 176, 46, 255)`, including while editing, when its tag is `player`. A circle is the sphere center projected into the image rectangle, using the same camera as the frame and the image widget's own pixels. Its pixel radius is the sphere radius divided by the depth along the view, times half the image height over the camera's vertical half-angle tangent. A center at or behind the camera is skipped, and a circle under half a pixel is skipped.

A solid mesh is the twelve edges of its oriented world box, in blue `IM_COL32(90, 170, 255, 255)`, drawn with `AddLine`. Local `boundsMin` and `boundsMax` on the mesh geometry are scaled, rotated with `meshRotate` (yaw Y, then pitch X, then roll Z), and translated by the mesh position. Those eight corners are the box. Each edge is clipped to a depth just past `1e-4` along the view before `project`. An edge with both ends at or behind that depth is omitted, and an end that still fails to project is omitted. A non-finite screen position is omitted. If the mesh has no geometry, the overlay is a 6-pixel cross at the mesh position in the same blue.

Every plane is a yellow square, `IM_COL32(230, 200, 70, 255)`, of half-extent 2 centered on the plane point, plus a normal segment of length 0.75. The square uses the plane's world normal and two tangents.

Each entry in `scene.lights()` is drawn. A point light is an amber circle, `IM_COL32(255, 170, 50, 210)`, whose radius is `sqrt(3 / falloff)`. Falloff below `1e-4` is treated as `1e-4`. That radius is where `intensity / (1 + falloff * distance²)` has fallen to a quarter of `intensity`. A `radius` above 0.01 adds a smaller orange circle, `IM_COL32(255, 90, 40, 255)`, for the soft-shadow disk. A spot with outer angle above 0 adds a cone of that same range: eight rim segments and every other spoke. A directional light has no position, so the overlay draws a 1.5-unit arrow that starts 2 units in front of the camera and follows the light direction. Emissive objects are not given a second circle.

Each named camera in `scene.shots()` draws a purple frustum, `IM_COL32(176, 140, 255, 230)`, with near 0.35 and far 8. The shot that matches the view (same look-from and look direction) is skipped, because projecting that frustum collapses to the image border. Solids use `play_detail::isSolid`, including `platform` meshes.

## Build note

`make clean` deletes the executable. If `raytracer.exe` is open, the link fails with a lock. Link to another name and copy over it. Do not clean as a workaround.

The toolbar button **Debug dump**, next to Colliders, writes `raytracer-debug.txt` in the working directory. The file lists the camera, ambient color, every object (kind, tag, layer, parent, transform, motion, action, material), every light, and each named shot. For each point light, and for each emissive object such as the Lamp, it records the shadow camera: which mesh it looks at, the target, field of view in degrees, near and far, and how far each mesh box sits in front of or behind that view. It also notes whether the last GPU frame reused the shadow maps, the 1024 map size, and the depth bias `max(0.004, 0.012*(1-facing))`. The file is rewritten each time the button is pressed.

The same file ends its object list with a `mirror bounce` section. For each sphere with reflectivity above 0.35 and no transmission, it tests the lamp and every other emissive object against six floor points around the sphere and the near point of up to six other spheres. Each line is `kept`, `align`, `faces away`, `dim`, or `blocked`, plus the bounce point `Q`, the reflection alignment, `nDotL`, attenuation from that point, and the resulting rgb. A blocked line names the object on the way to the bounce point or from there to the light. The mirror's own emission is skipped. A bounce from one mirror onto another is not a second bounce.

The toolbar checkbox **Bounce rays** draws a segment only when that sample's rgb sums to more than 0.02. A kept bounce is green, thickness 2. A rejected one is red, thickness 1. Each segment is the light position to `Q`, then `Q` to the receiver. Dim test points stay in the dump and are not drawn.

## Replay

`CommandRecorder` on `SimSession` copies every `enqueue`'d `Command`. Live play still drops hitch leftover in `takePlaySteps`; that time is not in the log. `replayCommands` / `replayFromBlob` in `engine/include/SimReplay.hpp` walk the recorded ticks in order (`kPlayStep` each, no wall clock). A few dozen ticks in `tests.exe` is enough; ten minutes is a target, not a requirement. Compare `PlayState` HUD fields and entity transform doubles (or skip pose if the play integrator is in flux). Bitsery helpers are `engine/include/SimSerialize.hpp`.

## Not built

- An on-screen profiler for CPU, GPU, draw, and simulation (Tracy is capture-only today).
- Headless demo-scene load and renderer self-tests inside `tests.exe` without an offscreen GL context.
- Collider overlay for emissive objects that are not in `scene.lights()`.
- A console command line.
- Crash logs.
- Keeping dropped hitch leftover so a wall-clock tape would match a lagged live session.
