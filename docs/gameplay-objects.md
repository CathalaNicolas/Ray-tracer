# Gameplay objects

An object is an EnTT entity reached through a non-owning `Object` handle. It has a name, a tag, and usually one shape component. The tag builds a `Role` (`engine/include/Role.hpp`). Game rules that exist today are the role plus `PlayState`. The scene file still stores the tag string.

## Tags the demo uses

| Tag | Demo object | Play behavior |
| --- | --- | --- |
| `player` | Blue sphere, radius 0.35, at `(0, 0.5, 2.2)` | The first sphere with this tag is controlled |
| `solid` | Ground, tree trunk, tree crown | Blocks the player. Planes block even without the tag. Meshes block when the tag is `solid` or empty |
| `pickup` | Three yellow spheres, radius 0.22 | Overlap removes it, adds 1 to the score, and adds 1 health up to 3. No key |
| `trigger` | None in the demo | Overlap sets the banner to `Reached ` plus the name, or `Triggered`. No key |
| `goal` | Green sphere named `Goal`, radius 0.45 | Overlap, or a play ray that hits it within 3 units, shows `Press F`. The ray hit also draws the Use key on the surface. F wins when `room` is 0 or 2. When `room` is 1, F loads the east room and the round stays open |
| `use` | Purple sphere named `Switch` | Overlap, or a play ray that hits it within `play_ray` (default 3), shows `Press F`. The ray hit also draws the Use key on the surface. F sets the banner to the object name and does not end the round. If Use target names an object and Use move or Use rotate is not zero, F also slides and turns that object over `action_duration` (default 0.4 s → 12 ticks at 30 Hz), once, and leaves it there |
| `hazard` | Dark red sphere named `Hazard` | Overlap loses the round when no `spawn` point exists. With a spawn point it costs 1 health and respawns. At 0 health it loses anyway. No key |
| `spawn` | Small cyan sphere named `Spawn` | Not solid. The first one is the respawn point. A hazard or a fall below `y = -3` moves the player there, clears vertical speed, shows `Respawned`, and leaves the round open. Score and removed pickups stay |
| `spawner` | Orange sphere named `Spawner` | Not solid. Every `spawnEvery` seconds converted to ticks (2.5 s → 75 ticks in the demo, 3 s → 90 ticks when the value is 0), if its last pickup is gone, it adds a yellow sphere named `Spawned`, radius 0.22, at its position plus `(0, 0.22, 0)` |

The mirror, gold, lamp, glass, red spheres, and the grey `Step` pebble are untagged or `solid`. Untagged spheres block the player. `goal`, `use`, `hazard`, `spawn`, and `spawner` do not. The platform mesh is tagged `platform` and moves. See `docs/animation.md`. The door is tagged `solid` and its layer is 1, so the player hits it and the chase camera passes through it.

If the scene has no `spawn` tag, a hazard or a fall still ends the round. Tests of that loss depend on the tag being absent.

## Play state

`PlayState` in `engine/include/Play.hpp` is not saved with the scene:

- `paused` (unused by the integrator; win/lose leave it false. The snapshot HUD sets `paused` when `result` is non-empty. Editor pause is `ViewState::paused`)
- `score`, starting at 0 each time Play is pressed
- `health`, starting at 3. A pickup adds 1, and health stays at 3 when it is already 3. A hazard subtracts 1 when a `spawn` point exists. At 0 the round is lost even if that point exists. With no `spawn` point, a hazard still loses immediately and health becomes 0. A fall does not spend health. Respawn, the spawn point, and the goal do not add health
- `message`, shown on the HUD. Pickup and hazard juice are `SimEvent`s the view turns into sounds and particles; the string is not compared for those
- `playerId`, or invalid `EntityId` 0
- `verticalVelocity`
- `result`, empty while the round is open, `won` or `lost` when it ends
- `room`, `0` unless the editor started play. `0` is not the campaign. `1` is the demo. `2` is the east room
- `motionTime` (tick count at 1/30 s) and `rests`, the clock and captured poses for transform animation
- `simSeed` (default 1) and `simRng` (`std::mt19937`). Cleared with `PlayState`. Play does not draw from it yet

Using the goal wins when `room` is 0 or 2, and the message is `You win`. When `room` is 1, that Use does not end the round: the scene becomes the east room, `room` becomes 2, and the message is `Next room`. A loss sets the message to `You lose`, and only when no `spawn` point exists. With a spawn point, a hazard or a fall teleports the player and the round continues. The demo ground is an infinite plane, so the fall rule is for a gap. Restart clones the play snapshot, clears `PlayState`, and `SimSession::begin` finds the player again. Stop still leaves play entirely.

Stop throws this away with the snapshot. See `docs/application-loop.md`.

The place to add another rule is `stepPlay`, not a new language. See `docs/scripting.md`.

## Not built

- Invisible entities.
- Components for render, collider, script, and audio.
- Prefab variants. A spawner exists, and its pickups do not survive Stop.
- Inventory, or any game state besides score, health, the HUD message, and win or loss.
- A timer rule besides motion, spawn, and Use tween tick clocks. Use and overlap are the rules that exist.
