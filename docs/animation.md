# Animation

FBX import still reads static node transforms and bakes them into the triangle soup. Clips, bones, and shape keys are not loaded.

Play can ping-pong a transform in C++. Each object has a `Motion` component: a move offset, a rotate offset in Euler degrees, a scale offset, and a period in seconds (default 4, never below 0.05). Play converts that period to ticks at `kPlayStep` (1/30 s; 4 s is 120 ticks). The inspector edits those fields, plus Spawn every. Save writes them only when they are in use, as optional tails, so an older scene file still loads:

- `motion ox oy oz rx ry rz ds period`
- `every seconds` for a spawner

The first play step copies the current placement into `PlayState::rests` and does not change it again. Later steps set the live placement to rest plus offset times a wave. `motionTime` is the tick count. The wave is 0 at tick 0, 1 at one period, and 0 again at two periods. The player object is skipped. Stop and Restart restore the snapshot, which was taken before any step, so the saved pose is the rest pose and `motionTime` goes back to 0.

What each channel writes:

- Move changes a sphere center, a mesh position, or a plane point.
- Rotate changes a mesh's Euler angles. A sphere or a plane ignores it.
- Scale changes a mesh's uniform scale, or a sphere's radius. The result stays at least 0.01.

If the player is standing on a solid that moves, Jolt ground velocity carries the capsule. Rotation and scale still change the mesh, and they do not drag the player.

The demo platform is `assets/platform.obj`, tagged `solid`, named Platform. Its move is about `(1.6, 0, 0)` over 4 seconds, from a rest position near the middle of the room.

A Use action is a one-shot tween. The object that receives F stores `act "Name" x y z`, and `rx ry rz` when the rotation is not zero. Those last three numbers are degrees. An older line with only the move still loads. The first F pushes one tween, up to `max_tweens` at once (default 8). It captures the target's position and, for a mesh, its Euler rotation, then slides and turns toward the offset over `action_duration` ticks (default 0.4 s → 12 ticks). `Tween::time` counts those ticks. It then leaves the list and the target stays. A second F on that same Use does not play it again. Restart restores the snapshot, so the target returns and the action can play again. The demo Switch targets `Door` and lifts it by `(0, 1.6, 0)`. A Use tween target is kinematic in Jolt while the tween runs, so a player standing on it is carried by ground velocity. The rotation is not.

The player sphere still moves by the play step, and the chase camera follows. That part is gameplay, not this ping-pong.

## Not built

- Skeletal clips, blending, and a state machine.
- Root motion and inverse kinematics.
- Vertex animation and shape keys.
- Material animation besides UV scroll. A material's `uvScrollU` and `uvScrollV` move the albedo and normal samples by that many UV units per second. The editor clock advances while the window is open, and a non-zero scroll redraws every frame.
- A timeline for one-shot sequences.
