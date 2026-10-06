# Input

Play input is read in `editor/src/EditorPlay.cpp` with `GetAsyncKeyState`. Those calls use the virtual key of the current layout, so the letters below match the key caps on an AZERTY keyboard.

## Actions while playing

`readPlayInput` fills a `PlayInput`:

| Key | Field |
| --- | --- |
| Z | `moveZ` +1, forward relative to the chase camera |
| S | `moveZ` -1 |
| Q | `moveX` -1, strafe left |
| D | `moveX` +1 |
| Space or E | `jump` |
| F | `use` |

Those are the defaults. Settings lists Forward, Back, Left, Right, Jump, Jump 2, and Use. Pressing a row, then a letter or Space, writes `key_forward`, `key_back`, `key_left`, `key_right`, `key_jump`, `key_jump2`, and `key_use` into `raytracer-settings.txt`. The next launch reads them. C, V, and Escape stay fixed. Forward and strafe are normalized together so a diagonal is not faster. `PlayInput` is the only action struct. Nothing reads a gamepad.

Keys are sampled once per `1/60` step, and only when the window is in the foreground and ImGui does not want text. Pause does not sample them. Space is not used as an editor shortcut, because a focused ImGui button treats Space as activate.

Escape toggles pause while the round is open. It does not unpause a win or a loss. It is an ImGui key, not `GetAsyncKeyState`. Delete, orbit, pan, and zoom are disabled while playing. F is sampled on a paused Step as well, so a held Use still counts for that tick.

Ctrl+Shift+C, while editing and while ImGui does not want text, adds a named camera from the current view. The same command is Add, then Camera. C toggles between the chase camera and first person during play. It is a press, not a hold, and it is ignored when paused, when the round is over, when ImGui wants text input, when the window is not focused, or while a named camera is showing. V steps to the next named camera, then back to chase. Ctrl, while editing, snaps a gizmo move or a position slider to the Snap spacing.

## Mouse

While editing, the mouse orbits, pans, zooms, and picks. While playing, mouse delta yaws and pitches the chase camera. During play the cursor is hidden and clipped to the window, except while paused, while the round is over, or while the keep-or-discard prompt is open. Stop and those pauses show it again and release the clip. See `docs/cameras.md`.

## Not built

- Rebinding C, V, or Escape.
- Gamepad.
- More than one local player.
- More actions than move, jump, and use.
