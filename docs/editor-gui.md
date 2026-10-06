# Editor GUI

The editor UI is ImGui. `editor/src/editor.cpp` owns the window and the frame. `EditorUi.cpp` draws the panels, `EditorScene.cpp` the selection and gizmos, `EditorWidgets.cpp` the field editors and the settings file, `EditorPlay.cpp` the chase camera, HUD, pause, and title, and `EditorHistory.cpp` undo. Shared types are in `editor/include/EditorInternal.hpp`. Docking layouts are not saved. `io.IniFilename` is null, so ImGui does not write an ini file.

Move, Rotate, and Scale sit on the toolbar. The matching gizmo is drawn on the selected object while not playing. A drag on a handle pushes one undo step, then moves that object along the world axis, rotates a mesh or a plane, or scales a mesh or a sphere. A click that misses the gizmo still picks an object or orbits.

Hold Ctrl while dragging a move handle, or while dragging a position slider, and the result lands on the Snap spacing from the inspector. That spacing is written as `snap` when Settings → Save settings is pressed.

The Assets list shows mesh and image files in `assets/`. Add mesh loads the selected mesh. Albedo image, Normal map, and Environment map use the selected image.

`--game` hides the toolbars, the outliner, and the inspector. The view, HUD, and pause window remain. Save PNG stays available in the editor while playing.

## What the panels do

- Outliner lists objects, lights, and cameras by name. A search box above the lists filters by case-insensitive substring of the shown name. An empty box shows every row. The Cameras section starts with Editor Camera, then each named shot. Selecting a row switches the game view to that camera. Editor Camera is the default. Selecting an object or a light keeps the current camera view and shows that object or light under Selection. Shift-click adds or removes an object from the selection. The last primary object keeps the gizmo. Move drags every selected object by the same world delta. Duplicate and Delete apply to the whole set.
- Inspector Selection edits the selected object, light, or camera. The Name box and the object Tag box copy the stored string when the selection changes and when that string changes while the same object stays selected. A named camera shows its name, from, target, and field of view. Editor Camera also shows aperture and focus. Orbit, pan, and zoom write back into the camera that is selected. Delete removes a selected named camera and returns to Editor Camera. Editor Camera cannot be deleted.
- **Add** opens a menu: Sphere, Plane, Mesh, Light, and Camera. Mesh uses the file selected in Assets. Camera copies the current view, selects the new shot, and is the same command as Ctrl+Shift+C. The menu is disabled while playing. Delete still removes the selection and still listens for the Delete key while not playing.
- **Edit** opens a menu: Duplicate, Delete, and Save PNG. Duplicate and Delete are disabled while playing. Save PNG stays available when a picture exists.
- **File** opens Save scene and Load scene. Both are disabled while playing. Save writes the Editor Camera as the scene camera, plus any named shots. Load returns the view to Editor Camera.
- **Settings** holds the render size, samples, bounces, Mirror bounces and the other `EngineSettings` sliders (mirror reflect min, mesh trace limit, job stack, trace limit, mesh stack, bloom, play physics, sound far, frustum near/far), the play keys, and Save settings. The first launch uses 640 by 480 unless `raytracer-settings.txt` already has a size. Samples is the still-image grid, disabled while playing. Play draws one sample. Mirror bounces lives on `engineSettings().mirrorBounces`, not a separate editor global. It is on until `mirrors 0` is stored. Turning it off uploads `uMirrorCount` 0 on the next frame and writes the settings file immediately. A key row waits for a letter or Space, then writes the settings file. Save settings writes width, height, samples, bounces, volume, the keys, and every `EngineSettings` key (including snap and mirrors), and the next launch reads them back. A numeric value that does not parse is skipped. `bounces 0` still loads. A missing `mirrors` line stays on. Selection has a Layer combo: Player and camera, or Player only.
- The toolbar is three columns. Add, Edit, File, and Settings are on the left. Play / Stop is the center column. Move, Rotate, Scale, Colliders, Bounce rays, and the status line are on the right.
- Play / Stop is one square button. A triangle means play, a square means stop. Stop opens Keep or Discard. Keep replaces the editor scene with the scene as it is then, and still restores the editor camera. Discard restores the snapshot taken when Play was pressed. See `docs/application-loop.md`. The World and Selection panels stay editable during play. Undo does not replace Stop. Hovering the button shows Play or Stop. The camera list does not follow the chase camera while playing. `--game` stops without the prompt and always discards.
- Undo and redo apply only while not playing. Ctrl+Z undoes. Ctrl+Y and Ctrl+Shift+Z redo. Those keys are ignored when ImGui wants text input. Each snapshot is a `Scene::clone` plus look-from, look-at, field of view, aperture, focus, width, height, samples, bounces, the object, light, and camera selection, the stored Editor Camera pose, and the colliders flag. A snapshot is taken when an edit starts, not on later slider ticks: if any item is activated that frame, the scene from the start of the frame is pushed, and Add, Delete, Duplicate, and Load push on their own before they change the scene. A new edit clears redo. The stack holds 32 snapshots; the newest edit drops the oldest. Play does not record.
- A GPU limit warning appears when a count in `docs/assets.md` is over the cap.
- Frame time of the last GPU render is shown.

## Not built

- Groups that rotate or scale together. Multi-select moves, duplicates, and deletes.
- A console for errors and logs. Load failures use the notice line.
- A separate record of only the inspector fields, apart from the whole scene. Keep stores the scene as it is at Stop, including objects that moved. Discard restores the snapshot from Play.
- A dock layout that is saved.
