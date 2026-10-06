# Application loop

The window, the play simulation, and the GPU render share one thread in `editor/src/editor.cpp`. `editor/src/main.cpp` chooses the mode and then calls `runEditor`. The clock helpers are `clampFrameDt` and `takePlaySteps` in `engine/include/Play.hpp`.

## Modes

| Invocation | What starts |
| --- | --- |
| `raytracer.exe` | Editor. The demo scene is loaded. Play is off. |
| `raytracer.exe --game` | Same window, title `Game`. A title screen with Play, Volume, and Quit sits in front of the round. Play starts the round. Toolbars, the outliner, and the inspector stay hidden. |
| `raytracer.exe --no-window` | One still image, no window. Optional `--ppm` or `--png`. |
| `raytracer.exe --self-test` | Checks, then exit. No window. |

`--game` and `--self-test` do not change scene version 1.

## Startup and shutdown

1. Register the window class, create the window, and create the OpenGL context.
2. Show the window and start ImGui.
3. Draw one frame that says `Loading...` and swap it.
4. Build the demo scene, then initialize the GPU renderer. The performance clock starts after that, so the load does not become the first play frame.
5. Run the frame loop.

Shutdown releases the GPU renderer, the display texture, ImGui, the GL context, and the window, then unregisters the class. A failed GL context destroys the window and returns before the loop.

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
