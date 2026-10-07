# Application loop

The window, the play simulation, and the GPU render share one thread in `editor/src/editor.cpp`. `editor/src/main.cpp` chooses the mode and then calls `runEditor`. The clock helpers are `clampFrameDt` and `takePlaySteps` in `engine/include/Play.hpp`. Play ticks go through `SimSession` in `engine/include/SimChannel.hpp` (`enqueue` + `take`, or `step` in tests).

Process startup in `main` calls `logging::init` then `jobs::init` before any mode runs, and `jobs::shutdown` then `logging::shutdown` on the way out. That keeps an enkiTS worker pool alive for the whole process. SDL is not started in `main`. `runEditor` calls `SDL_Init` (video and gamepad) and `SDL_Quit` when the window closes, so `--help`, `--self-test`, `--no-window`, and `tests.exe` never need SDL. Play steps and the editor frame still run on the window thread; the pool is available through `jobs::parallelFor`, `jobs::runPinned`, and `jobs::schedulePinned` in `engine/include/Jobs.hpp`.

## Modes

| Invocation | What starts |
| --- | --- |
| `raytracer.exe` | Editor. The demo scene is loaded. Play is off. |
| `raytracer.exe --game` | Same window, title `Game`. A title screen with Play, Volume, and Quit sits in front of the round. Play starts the round. Toolbars, the outliner, and the inspector stay hidden. |
| `raytracer.exe --no-window` | One still image, no window. Optional `--ppm` or `--png`. |
| `raytracer.exe --self-test` | Checks, then exit. No window. |

`--game` and `--self-test` do not change scene version 1.

## Startup and shutdown

1. `SDL_Init`, create an SDL3 window with an OpenGL 3.3 core context, and make it current.
2. Start ImGui with `imgui_impl_sdl3` and `imgui_impl_opengl3`.
3. Draw one frame that says `Loading...` and swap it.
4. Build the demo scene, then initialize the GPU renderer. The performance clock starts after that, so the load does not become the first play frame.
5. Run the frame loop.

Shutdown releases the GPU renderer, play gamepad, the display texture, ImGui, the GL context, and the window, then `SDL_Quit`. A failed SDL init or GL context returns before the loop and still quits SDL if it was started.

## One frame

1. Drain SDL events and pass each to `ImGui_ImplSDL3_ProcessEvent`. `SDL_EVENT_QUIT` ends the loop. A minimized window sleeps instead of rendering. Pixel-size changes update the GL viewport size.
2. Measure `frameDt` with `SDL_GetPerformanceCounter`. `clampFrameDt` turns a negative value, or anything above `0.25` s, into `0.25` s.
3. `--game` starts on the title screen. Play sets `view.playing`. The first play frame uses the measured (clamped) `frameDt`; it does not force one tick.
4. Start an ImGui frame and draw the interface. In `--game`, that is the view plus the HUD and pause window.
5. Escape, when ImGui is not capturing text, toggles pause while playing. It does not restore the scene.
6. If Play was just pressed, clone the scene into `playSnapshot`, remember the editor camera, clear `PlayState` (keeping pause if it was already set), and arm the chase camera. If Stop just happened, move the snapshot back and restore that camera.
7. While playing and not paused, the editor shows a speed slider. While paused, the pause window can change that speed, step one tick, resume, or quit.
8. While playing, and not paused or when Step was pressed, enqueue one `Command` per tick from `readPlayInput` plus the look vector (`lookAt - lookFrom`), `SimSession::take` those ticks, `applyDisplay` (writes `DisplayTransform` only), place the chase camera from `displayWorldPosition()`, then play `SimEvent` clips and particle bursts. Particle life then advances with `frameDt`. Escape pause is `ViewState::paused`, not `PlayState::paused`.
9. Draw the HUD. Pause **Quit** in the editor is Stop. In `--game` it pushes `SDL_EVENT_QUIT`.
10. If the scene or camera changed, render one GPU sample. The window still swaps every frame, so ImGui presents at the display rate while the ray tracer only runs when the view changed. While the editor is idle and play is off, keep adding samples up to the Samples slider (`N` means `N×N`, slider 1–4). Play forces one sample.

## Fixed step

`takePlaySteps` decides how many ticks of `kPlayStep` (`1/30` s) to run:

- Normal play adds `frameDt * speed` to an accumulator. Speed is the pause-menu slider, from `0.25` to `2`.
- Each whole `1/30` s becomes one queued `Command` and one `SimSession::take` tick (`dt = kPlayStep` inside `stepPlay`), then the view interpolates and the chase camera is placed.
- At most four steps run in one window frame (`max_play_steps`). If time is still left, the accumulator is cleared. The simulation slows during the spike instead of spending the whole frame catching up. That leftover is not stored for replay.
- Step, while paused, runs exactly one tick and does not change the accumulator. Held movement keys still apply. Mouse look does not, until Resume. The displayed pose is the new snapshot (alpha 1).
- Pause without Step adds no time. The last interpolated picture stays until the next tick.

Play timers are tick counts, not seconds: `PlayState::motionTime`, `Tween::time`, and `Spawner::clock`. Scene and settings still author durations in seconds (`Motion::period`, `spawnEvery`, `action_duration`). `playTicksFromSeconds` rounds `seconds / kPlayStep` (a 1 s tween is 30 ticks; default `action_duration` 0.4 s is 12 ticks; a 0 spawn interval uses 3 s → 90 ticks). Player integration uses `dt` in seconds (`kPlayStep` per tick). Particle life advances on the window frame (`frameDt`), not inside `stepPlay`.

`PlayState::simSeed` (default 1) and `PlayState::simRng` (`std::mt19937`) belong to the simulation. `PlayState{}` restores seed 1 and that stream. Play does not call `rand()`. Nothing in `stepPlay` draws from the generator yet.

A display faster than 30 Hz interpolates local poses between the last two snapshots. Alpha is the leftover `playAccumulator / kPlayStep` (0 shows the previous tick, 1 the current). HUD fields always come from the latest snapshot. Pause and single-step use alpha 1.

## Simulation channel

`SimSession` (`engine/include/SimChannel.hpp`, `engine/src/SimChannel.cpp`) is the public play API for the editor, `--game`, and headless tests.

- `attach(Scene, PlayState)` — the session does not own them. One EnTT `Scene` is still the sim store; a tick mutates `Transform`. After the tick, `captureSnapshot` records every object's local pose plus HUD fields. The view writes `DisplayTransform` only; gizmos write `Scene` only while not playing. Keep on Stop calls `clearDisplay` before cloning.
- `begin()` — tick 0, empty queue, snapshot of the current scene (Play / Restart).
- `enqueue(Command)` — tick-stamped intent. `Command` is `PlayInput` (`moveX`, `moveZ`, `jump`, `use`) plus `tick` and `look` (the vector previously passed to `stepPlay` as `cameraForwardXZ`). No device id and no frame `dt`.
- `take(n)` — run `n` ticks. Each tick pops the command for that tick (or last look and no buttons), calls `stepPlay`, publishes a `Snapshot`, and appends that tick's `SimEvent` list. `step(input, look)` is enqueue + `take(1)` for tests.
- `snapshot()` / `previousSnapshot()` / `events()` — latest pair and the events from the last `take`.
- `applyDisplay(alpha)` — `interpolateSnapshots` (slerp quaternions) then write `DisplayTransform`. `clearDisplay` removes those components. Simulation `Transform` is never restored from a snapshot.

`stepPlay` remains the inner integrator (collision, motions, pickups). The window thread must not call it as the public play API; `runPlaySelfTests` uses `SimSession::step`. An overload still accepts a `SimEvent` vector and tick for the session.

`Snapshot` holds `tick`, `playerId`, `poses` (`id`, `position`, quaternion `rotation`, `dvec3` `scale`), and HUD: `score`, `health`, `paused`, `message`, `result`, look-ray `lookId` / `lookName` / `lookTag` / `lookPoint`. Not `verticalVelocity`, rests, or tweens.

`SimEvent` is `{kind, id, position, tick}` with `SimEventKind` Pickup, Win, Sound (beep), Effect (hazard particles). The editor `playSimEvents` maps kinds to miniaudio clips and to gold/red particle bursts. `stepPlay` does not spawn particles.

`kCommandBitseryVersion` is 1, `kSnapshotBitseryVersion` is 2, `kSimPlayBitseryVersion` is 4, `kSimEventBitseryVersion` is 1. Tick length is `kPlayStep` (1/30 s). `CommandRecorder` stores enqueue'd commands up to `kMaxRecordedCommands`. Replay (`replayCommands`) takes those ticks in order and does not use `takePlaySteps` or wall clock; hitch leftover stays dropped. When `PlayState::stream` is set, each tick blocks in `ensureReady` until the sim tile ring is settled before the rest of `stepPlay` runs.

## Play session

Play copies the whole scene with `Scene::clone`. Object ids are kept, so the chase camera can still find the player. Stop puts that copy back, which undoes movement and removed pickups. The editor camera from the moment Play was pressed is restored too. Speed stays at whatever the slider was set to.

Input is ignored when the window is not in the foreground or when ImGui wants text. See `docs/input.md` and `docs/cameras.md`.

A win or a loss sets `PlayState::result` and pauses inside `stepPlay`. Escape does not clear that pause. Restart copies the play snapshot back into the live scene, clears the result, and stays in play. Stop still leaves play and restores the editor camera.

## Jobs

`engine/src/Jobs.cpp` owns one `enki::TaskScheduler` and a Jolt `JobSystemThreadPool`. `jobs::init` starts both; `jobs::shutdown` tears them down. `parallelFor` adds an `enki::TaskSet` and waits. Pinned work uses `enki::LambdaPinnedTask`: `runPinned` schedules and waits, `schedulePinned` returns a `PinnedHandle` the caller must keep until `wait()`. Thread 0 is the thread that called `init` (the app main thread). OBJ loads in `MeshObj.cpp` call `runPinned` for the file read. `TileStream` uses `schedulePinned` for `.rtt` bytes. `PhysicsSystem::Update` uses `jobs::joltJobSystem()`.

## Not built

- Moving simulation or render off the window thread so a long `stepPlay` does not block the UI.
- A choice of vsync or an unlocked frame cap.
- Loading progress for a large scene. The first version is one `Loading...` frame.
- Keeping the dropped hitch time for a deterministic wall-clock replay (command-list replay is in `SimReplay.hpp`).
- A second EnTT store so render components are not the objects `stepPlay` mutates. Play-session SQLite is `docs/persistence.md`.
