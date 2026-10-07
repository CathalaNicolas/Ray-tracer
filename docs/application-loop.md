# Application loop

With `RAYTRACER_DILIGENT`, `editor/src/main.cpp` calls `runEditorDiligent` for windowed / `--game` modes. That loop lives in `editor/src/EditorDiligent.cpp`: SDL window (no OpenGL), `GfxDevice` begin/clear/draw/ImGui/present, and `GfxView` every frame. The legacy Win32+GL loop in `editor/src/editor.cpp` remains for builds without Diligent. Clock helpers `clampFrameDt` and `takePlaySteps` stay in `engine/include/Play.hpp` for the GL play path.

## Modes

| Invocation | What starts |
| --- | --- |
| `raytracer` / `raytracer.exe` | Diligent editor view when built with Diligent; demo scene, orbit camera, quality menu. |
| `raytracer --game` | Same Diligent primary view (play toggle in the menu bar). |
| `raytracer --no-window` | One still image via `RayTracer` (Windows GL host). Optional `--ppm` or `--png`. |
| `raytracer --self-test` | Checks on Windows GL host; stub skip on Linux Diligent bring-up. |

`--game` and `--self-test` do not change scene version 1.

## Diligent startup and shutdown

1. `SDL_Init` + create a resizable window **without** `SDL_WINDOW_OPENGL`.
2. `GfxDevice::init` (D3D12 or Vulkan factory → device → swapchain) and Diligent ImGui.
3. `GfxView::init` (opaque PSO) and optional `GfxFx::init` from `GfxQuality`.
4. Load `createDemoScene()`, upload meshes/lights, enter the frame loop.

Shutdown tears down FX, view, ImGui, device, window, then SDL.

## Legacy GL startup (non-Diligent builds)

1. Register the window class, create the window, and create the OpenGL context.
2. Show the window and start ImGui.
3. Draw one frame that says `Loading...` and swap it.
4. Build the demo scene, then initialize the GPU renderer.
5. Run the frame loop.

Shutdown releases the GPU renderer, the display texture, ImGui, the GL context, and the window.

## One frame

1. Drain Win32 messages. `WM_QUIT` ends the loop. A minimized window sleeps instead of rendering.
2. Measure `frameDt` with `QueryPerformanceCounter`. `clampFrameDt` turns a negative value, or anything above `0.25` s, into `0.25` s.
3. On the first `--game` frame, set `view.playing` and force `frameDt` to `1/60`.
4. Start an ImGui frame and draw the interface. In `--game`, that is the view plus the HUD and pause window.
5. Escape, when ImGui is not capturing text, toggles pause while playing. It does not restore the scene.
6. If Play was just pressed, clone the scene into `playSnapshot`, remember the editor camera, clear `PlayState` (keeping pause if it was already set), and arm the chase camera. If Stop just happened, move the snapshot back and restore that camera.
7. While playing and not paused, the editor shows a speed slider. While paused, the pause window can change that speed, step one tick, resume, or quit.
8. While playing, and not paused or when Step was pressed, run the fixed step, then place the chase camera.
9. Draw the HUD. Pause **Quit** in the editor is Stop. In `--game` it posts `WM_QUIT`.
10. If the scene or camera changed, render one GPU sample. The window still swaps every frame, so ImGui presents at the display rate while the ray tracer only runs when the view changed. While the editor is idle and play is off, keep adding samples up to the Samples slider (`N` means `N×N`, slider 1–4). Play forces one sample.

## Fixed step

`takePlaySteps` decides how many ticks of `kPlayStep` (`1/60` s) to run:

- Normal play adds `frameDt * speed` to an accumulator. Speed is the pause-menu slider, from `0.25` to `2`.
- Each whole `1/60` s becomes one call to `stepPlay`, then the chase camera is placed again.
- At most four steps run in one window frame. If time is still left, the accumulator is cleared. The simulation slows during the spike instead of spending the whole frame catching up.
- Step, while paused, runs exactly one tick and does not change the accumulator. Held movement keys still apply. Mouse look does not, until Resume.
- Pause without Step adds no time.

A display faster than 60 Hz presents several window frames between simulation ticks. The picture stays on the latest tick until the next one. Smoothing between those ticks is later work.

## Play session

Play copies the whole scene with `Scene::clone`. Object ids are kept, so the chase camera can still find the player. Stop puts that copy back, which undoes movement and removed pickups. The editor camera from the moment Play was pressed is restored too. Speed stays at whatever the slider was set to.

Input is ignored when the window is not in the foreground or when ImGui wants text. See `docs/input.md` and `docs/cameras.md`.

A win or a loss sets `PlayState::result` and pauses inside `stepPlay`. Escape does not clear that pause. Restart copies the play snapshot back into the live scene, clears the result, and stays in play. Stop still leaves play and restores the editor camera.

## Not built

- A second thread so a long simulation step does not block the window.
- Interpolation between simulation steps when the display is faster than 60 Hz.
- A choice of vsync or an unlocked frame cap.
- Loading progress for a large scene. The first version is one `Loading...` frame.
- Keeping the dropped hitch time for a deterministic replay.
