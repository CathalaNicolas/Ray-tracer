# Game GUI

The HUD and the pause window are ImGui, drawn over the ray-traced image. They are not ray traced. A future game menu can replace them without removing ImGui from the editor.

## HUD

`drawPlayHud` runs only while playing. It sits 16 pixels from the top-left of the main viewport, with a translucent background, and it ignores the mouse.

- No player sphere: the text `No player`.
- Otherwise: `Score: N    Health: N`. Health starts at 3. A pickup adds the line `Picked up` under the score.
- Any other message is a banner at the top center. Inside the goal or the switch the stored message is `Press F`. The banner shows `Press ` plus the Use key, which is F until it is rebound. Using the switch shows the switch name. Winning shows `You win`. Losing shows `You lose`.
- When the play ray hits a `use` or a `goal` and the round is still open, `drawWorldPrompt` draws that same Use key on the hit point. The label is white text on a dark rounded rect, centered in the view image. A hit behind the camera is skipped. Nameplates are not drawn.

## Pause

Escape toggles pause while the round is open. The window is centered.

- Resume clears `paused`. The simulation continues. The scene is not restored.
- Step runs one `1/60` s tick and stays paused. Held move keys and F apply.
- Speed scales simulation time from `0.25×` to `2×`. The editor also shows that slider while play is running.
- A win or a loss replaces Resume and Step with the line `You win` or `You lose`, and Restart. Restart copies the snapshot from Play, clears the score and the result, and stays in play. Escape does not resume that window.
- Quit in the editor leaves play, which restores the snapshot.
- Quit in `--game` posts `WM_QUIT` and closes the window.

`--game` starts on a centered title: Play, a Volume slider, and Quit. Play starts the round. Quit and Escape leave the process. The Volume slider writes `raytracer-settings.txt`. The editor does not show this window. The round-over window is the game-over screen. There is no settings page inside the game window. Key changes live in the editor Settings menu.

## Not built

- A font path that is not ImGui.
- Widgets with gamepad or keyboard focus for a real menu.
- A crosshair or subtitles. Health is the integer next to the score. The Use key on a looked-at `use` or `goal` is the world prompt above.
- Drag and drop and inventory grids.
- Nameplates and any world-space widget besides the Use key.
- French and English string tables. The few strings above are English literals.
- Cursor modes. The pointer stays visible and free.
