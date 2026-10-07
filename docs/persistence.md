# Persistence

The edited scene is the file the project writes for the level. `saveScene` in `engine/src/SceneFile.cpp` and `loadScene` in `engine/src/SceneLoad.cpp` use version 1. The format is in `docs/world-and-scenes.md`. Load still accepts a version-1 file from before tags, rotation, roughness, emission, and the lens fields existed.

`raytracer-settings.txt` sits next to the working directory. The editor owns `volume`, `width`, `height`, `samples`, `bounces`, and the `key_*` binds. Everything else on that file is an `EngineSettings` key from `engine/include/EngineSettings.hpp`: play (`move_speed`, `gravity`, `jump_speed`, `ground_probe`, `step_height`, `fall_y`, `action_duration`, `play_ray`, `max_tweens`, `max_play_steps`, `max_frame_dt`, `min_substeps`, `max_substeps`, `resolve_passes`, `max_particles`, `sound_far`), render (`mirrors`, `mirror_reflect`, `mesh_trace_limit`, `job_stack`, `trace_limit`, `mesh_stack`, `bloom_threshold`, `bloom_strength`), debug overlay (`frustum_near`, `frustum_far`), and editor `snap`. The editor reads the file at startup, before the first picture. Settings → Save settings writes every key. A key row writes as soon as the new key is pressed. Mirror bounces writes as soon as the checkbox changes. The title Volume slider in `--game` writes too. Volume and Snap in the World panel take effect immediately and are stored the next time Save settings is pressed. A file that only has `volume` and `snap` still loads those two; missing engine keys keep their defaults. A numeric value that does not parse is skipped, so it does not become 0. `bounces 0` still loads as 0. `mirrors 0` turns mirror bounces off. A file with no `mirrors` line leaves them on. Key lines are names, not numbers.

Play does not write a scene file. Score, the player position, and removed pickups live until Stop, which restores the in-memory snapshot. The editor does not write an ImGui ini.

`--self-test` checks a tag round-trip: a sphere tagged `player` and a plane tagged `solid` come back with those tags, and an object with no tag stays empty.

## Not built

- A play-session save for position, inventory, and flags.
- Slots, autosave, and a save-file version separate from the scene version.
- Bindings for C, V, and Escape.
