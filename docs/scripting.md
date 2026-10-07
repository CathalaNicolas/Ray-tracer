# Scripting and game rules

Gameplay code is C++. The current rules are `stepPlay` in `engine/src/Play.cpp`: advance moving objects and spawners, move the player, carry the player on a moving solid, step onto a low curb, resolve solids, collect pickups, require F for `use` and `goal`, play a Use action, spend health on a hazard, respawn at a `spawn` point, and otherwise end the round on the goal, a hazard, a fall, or zero health. A goal Use wins when `room` is 0 or 2. When `room` is 1, that Use keeps the round open and replaces the scene with the east room. The editor calls it once per `1/60` step and does not know those rules itself. Restart and the win window live in the editor. Step, while paused, clears pause for that one call and sets it again if the round is still open. Restart sets health back to 3.

A new rule belongs next to that function, or in a function it calls, and should be covered by `runPlaySelfTests` when the result is numeric or a scene change. Tags are the data. See `docs/gameplay-objects.md`.

There is no script file, no hot reload, and no graph.

## Not built

- A data language for triggers, timers, and keys.
- Hot reload of gameplay code.
- A visual script graph.
