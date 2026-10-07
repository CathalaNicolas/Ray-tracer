# Cameras

Two cameras exist. Both are a look-from point, a look-at point, and up `(0, 1, 0)`. `engine/include/Camera.hpp` turns that into a pinhole ray, plus an optional lens.

## Editor camera

Orbit, pan, and zoom run only while play is off. Field of view, aperture, and focus distance are on the view and are saved with the scene. Focus distance defaults to the look-from / look-at distance when the file omits it.

Aperture `0`, or a single sample, is a pinhole. Aperture above zero with more than one sample offsets the ray origin on a Vogel disk and aims at the focus distance. The clip matrix used for the raster meshes is `cameraClipMatrix`. Leave that function intact when changing the lens.

Play stores the editor camera and puts it back on Stop.

## Chase camera

While playing, orbit, pan, and zoom are off. If `PlayState::playerId` is a sphere, the camera sits on a sphere around the player:

- Distance `4.5`.
- Look-at is the player center plus `(0, 0.4, 0)`.
- Yaw and pitch come from the mouse. Pitch is clamped to `-1.2..1.2` radians. Sensitivity is `0.005` radians per pixel.
- On Play, yaw is taken from the editor view so the chase starts behind the current forward, and pitch starts at `0.4`.

Mouse look is ignored when the window is in the background. The camera is placed again after every simulation step so it follows the player. `chaseCameraPosition` then casts from the player center toward that point. It uses `play_detail::isSolid` with `forCamera` true and stops `0.3` short of the first layer-0 solid, so the eye does not enter the tree. Layer 1, including the demo door, is skipped by that same test and still blocks the player. It never comes closer than `0.5`. Pickups, the goal, the switch, and hazards are not solids, so they do not pull the camera in. If there is no player sphere, the editor camera stays where it was and the HUD says `No player`.

While chase is active, distance stays `4.5`, `chaseCameraPosition` still pulls the eye in, and pitch stays clamped to `-1.2..1.2`. Nothing splits the view.

## First person

C toggles this while playing. Chase is the default, including when Play or Restart arms the camera. The edge is a press: holding C does not toggle again, and the next press returns to chase. The press is ignored when play is paused, when the round is over (`PlayState::result` is `won` or `lost`), when ImGui wants text input, or when the window is not the foreground window.

The eye is the player center plus `(0, 0.2, 0)`, then moved forward by the sphere radius plus `0.08` along `(-sin(yaw), 0, -cos(yaw))`. That is the horizontal look direction from the same yaw the mouse updates. Look-at is the eye plus `(-sin(yaw) cos(pitch), -sin(pitch), -cos(yaw) cos(pitch))`, so pitch matches the chase camera. Sensitivity stays `0.005` radians per pixel. The player sphere stays in the scene. This mode does not call `chaseCameraPosition`.

The play HUD draws `First person` while the mode is on. C does nothing while a named camera is showing.

## Named cameras

A scene can store shots. Each shot is a name, a look-from point, a look-at point, and a field of view. The file line is `shot "Name" fx fy fz ax ay az fov`, omitted when there are none. The left panel lists Editor Camera, then each shot. Selecting a row copies that camera into the game view. Editor Camera is selected at startup and after Load scene. Its pose is stored separately, so switching back restores it. Add → Camera and Ctrl+Shift+C copy the current view into a new shot and select it. Orbit and the Selection fields write back into the selected shot. Delete removes that shot and returns to Editor Camera. During play, V steps to the next shot and then back to chase. The view blends from the current pose to the destination over 0.35 seconds with a smoothstep. A named shot's destination is fixed. The chase destination is sampled again each frame, so the blend follows the player. Pressing V during a blend starts a new one from the current pose. Mouse look waits until the blend is finished and chase or first person is back. The HUD shows the shot name. The outliner selection does not change the chase camera while playing.

## Not built

- Camera collision against a layer-1 solid. That layer is for the player only.
- A fixed camera or a rail.
- A blend longer than the 0.35 second V step, or a blend the editor can author.
- A camera rail.
- Split screen.
