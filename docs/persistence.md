# Persistence

The edited scene is the file the project writes for the level. `saveScene` in `engine/src/SceneFile.cpp` and `loadScene` in `engine/src/SceneLoad.cpp` use version 2 (64-bit ids and `next_id`). Version 1 still loads. The format is in `docs/world-and-scenes.md`. Load still accepts a version-1 file from before tags, rotation, roughness, emission, and the lens fields existed. Euler degrees in the file use yaw-then-pitch-then-roll, the inverse of `quatFromEulerDegrees`.

`raytracer-settings.txt` sits next to the working directory. The editor owns `volume`, `width`, `height`, `samples`, `bounces`, and the `key_*` binds. Everything else on that file is an `EngineSettings` key from `engine/include/EngineSettings.hpp`: play (`move_speed`, `gravity`, `jump_speed`, `ground_probe`, `step_height`, `fall_y`, `action_duration`, `play_ray`, `max_tweens`, `max_play_steps`, `max_frame_dt`, `min_substeps`, `max_substeps`, `resolve_passes`, `max_particles`, `sound_far`), render (`mirrors`, `mirror_reflect`, `mesh_trace_limit`, `job_stack`, `trace_limit`, `mesh_stack`, `bloom_threshold`, `bloom_strength`, `view_distance`, `shadow_map`, `particle_density`), debug overlay (`frustum_near`, `frustum_far`), and editor `snap`. The editor reads the file at startup, before the first picture. Settings → Save settings writes every key. A key row writes as soon as the new key is pressed. Mirror bounces writes as soon as the checkbox changes. The title Volume slider in `--game` writes too. Volume and Snap in the World panel take effect immediately and are stored the next time Save settings is pressed. A file that only has `volume` and `snap` still loads those two; missing engine keys keep their defaults. A numeric value that does not parse is skipped, so it does not become 0. `bounces 0` still loads as 0. `mirrors 0` turns mirror bounces off. A file with no `mirrors` line leaves them on. Key lines are names, not numbers.

Play does not write a scene file. Score, the player position, and removed pickups live until Stop, which restores the in-memory snapshot. The editor does not write an ImGui ini.

`--self-test` checks a tag round-trip: a sphere tagged `player` and a plane tagged `solid` come back with those tags, and an object with no tag stays empty.

`engine/include/SimSerialize.hpp` encodes and decodes versioned bitsery blobs (`kCommandBitseryVersion` 1, `kSnapshotBitseryVersion` 2, `kSimEventBitseryVersion` 1, `kSimPlayBitseryVersion` 4). Field order follows the struct members; each record is wrapped in bitsery `Growable` so later fields can append. Mesh triangle soup is not in the blob; `MeshShape::sourcePath` (and stamp/bytes) is stored so geometry reloads from disk. Terrain uses `SimShapeKind::Terrain` with `tileX` / `tileZ` (and an optional path). `applySimPlay` resolves terrain from `cooked/t{x}_{z}.rtt` via PhysFS when present, otherwise from `meshSourcePath`. It checks that every mesh path and every terrain source exists (or that the entity is already loaded) before it removes extras or mutates the scene. A missing source returns false and leaves the scene unchanged.

In-memory API:

- `encodeCommand` / `decodeCommand`, `encodeSnapshot` / `decodeSnapshot`, `encodeSimEvent` / `decodeSimEvent`
- `encodeCommandList` / `decodeCommandList` for a recorded tick list
- `encodeSimEntityRecord` / `decodeSimEntityRecord` for one entity row
- `captureSimPlay` / `applySimPlay` plus `encodeSimPlayBlob` / `decodeSimPlayBlob` for a play-session restore blob: tick, `nextId`, last look, PlayState HUD (`score`, `health`, `paused`, `message`, `result`, look-ray, `room`, `motionTime`, rests, tweens, `verticalVelocity`, `simSeed`, `mt19937` stream as text), per-entity sim components (parent, GLM transform doubles including quaternion and scale vector, sphere/plane/mesh/terrain shape scalars including terrain `tileX`/`tileZ`, **material**, motion, action, spawner clocks, name/tag/layer), the Jolt capsule (`hasCapsule`, feet, velocity, radius, grounded), `CharacterVirtual::SaveState` bytes, and `PhysicsSystem::SaveState` bytes. `applySimPlay` calls `primePlayPhysics` so the next play tick already has a World, restored bodies, and a restored character.

Replay of commands is tick-by-tick (`SimReplay.hpp`); it does not write a file.

## Play-session SQLite

`engine/include/SimSave.hpp` writes one SQLite file. The default path is `raytracer-play.sqlite` in the working directory, next to `raytracer-settings.txt` (`defaultPlaySessionPath()`). Callers can pass another path (tests use a temp file).

On open: `PRAGMA journal_mode=WAL` and `PRAGMA synchronous=FULL`. A save is one `BEGIN IMMEDIATE` … `COMMIT`. A crash during the write leaves the previous committed session or the new one, not a mix. Format key `meta.format_version` is `kPlaySessionDbVersion` (1). This is not networking and not a scene file.

Schema:

| Table | Key | Contents |
| --- | --- | --- |
| `meta` | `key` TEXT | `format_version`, `sim_play_bitsery` |
| `world` | `id` INTEGER, always 0 | `tick`, `seed`, `sim_play` BLOB (`encodeSimPlayBlob`), `commands` BLOB (`encodeCommandList`) |
| `entity` | `entity_id` INTEGER (`EntityId`) | bitsery `SimEntityRecord` (same fields as W6, including mesh `sourcePath` and material, not triangles) |
| `character` | `entity_id` INTEGER | the player row, same blob as that entity |

API: `savePlaySession(path, scene, state, tick, lastLook, commands)` captures with `captureSimPlay` then writes. `loadPlaySession` reads `world.sim_play`, `decodeSimPlayBlob`, `applySimPlay` (drops the old Jolt world, rebuilds solids, restores `PhysicsSystem` then `CharacterVirtual`), and fills `PlaySessionInfo` (`tick`, `lastLook`, `seed`, decoded command list).

While Play is on, File → Save play session / Load play session use `defaultPlaySessionPath()` (`raytracer-play.sqlite` in the working directory). Save writes the live simulation scene (not interpolated display poses), `PlayState`, tick, last look, and the `CommandRecorder` list. Load applies that file, replaces the recorder, and `SimSession::primeFromCurrent` so the next ticks continue from the stored tick. `--game` has no File menu. Particles are not in the blob. Prefab fields and lights are still not in the play blob; a save is meant to resume on the scene it came from.

`tests.exe` saves 14 ticks in (including a jump), loads into a fresh scene, and requires the load to match the live session at the save point, including capsule feet, velocity, and grounded. The live session then runs 300 more ticks; the loaded session must stay bit-identical with that uninterrupted run.

## Not built

- Save slots and autosave.
- A `map` table or inventory; character/world grouping is the player row plus the full entity list.
- Lights and prefab metadata in the play blob.
- Bindings for C, V, and Escape.
