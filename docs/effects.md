# Effects and juice

A pickup and a hazard spawn eight particles. Each particle is a position, a velocity, a color, a life of 0.45 seconds, and a size of 0.07. `Scene::advanceParticles` moves them and drops them when life reaches 0. The shading shader draws up to 16 of them as camera-facing discs in the same pass as the scene. They are not saved in the scene file. There is no shake. A Use tween is the move and rotate in `docs/animation.md`. The pickup beep in `docs/audio.md` still plays.

Material emission, reflections, and soft shadows are shading, documented in `docs/materials.md` and `docs/lighting.md`, not gameplay effects.

## Not built

- Screen shake, hit pause, and camera punch.
- Particle bursts besides the pickup and the hazard.
- Tweening an object to a point over time.
- Trails and decals.
- Rumble.
