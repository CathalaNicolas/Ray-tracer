# Audio

Three one-shot clips exist. After each `SimSession::take`, `playSimEvents` in `editor/src/EditorPlay.cpp` maps that tick's `SimEvent` list to one clip and `playGameSound` in `engine/src/Sound.cpp` plays it through miniaudio (`ma_sound_init_from_file` on a registered in-memory WAV). The same function spawns view-only particle bursts (`docs/effects.md`). `assets/music.wav` is a two-second loop. Play calls `startMusic()`, which streams that file with looping enabled. Stop calls `stopMusic()`, which uninitializes the music `ma_sound`. The master volume slider sets the looping sound's `ma_sound_set_volume` from 0 to 1 and is multiplied into each one-shot's volume. Volume 0 leaves the loop running at silence. A one-shot does not stop the loop.

A tick plays a clip when it emitted Pickup, Win, or Sound (Effect is particles only):

- `SimEventKind::Win` plays the win tone. This wins over a pickup in the same tick.
- `SimEventKind::Pickup` plays the higher blip.
- `SimEventKind::Sound` plays the beep. That includes `"Respawned"`, `"Press F"`, `"Next room"`, `"You lose"`, and trigger/use text.

Several ticks in one hitch play one clip per tick, last write still replacing the previous `ma_sound`. HUD text still uses `PlayState::message` / `Snapshot::message`. The editor does not compare those strings to choose a clip.

Each clip loads a WAV from `assets/` the first time that sound is needed: `beep.wav`, `pickup.wav`, or `win.wav`. The files are 22050 Hz, mono, 16-bit. If a file is missing or not a RIFF WAV, the same clip is built in memory: one sine, linear fade `1 - i/count`, peak 28000. Those bytes stay registered with miniaudio's resource manager as `game://beep`, `game://pickup`, and `game://win`. `0` plays nothing (gain at or below `0.0001` returns without starting a sound). The Volume slider changes the looping track immediately and is applied to the next one-shot. Settings → Save settings writes `volume` into `raytracer-settings.txt`, and the next launch reads it back.

The listener is the camera position after that step's chase update (`view.lookFrom`). `playGameSound` takes the event position from `PlayState.eventAt`: the pickup, trigger, hazard, use, or goal that caused the message, the spawn point on a respawn, or the player center on a fall. Gain is `master * soundDistanceFade(distance)`, where fade is `max(0, 1 - distance / sound_far)` and `sound_far` defaults to 16 in `EngineSettings`. At the listener the one-shot is full master volume. At `sound_far` it is silent. Music is not faded by distance. miniaudio spatialization is off on these sounds; distance uses that linear fade only.

| Clip | Frequency | Samples | Length |
| --- | --- | --- | --- |
| Beep | 880 Hz | 1540 | 1540/22050 s |
| Pickup | 1760 Hz | 882 | 0.04 s |
| Win | 660 Hz | 2205 | 0.1 s |

`ma_engine_init` runs on the first `playGameSound` or `startMusic`. Playback does not block the frame. Before a new one-shot is created, the previous `ma_sound` is uninitialized so only one effects clip runs at a time. `Sound.obj` is compiled from `engine/src/Sound.cpp`. `raytracer_audio` also compiles miniaudio (`engine/src/Miniaudio.cpp`, `MINIAUDIO_IMPLEMENTATION`) and links `miniaudio::miniaudio`. `Sound.cpp` does not use winmm.

The same event always plays the same clip. Volume is one master gain times the distance fade above. Music uses that master gain only.

## Not built

- One-shot effects beyond the three clips, or music other than `assets/music.wav`.
- Music and effects buses. Master volume is the slider above.
- A falloff curve other than the linear fade to `sound_far`.
- Variation so a repeated action is not the same tone.
- miniaudio 3D listener/attenuation (positions feed `soundDistanceFade` only).
