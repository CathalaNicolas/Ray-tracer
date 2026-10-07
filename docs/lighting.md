# Lighting

Lights are `PointLight` values on the scene (`engine/include/Light.hpp`). The type name stays even when a light is directional. The scene also has an ambient color, used as the ambient term in the shader.

## A light

- `position` and `color`.
- `intensity` and `falloff`. Falloff is a distance term on point lights.
- `radius`. Above zero, shadow rays are spread across that radius so the shadow softens. Zero is a hard point.
- `directional`. When set, the light is a direction rather than a point at `position`.
- `spotDirection`, `spotOuter`, and `spotInner`. These are a cone on the same light. `spotOuter` and `spotInner` are half-angles in degrees, measured from the cone axis. `spotOuter` of 0 means the light is not a spot, so an old point or directional light is unchanged. Inside `spotInner` the cone is full strength. Between the inner and outer half-angles it falls off with the square of a smooth step, and it is dark outside the outer half-angle. The axis is `spotDirection`, normalized in the shader. It points from the light into the scene. A zero-length axis adds no light while the outer angle is above 0. If the inner angle is larger than the outer angle, it is clamped to the outer angle.

The demo uses a key light with radius `0.45` and a dimmer fill aimed back toward the room as a mild spot (outer half-angle 38 degrees, inner 18). Both are saved with the scene. The file writes `radius` and `directional` only when they are set. A spot adds `spot x y z outer inner` and omits that tail when the outer angle is 0. The scene file stays version 1. `spot` is a light-line keyword. An object line that contains it is still an unknown option.

## Diligent clustered lights

The Diligent forward path does **not** rank lights down to eight. `gfxBuildLightClusters` (`GfxCluster.cpp`) assigns every scene light into a 16×9×24 cluster grid; `OpaquePS.hlsl` walks the per-tile index list (with a small fallback when a tile is empty). `rankGpuLights` / the 8-light upload remain only on the legacy GL still shader.

Quality settings bind through `GfxQuality`: view distance, shadow map resolution, cascade count, bloom, and atmospheric sky.

## Legacy GL still path (≤8 lights)

The GL still path keeps at most 8 lights. Past that, `gpuSceneLimits` warns and the extra lights are skipped. A point light, including the Lamp, writes six 90° faces into its 1024 shadow layer, three across and two up. A mesh on any side of the light stays on one face, so a door that straddles the old seam no longer smears across the floor. The pass draws back faces, and the shader averages 4 taps inside that face. The compare bias is `max(0.004, 0.012*(1-facing))`. A directional light still uses one orthographic map.

## Emission

`Material::emission` brightens that surface and shows up in reflections. It also becomes a point light at the object center while fewer than 8 lights are already in use. The color is the albedo, the intensity is the emission, and the falloff is 0.2. The surface does not light itself. Sphere blockers are tested, skipping the emitter. Mesh shadow maps block these lights the same way they block a real point light. A glowing mesh is omitted from its own shadow map so the light can leave the surface. The lamp sphere in the demo is this light plus the real key light.

Settings → Mirror bounces is on by default. Off writes `mirrors 0` and the next frame uploads `uMirrorCount` 0, so no sphere bounce runs. On restores the count. A settings file with no `mirrors` line leaves the bounce on.

A sphere with reflectivity above 0.35 and transmission 0 bounces each light once, on the camera's first hit. The GPU stores up to 8 of those spheres. Eight iterations move the point on the sphere until its normal bisects the light and the surface, and the bounce is kept only when the reflected ray matches that surface direction (alignment at least 0.995). Strength is the light color times its intensity, faded by `1 + falloff * distance²` from that point to the surface, times how face-on the point is to the light, times the sphere albedo and its reflectivity, times the receiver albedo, diffuse, and facing. Anything between that point and the surface leaves a shadow in the bounced light, and that shadow moves with the sphere. Spheres, planes, and meshes can block that path, and the light's shadow test blocks the path from the sphere to the light. The sphere itself is not a blocker of its own bounce, and neither is the emissive object that produced the light. A plane or a mesh does not bounce light. Reflection and glass rays do not bounce again. A second mirror can send that light on. The last bounce is the one of the first four mirror spheres that the surface faces. Each of the others in that four can feed it. The solver uses four iterations and at most two passes, and it stops once both reflections match. Both alignments must be at least 0.995. Strength multiplies both albedos and both reflectivities, and the fade is from the second point to the surface. An object between the surface and the second point, or between the two points, leaves a shadow. The demo mirror sphere does. The floor at 0.14 does not. The gold sphere at 0.42 does, more dimly, and it can be the second bounce.

## Not built

- A third mirror in one bounce path, or a second bounce that uses mirror spheres past the first four (GL still path).
- Light cookies and colored shadows.
- Baked lightmaps.
- Full GPU cluster build (current Diligent clusters are CPU-side).
- Day/night clock driven continuously in the editor (DiligentFX sky wrapper is quality-gated).
