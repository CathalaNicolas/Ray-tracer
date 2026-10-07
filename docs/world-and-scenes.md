# World and scenes

One `Scene` is in memory. It owns the objects, the lights, and the background. Files live in `engine/include/Scene.hpp`, `engine/include/SceneFile.hpp`, `engine/src/SceneFile.cpp` (save), and `engine/src/SceneLoad.cpp` (load). The demo is built in `engine/include/DemoScene.hpp`.

## Objects

Every object is a `Hittable`:

- `id` — stable integer. `Scene::add` assigns the next id when `id` is 0 or negative. A positive id is kept, and the counter moves past it.
- `name` — display name. Quotes in the file are escaped.
- `tag` — one string. Empty means untagged. Play reads `player`, `solid`, `pickup`, `trigger`, `goal`, `use`, `hazard`, `spawn`, and `spawner`. Any other string is stored and saved, and play treats it as neither of those roles.
- `motion` — optional move, rotate, scale, and period. See `docs/animation.md`.
- `spawnEvery` — seconds between pickups for a `spawner`. `spawnClock` and `spawnedId` are runtime only and are not written to the file.

Kinds today are `Sphere`, `Plane`, and `Mesh`. `Scene::find`, `remove`, and `clone` work by id. `clone` copies lights, ambient, background, environment path, exposure, fog, the id counter, and every object through `Hittable::clone`. Mesh clones share geometry and keep their id. The editor Duplicate button clears the id before `add`, so the copy gets a new one, and nudges it by `(0.55, 0, 0.55)`.

Each object has a `parentId`. `0` means no parent. The scene file stores the local position. The world position applies ancestor mesh rotation and scale as well as translation. A prefab is an object whose `prefab` name is set. The file tail is `prefab "Name"`, omitted when empty. Add → Prefab clones that object and its children and marks the clone with `instance "Name"`. `syncPrefabInstances` copies material, tag, layer, motion, scale, rotation, and child local placement onto each instance. A second call with the same source fields skips that copy. Layer `0` is omitted. Layer `1` is the tail `layer 1`. The instance root position stays where it was placed. One `Scene` is live. Play can replace that scene when the campaign advances. A pickup removed during play is gone until Stop restores the snapshot. A `spawner` can add a yellow pickup while the round is running. That pickup is not in the snapshot, so Stop and Restart remove it.

## File format

The first line is `raytracer-scene 1`. Any other version is rejected. Older version-1 files still load: `tag`, `rot`, `radius`, `directional`, `parent`, aperture, focus, roughness, emission, and normal maps are optional tails.

World lines:

- `ambient r g b`
- `background` horizon RGB, then zenith RGB
- `camera` look-from, look-at, vertical field of view in degrees, then optional aperture and focus distance
- `exposure` value, defaulting when the line is absent
- `fog r g b density`, omitted when the density is 0
- `environment "path"` when a map is set
- `shot "Name" fx fy fz ax ay az fov` for each named camera, omitted when there are none

Object lines, one per object:

- `sphere "name" cx cy cz radius` then the material, then an optional `tag "..."`, then optional `motion`, `every`, `act`, and `parent <id>`.
- `plane "name" px py pz nx ny nz` then the material, an optional `checker r g b scale`, then an optional tag, then optional `motion`, `every`, `act`, and `parent <id>`.
- `mesh "name" px py pz scale` then the material, an optional `rot pitch yaw roll` in degrees, then `obj "path"` or `fbx "path"`, then an optional tag, then optional `motion`, `every`, `act`, and `parent <id>`.

`cx cy cz`, `px py pz` on these lines are local. `parent <id>` is omitted when the id is 0.

`motion` is eight numbers: move xyz, rotate xyz in degrees, a scale offset, and a period in seconds. `every` is one number of seconds. `act "Name" x y z` is a Use target and a move offset. `rx ry rz` follows when the rotation in degrees is not zero. An older `act` with only the move still loads. All three fields are omitted when unused. An older file that has none of them still loads. `act` does not store whether it has already played.

A light line is `light "name" px py pz cr cg cb intensity falloff`, then optional `radius` and `directional`.

Material numbers on those lines are albedo, ambient, diffuse, specular, shininess, reflectivity, transmission, ior, then optional `uv`, `map`, `rough`, `emit`, and `normal`. See `docs/materials.md`.

Save uses 17 digits of precision. A kind the writer does not know fails the save instead of dropping the object.

## Demo rooms

`PlayState::room` is not stored in the scene file. `0` means the scene is not the campaign. The editor sets `room` to `1` when Play starts and again when Restart starts a round.

Room 1 is `createDemoScene`: a checker ground tagged `solid`, the colored spheres, a glass sphere, a blue player sphere at `(0, 0.5, 2.2)` with radius `0.35`, three yellow pickups, a green `Goal`, a dark red `Hazard`, a purple `Switch`, a `Door` the switch lifts, a grey `Step` pebble, a moving platform, a small cyan `Spawn` marker, an orange `Spawner`, and the tree. The tree root stays at `(-2.4, 0, -0.15)`. Trunk and crown are tagged `solid`. Two lights are named Key and Fill. The platform is `assets/platform.obj`. The door is `assets/door.obj`. That object set is what a save of the demo writes. The file stays version 1.

Room 2 is `createEastScene` in `engine/include/DemoScene.hpp`. It is four objects and no mesh: a checker ground tagged `solid` with checker scale `0.7`, a blue player sphere named `Player` at `(0, 0.5, 1.6)` with radius `0.35`, one yellow pickup named `Pickup` at `(-1.2, 0.22, 0.4)` with radius `0.22`, and a green `Goal` at `(1.5, 0.45, -0.8)` with radius `0.45`. It is built in code when play advances. It is not a second scene file.

The swap runs in `collectPlayEvents` after the overlap loop and after pickup removals. While `room` is `1`, Use on a `goal` sets a flag and does not end the round. After that loop, if the player has not lost, the live scene is replaced with `createEastScene()`. `room` becomes `2`, the message becomes `Next room`, `verticalVelocity` becomes `0`, and `paused` stays false. Score, health, and `result` stay as they were. `rests` is cleared so room 1 motion poses are not applied to the new ids. `playerId` becomes the east room's player sphere, which is already at `(0, 0.5, 1.6)`. The chase camera follows that id on the next place. While `room` is `0` or `2`, Use on a `goal` still sets `result` to `won`, the message to `You win`, and `paused`.

Play start copies the editor scene into the play snapshot, then sets `room` to `1`. Stop puts that snapshot back, so leaving play returns to room 1 as it was when Play was pressed, including pickups taken or spawned during the round. Restart clones that same snapshot and then sets `room` to `1`, which starts the campaign over in room 1 with score `0` and health `3`. Editor undo is a separate stack and does not record during play. Stop is still the only restore of the play snapshot.

## Not built

- A scene list in the file, or any transition besides the room 1 goal.
- A prefab stored outside the scene file.
- Layers in addition to the one tag string.
- A destroy that survives Stop. Spawn during play exists, and Stop still throws the new objects away.
- Streaming.
