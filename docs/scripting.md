# Scripting and game rules

Gameplay code is C++. The current rules are `stepPlay` in `engine/src/Play.cpp`: advance moving objects and spawners, move the player capsule on Jolt `CharacterVirtual`, collect pickups, require F for `use` and `goal`, play a Use action, spend health on a hazard, respawn at a `spawn` point, and otherwise end the round on the goal, a hazard, a fall, or zero health. A goal Use wins when `room` is 0 or 2. When `room` is 1, that Use keeps the round open and replaces the scene with the east room. Moving solids and Use tweens are kinematic in Jolt; ground velocity carries a standing player. Walk-stairs is 0.35.

The editor and `--game` do not call `stepPlay` themselves. They enqueue `Command`s and `SimSession::take` those ticks. Headless tests use `SimSession::step`. Restart and the win window live in the editor. Step, while paused, clears pause for that one call and sets it again if the round is still open. Restart sets health back to 3.

Each tick can emit `SimEvent`s: Pickup (score / gold burst), Win (`You win`), Sound (beep: Respawned, Press F, Next room, You lose, trigger/use text), Effect (red hazard burst). `PlayState::message` still feeds the HUD. `playSimEvents` in the editor maps kinds to `GameSound` (Win over Pickup over Beep, one clip per tick) and spawns the gold/red discs. `stepPlay` does not add particles.

A new rule belongs next to `stepPlay`, or in a function it calls, and should be covered by `runPlaySelfTests` when the result is numeric or a scene change. Tags are the data. See `docs/gameplay-objects.md`.

There is no script file, no hot reload, and no graph.

## Not built

- A data language for triggers, timers, and keys.
- Hot reload of gameplay code.
- A visual script graph.
