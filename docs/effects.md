# Effects and juice

Pickup (gold) and hazard (red) bursts spawn on the view, not inside `stepPlay`. After `SimSession::take`, `playSimEvents` in `editor/src/EditorPlay.cpp` walks that batch's `SimEvent` list. Each `SimEventKind::Pickup` adds eight discs at `event.position` with color `(0.95, 0.85, 0.25)`. Each `SimEventKind::Effect` does the same in `(0.9, 0.2, 0.15)`. Headless `stepPlay` / `SimSession::step` only emit the events; `Scene::particles()` stays empty.

Each particle is a position, a velocity, a color, a life of 0.45 seconds, and a size of 0.07. `Scene::addParticle` drops new discs once `particles_.size()` reaches `max(1, min(256, round(maxParticles * particleDensity)))`. `max_particles` defaults to 32 and `particle_density` to 1. While playing, `editor/src/editor.cpp` calls `Scene::advanceParticles(frameDt)` every window frame (including pause) so they keep moving when the display is faster than 30 Hz. They drop when life reaches 0. The shading shader draws up to 16 of them as camera-facing discs in the same pass as the scene. They are not saved in the scene file or in the play-session SQLite blob. `Scene::clone` does not copy the particle list. Play start, Restart, and Stop clear the list.

There is no shake. A Use tween is the move and rotate in `docs/animation.md`. HUD text still uses `PlayState::message`. Audio for the same tick is also in `playSimEvents` (`docs/audio.md`).

## Not built

- Screen shake, hit pause, and camera punch.
- Particle bursts besides pickup gold and hazard red.
- Tweening an object to a point over time.
- Trails and decals.
- Rumble.
- A GPU particle system (Effekseer or similar).
