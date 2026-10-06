# Review: blunt notes

Direct feedback on the code as it stands. No praise, no hedging. Each item says where it is and what is wrong with it. Severity is about what a player or a developer actually hits, not about how ugly the line looks.

Two structural pages sit next to this one: [objects-today.md](objects-today.md) for how the types are used, [objects-as-types.md](objects-as-types.md) for which of them deserve to be types. This page is the defect list.

## Fixed

These were broken; the code now matches the intended behavior. Left here so the history is not lost.

- **Camera frustum overlay.** Colliders draws each named `SceneCamera` in `scene.shots()`, and skips the shot that matches the view. Drawing the view camera itself collapsed to the image border. Near/far come from `EngineSettings`.
- **Parented sphere and plane collision.** `contact` and `overlapsShape` use `worldRadius()` and `worldNormal()`. Play tests cover a scaled parent sphere and a rotated parent plane.
- **Look ray blocked by pass-through tags.** `castPlayRay` skips `pickup`, `trigger`, `hazard`, `spawn`, `spawner`, and `player`. Solids still block. A play test aims through a spawn at a use.
- **Non-Windows `GpuRayTracer::render` stub.** The stub takes the `mirrorBounces` argument. The Makefile non-Windows `OBJS` list is still stale; see Fragile / build notes.
- **Sound buffer freed while playing.** `playGameSound` stops the previous clip before reallocating `gPlayed`.
- **Duplicate `isSolid` in the collider overlay.** `DebugDraw.cpp` calls `play_detail::isSolid`. Platform meshes draw again.
- **`triBlocks` / `hitStoredTri` duplication.** Both call shared `hitTriCore` in `GpuShaderTrace.cpp`.
- **`state.rests` pointer into a growing vector.** Motions keep a rest index, not a pointer into `rests`.
- **Platform carry used local position.** Carry uses `worldPosition()`.
- **Renderer fingerprint statics.** Fingerprint and upload caches live on `GpuRayTracer` and clear in `shutdown()`.
- **`SceneLoad` number-or-keyword probe.** Optional `rx ry rz` uses `looksLikeNumber` before consuming tokens.
- **`PlayState.eventAt` drifted from the message.** `messageAt` updates only when the message, prompt, win, or advance actually changes.
- **`addParticle` silent drop.** Cap is `max_particles` from settings; the first overflow prints once to stderr.
- **`gMirrorBounces` / `gSnapStep` globals.** Both live on `engineSettings()` and persist with the other tunables.
- **Shape switches in play / save / GPU / overlay.** Shared ops are virtual on `Hittable`. Inspector shape-only fields still cast.
- **`Scene::find` linear scan.** `find` is an `id` → pointer map updated on add/remove/clone/move.
- **`parentFrame()` rebuilt every call.** Cached per object until `Scene::bumpParentFrames()`.
- **Role logic as tag strings in `collectPlayEvents`.** A `Role` beside the shape; play switches on `RoleKind`. The file still writes the tag word.
- **GLSL stack depths as literals.** `job_stack`, `trace_limit`, and `mesh_stack` are settings and uniforms (array maxes 8 / 24 / 24). The status line warns when BVH depth or glass needs more than the settings allow.
- **`/W3` on engine and editor.** Those objects build with `/W4`. `third_party` and stb stay `/W0`.

## Fragile

- **Non-Windows Makefile `OBJS`.** The g++ branch does not list the current sources and does not build `Mesh.cpp` (FBX). The stub matches the header again; a full non-Windows build is still not maintained.
- **GLSL stack warnings are predictive.** There is no GPU readback when a job or mesh stack push is dropped mid-shade. The editor warns from BVH depth and glass heuristics, not from a live overflow flag.

## Performance, honestly

The frame time is the fragment shader. Nothing in the C++ object model is on that path, and no amount of removing `if`s from `Play.cpp` will show up in the millisecond counter. Anyone reading `objects-as-types.md` as a performance plan is reading it wrong.

The real CPU-side waste, in order:

1. **The mirror bounce still walks the mesh BVH** on a kept path, twice for the two-hop case. It is now bounded by segment length and returns on first hit, which is what brought 100 ms down, but it is the remaining lever if a mirror-heavy scene gets slow again. Mirror bounces off is the escape hatch.
2. **`materialScrolling` and the fingerprint walk the whole scene every frame** to decide whether to re-upload. Cheap now, linear in objects forever.

## Missing tests

`self-test: ok` covers scene IO, the tail parse, mesh sharing, tone mapping, normal maps, and the play step. It does not cover:

- Any GPU pixel. Deliberate, and the reason every shader regression this project has had was found by eye.
- The mirror bounce, in either hop. The debug dump is the only check, and it simulates the first hop only with its own copy of the energy formula — a second place for that math to drift.
- The collider overlay and the world prompt. The frustum is no longer degenerate for named shots; there is still no automated check of the overlay pixels.

Parented colliders and a look ray through a spawn are covered by play tests now.

## Enhancements worth the effort

Ordered by payoff per hour, not by appeal.

1. Live GPU feedback when a GLSL stack push is dropped (not only the CPU heuristic).
2. The mirror bounce BVH walk on the kept path, if a mirror-heavy scene gets slow again.
3. A shared transform type for lights and cameras.

## Deliberate, do not "fix"

Flagged because they look like bugs in review and are not:

- **Prefab sync overwrites instance fields.** Material, tag, layer, motion, scale, and rotation are copied from the source onto every instance each frame. The instance root position is left alone. Editing the source updating every placement *is* the rule. See `docs/code-smell.md`.
- **The opaque non-glass path is not clamped.** Aperture 0 with linear output must stay numerically identical for the self-test. Reinhard only runs when `uLinear == 0`.
- **Bloom threshold 0.88 and strength 0.85.** Lowering the threshold below about 0.86 makes the emission-2 sphere test fail.
- **The first bounce hop keeps its own 8-iteration solver** instead of calling `bouncePoint`. Switching it would change convergence on the path that is already accepted and tuned.
- **Health caps at 3 and a pickup adds 1.** Game rule, not an engine gap. No regen, no damage types, no bars.
