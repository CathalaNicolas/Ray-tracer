# Audio

Three one-shot clips exist. After each play step, `playScoreBeep` in `editor/src/editor.cpp` picks one and `playGameSound` in `engine/src/Sound.cpp` plays it through `PlaySoundA`. `assets/music.wav` is a two-second loop. Play opens it with MCI `play raymusic repeat`. Stop closes it. The master volume slider sets the MCI volume from 0 to 1000 and still scales the one-shots. Volume 0 leaves the loop running at silence. A one-shot does not stop the loop.

The step plays a clip when the score increases, the message changes to something non-empty, or `result` becomes `"won"`. `playScoreBeep` then chooses:

- A win (`message` is `"You win"`, or `result` just became `"won"`) plays the win tone. This wins over a pickup in the same step.
- A pickup (the score increased, or `message` is `"Picked up"`) plays the higher blip.
- Every other message keeps the beep. That includes `"Respawned"`, `"Press F"`, `"Next room"`, `"You lose"`, and trigger text. A score increase in that same step still plays the pickup blip, unless the step was also a win.

Each clip loads a WAV from `assets/` the first time that sound is needed: `beep.wav`, `pickup.wav`, or `win.wav`. The files are 22050 Hz, mono, 16-bit. If a file is missing or not a RIFF WAV, the same clip is built in memory: one sine, linear fade `1 - i/count`, peak 28000. Master volume scales those samples. `0` plays nothing. The Volume slider changes the level immediately. Settings → Save settings writes `volume` into `raytracer-settings.txt`, and the next launch reads it back.

The listener is the camera position after that step's chase update (`view.lookFrom`). `playGameSound` takes the event position from `PlayState.eventAt`: the pickup, trigger, hazard, use, or goal that caused the message, the spawn point on a respawn, or the player center on a fall. Gain is `master * soundDistanceFade(distance)`, where fade is `max(0, 1 - distance / sound_far)` and `sound_far` defaults to 16 in `EngineSettings`. At the listener the one-shot is full master volume. At `sound_far` it is silent. Music is not faded by distance.

| Clip | Frequency | Samples | Length |
| --- | --- | --- | --- |
| Beep | 880 Hz | 1540 | 1540/22050 s |
| Pickup | 1760 Hz | 882 | 0.04 s |
| Win | 660 Hz | 2205 | 0.1 s |

The flags are `SND_ASYNC | SND_MEMORY | SND_NODEFAULT`, so playback does not block the frame. Before a new clip is written into the memory buffer, `PlaySoundA(nullptr, ...)` stops the previous one so that buffer can be freed. A new clip then replaces it. The Windows link line includes `winmm.lib`. `Sound.obj` is compiled from `engine/src/Sound.cpp`.

The same event always plays the same clip. Volume is one master gain times the distance fade above. Music uses that master gain only.

## Not built

- Looping music and one-shot effects beyond the three clips.
- Music and effects buses. Master volume is the slider above.
- A falloff curve other than the linear fade to `sound_far`.
- Variation so a repeated action is not the same tone.
