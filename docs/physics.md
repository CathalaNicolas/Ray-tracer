# Physics

There is no rigid-body solver. Gravity and jumping exist only inside the player controller described in `docs/collision.md`: `gravity` and `jump_speed` from `EngineSettings` (defaults `-12` and `5`), and a vertical velocity stored on `PlayState`.

Other objects do not fall, push, or sleep. A mesh tagged `solid` is a static box. A pickup disappears on overlap. Nothing has mass, friction, or restitution.

A small game can stay on that controller. A library such as PhysX, Jolt, or Bullet is a later choice, not a dependency of this tree.

## Not built

- Rigid bodies, mass, friction, and restitution.
- Kinematic bodies other than a `platform`. A mesh or sphere tagged `platform` is solid. Its Motion offset carries a player who is standing on it. The player does not push it. The demo platform mesh moves 1.6 units along X over a 4 second period.
- Joints.
- Continuous collision for fast bodies. The player substeps are the only tunneling guard, and they apply to that one sphere.
- Sleeping bodies.
- Vehicles, cloth, soft bodies, and destruction.
