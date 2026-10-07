# World and scenes

One `Scene` is in memory. It owns an `entt::registry`, stable 64-bit ids and `Object` handles, ordered entity iteration, lights, shots, particles, and the background.

## Objects

Every object is a non-owning `Object { Scene*, EntityId }` handle over registry components:

- `id` — stable `uint64_t`; 0 is invalid. Shape factories assign the next id unless the loader supplies one.
- `name` — display name. Quotes in the file are escaped.
- `tag` — one string. Empty means untagged. Play reads `player`, `solid`, `pickup`, `trigger`, `goal`, `use`, `hazard`, `spawn`, and `spawner`. Any other string is stored and saved, and play treats it as neither of those roles.
- `motion` — optional move, rotate, scale, and period. See `docs/animation.md`.
- `spawnEvery` — seconds between pickups for a `spawner`. `spawnClock` and `spawnedId` are runtime only and are not written to the file.

Kinds today are `SphereShape`, `PlaneShape`, `MeshShape`, and `TerrainTileComponent` (a mesh plus a heightmap). `Scene::find`, `remove`, and `clone` work by `EntityId`. Scene cloning preserves ids; editor duplication clones into the live scene, receives a fresh id, and nudges the local position by `(0.55, 0, 0.55)`. Mesh geometry remains shared.

Each object has a `parentId`. `0` means no parent. The scene file stores the local pose. World matrices apply ancestor translation, rotation, and scale to every shape. A prefab is an object whose `prefab` name is set. The file tail is `prefab "Name"`, omitted when empty. Add → Prefab clones that object and its children and marks the clone with `instance "Name"`. `syncPrefabInstances` copies material, tag, layer, motion, scale, rotation, and child local placement onto each instance. A second call with the same source fields skips that copy. Layer `0` is omitted. Layer `1` is the tail `layer 1`. The instance root position stays where it was placed. One `Scene` is live. Play can replace that scene when the campaign advances. A pickup removed during play is gone until Stop restores the snapshot. A `spawner` can add a yellow pickup while the round is running. That pickup is not in the snapshot, so Stop and Restart remove it.

## File format

The file writer emits `raytracer-scene 2`, then `next_id <n>`, and writes `id <n>` on every object. Version-1 files still load and receive sequential ids.

World lines:

- `ambient r g b`
- `background` horizon RGB, then zenith RGB
- `camera` look-from, look-at, vertical field of view in degrees, then optional aperture and focus distance
- `exposure` value, defaulting when the line is absent
- `fog r g b density`, omitted when the density is 0
- `environment "path"` when a map is set
- `map <id> "name" continent|dungeon <tileSize> <tilesX> <tilesZ>`
- `liquid water|lava xmin ymin zmin xmax ymax zmax surfaceY` for each volume
- `shot "Name" fx fy fz ax ay az fov` for each named camera, omitted when there are none

Object lines, one per object:

- `sphere "name" cx cy cz radius` then the material, then optional `tag`, `motion`, `every`, `act`, `parent`, and `id`.
- `plane "name" px py pz nx ny nz` then the material, an optional `checker r g b scale`, then optional tag, motion, every, act, parent, and `id`.
- `mesh "name" px py pz scale` then the material, an optional `rot pitch yaw roll` in degrees, then `obj "path"` or `fbx "path"`, then optional tag, motion, every, act, parent, and `id`.
- `terrain "name" px py pz scale` then the material, optional `rot`, then `rtt "path"`, then the same optional tail. See `docs/terrain.md`.

`cx cy cz`, `px py pz` on these lines are local. `parent <id>` is omitted when the id is 0.

`motion` is eight numbers: move xyz, rotate xyz in degrees, a scale offset, and a period in seconds. `every` is one number of seconds. `act "Name" x y z` is a Use target and a move offset. `rx ry rz` follows when the rotation in degrees is not zero. An older `act` with only the move still loads. All three fields are omitted when unused. An older file that has none of them still loads. `act` does not store whether it has already played.

A light line is `light "name" px py pz cr cg cb intensity falloff`, then optional `radius` and `directional`.

Material numbers on those lines are albedo, ambient, diffuse, specular, shininess, reflectivity, transmission, ior, then optional `uv`, `map`, `rough`, `emit`, and `normal`. See `docs/materials.md`.

Save uses 17 digits of precision. A kind the writer does not know fails the save instead of dropping the object.

## Demo rooms

`PlayState::room` is not stored in the scene file. `0` means the scene is not the campaign. The editor sets `room` to `1` when Play starts and again when Restart starts a round.

Room 1 is `createDemoScene`: a checker ground, gameplay spheres, door, moving platform, tree, and two lights. A save of the demo uses scene version 2.

Room 2 is `createEastScene` in `engine/include/DemoScene.hpp`. It is four objects and no mesh: a checker ground tagged `solid` with checker scale `0.7`, a blue player sphere named `Player` at `(0, 0.5, 1.6)` with radius `0.35`, one yellow pickup named `Pickup` at `(-1.2, 0.22, 0.4)` with radius `0.22`, and a green `Goal` at `(1.5, 0.45, -0.8)` with radius `0.45`. It is built in code when play advances. It is not a second scene file.

The swap runs in `collectPlayEvents` after the overlap loop and after pickup removals. While `room` is `1`, Use on a `goal` sets a flag and does not end the round. After that loop, if the player has not lost, the live scene is replaced with `createEastScene()`. `room` becomes `2`, the message becomes `Next room`, `verticalVelocity` becomes `0`, and `paused` stays false. Score, health, and `result` stay as they were. `rests` is cleared so room 1 motion poses are not applied to the new ids. `playerId` becomes the east room's player sphere, which is already at `(0, 0.5, 1.6)`. The chase camera follows that id on the next place. While `room` is `0` or `2`, Use on a `goal` still sets `result` to `won`, the message to `You win`, and `paused`.

Play start copies the editor scene into the play snapshot, then sets `room` to `1`. Stop puts that snapshot back, so leaving play returns to room 1 as it was when Play was pressed, including pickups taken or spawned during the round. Restart clones that same snapshot and then sets `room` to `1`, which starts the campaign over in room 1 with score `0` and health `3`. Editor undo is a separate stack and does not record during play. Stop is still the only restore of the play snapshot.

## Not built

- A scene list in the file, or any transition besides the room 1 goal.
- A prefab stored outside the scene file.
- Layers in addition to the one tag string.
- A destroy that survives Stop. Spawn during play exists, and Stop still throws the new objects away.
- Streaming is `TileStream` (`docs/streaming.md`), off unless play sets `PlayState::stream`.
- Interior cells and portals. Map kind and liquid volumes exist; see `docs/terrain.md`.
- A second map instance.
