# Rendering

## Diligent primary view (clustered forward)

Interactive play and the editor orbit view draw through Diligent (`GfxDevice`, `GfxView`, `GfxFx`):

- Window and swapchain: SDL native handle → D3D12 (Windows) or Vulkan (Linux bring-up). No OpenGL context on the main window.
- Opaque pass: HLSL `engine/shaders/OpaqueVS.hlsl` / `OpaquePS.hlsl`, instanced indexed meshes, camera-relative float positions, runtime-sized vertex/instance buffers (GL `GpuLimits` mesh caps are not raised).
- Materials: vertex color × optional albedo texture (1×1 white default; BC/DDS upload helper in `GfxTextures.cpp`).
- Presentation: `GfxFx` wraps DiligentFX cascaded shadow maps, bloom, and epipolar sky sized from `GfxQuality` / `EngineSettings` (`view_distance`, `shadow_map`, `particle_density`).
- ImGui: DiligentTools `ImGuiImplDiligent` into the swapchain; SDL feeds input in `EditorDiligent.cpp`.

`diligent_smoke` clears, uploads the demo meshes, and presents a few frames. The editor entry is `runEditorDiligent`.

## Legacy OpenGL still / self-test

Shading for stills is one GLSL 330 fragment shader, `kFragmentShader` in `engine/src/GpuShaderTrace.cpp`. The smaller shaders stay in `engine/src/GpuShaders.cpp`. The string is split into adjacent raw literals because MSVC rejects a literal past about 16380 characters. Do not merge it back into one literal. That path is not the primary editor view once Diligent is enabled.

`engine/src/GpuRender.cpp` uploads the scene and draws. `engine/src/GpuRayTracer.cpp` creates the context, folds samples, and runs bloom. `engine/src/GpuGl.hpp` supplies the GL entry points used on Windows. The repo does not include `GL/glext.h`.

Distance fog mixes the shaded hit toward `uFogColor` by `1 - exp(-uFogDensity * hit.t)`. Density 0 skips that mix, so the linear test path is unchanged. The scene file writes `fog r g b density` only when the density is above 0.

Bloom runs after that image is tone-mapped, and only when linear output is off. It copies the display, blurs pixels brighter than luma `0.88`, and adds that glow back. Linear reads used by tests skip this pass.

## What draws what

- Spheres and planes are ray traced in the shader.
- Mesh primary visibility is a raster mesh. The vertex buffer is world-space, with the instance index in an extra channel.
- Mesh shadows come from shadow maps. A point light, including an emissive Lamp, fills its 1024 layer with six 90° faces in a 3 by 2 grid. Each face is clipped, so a triangle that crosses a face edge does not stretch across the map. The pass draws back faces only, then the shader averages 4 taps inside the face. A directional light uses one orthographic map of the full layer. A render whose lights, emissive objects, mesh transforms, and materials match the previous one keeps those maps and the material rows. The camera uniforms are uploaded every frame.
- A mesh reflection is traced when reflectivity is above `0.35`. `traceMesh` walks that mesh's BVH in local space.
- A sphere with reflectivity above `0.35` and no transmission bounces each light once, and only on the camera's first hit. The shader walks up to 8 such spheres (`uMirrorIndex`). Eight iterations place the point whose normal bisects the light and the surface. The bounce is kept only when `reflect` of the light direction matches the direction to the surface, with a dot of at least `0.995`. Strength is the light color times intensity, divided by `1 + falloff * distance²` from that point to the surface, times the facing term, the sphere albedo, and its reflectivity. An object between that point and the surface leaves a shadow in the bounced light. Spheres, planes, and meshes block the way to that point. The light's shadow test blocks the way from the sphere to the light. The mirror sphere and the emissive object that made the light are skipped. Reflection and glass rays do not bounce again. Among the first four mirror spheres, the one the surface faces can send a bounce on from each of the others. The solver stops once both reflection points match with a dot of at least `0.995`. An object between those points blocks it. Planes and meshes do not bounce light. The floor at `0.14` is not a bounce source. Settings → Mirror bounces off uploads `uMirrorCount` 0 for that frame.
- Glass transmission traces meshes through `traceMesh`. A floor reflectivity of `0.14` does not trace meshes.
- The background is a horizon-to-zenith gradient, plus the environment map when one is set. It is not a sky mesh.

The glass job stack, trace limit, and mesh BVH stack are `EngineSettings` keys (`job_stack`, `trace_limit`, `mesh_stack`), uploaded as uniforms and clamped to the fixed GLSL array sizes 8 / 24 / 24. Defaults match those sizes. The editor status line warns when the mesh stack is below BVH depth, or when glass needs more job stack or trace steps than the settings allow. The fat path must not use a literal constant loop bound.

## Samples, tone, and depth of field

The Samples slider is `N` for an `N×N` grid, from 1 to 4. While the editor is idle the GPU adds one sample at a time until that grid is full. Play and any camera or scene edit drop back to one sample and restart the count.

`trace()` uploads `uLinear = 1` and must not clamp color to 1. `max(color, 0)` is fine. Reinhard `color / (1 + color)`, then gamma 2.2, runs only when `uLinear == 0`, which is the display path. Tests that compare the opaque non-glass image stay numerically identical when aperture is 0 and linear output is on.

Depth of field uses a Vogel disk when aperture is above 0 and the sample count is above 1. Aperture 0, or a single sample, stays a pinhole. `cameraClipMatrix` stays as written.

## GPU data

Texture units:

| Unit | Name |
| --- | --- |
| 0 | Albedo |
| 1 | Environment |
| 2 | Triangles |
| 3 | BVH |
| 4 | Positions |
| 5 | Normals |
| 6 | UVs |
| 7 | Shadow map |
| 8 | Normal map |
| 9 | Material extras, RGBA32F |
| 10 | Mesh instances, RGBA32F |

Unique mesh geometry is uploaded once into the triangle and BVH textures. Each placement is a column of the instance texture, 256 wide and 9 rows:

- Row 0: albedo rgb, ambient
- Row 1: diffuse, specular, shininess, reflectivity
- Row 2: transmission, ior, texture layer, uv scale
- Row 3: world min xyz, BVH root
- Row 4: world max xyz, object id
- Row 5: position xyz, uniform scale
- Rows 6–8: rotation matrix columns

The material texture is the same width, 3 rows: spheres, planes, then mesh instances. A texel is emission, normal-map layer, roughness, and 0. Roughness below 0 means the stored shininess is used as-is.

Old per-mesh uniform arrays (`uMeshLeft`, `uMeshRight`, and the rest) are gone. They were the 48-slot constant-register limit. Do not put them back.

## Not built

- A skybox object.
- Volumetric light. Distance fog is the color and density above.
- Glass that can see meshes.
- Decals, particles, skeletal meshes, and morph targets.
- GPU instanced drawing. Sharing is in the triangle storage, while the raster mesh is still expanded per placement.
- Frustum and occlusion culling.
- A full-scene forward or deferred raster.
- Bloom, grading, vignette, motion blur, and anti-aliasing beyond the sample grid.
- Separate render layers for world, effects, and UI. The editor and HUD are ImGui on top of the image.
- Video capture. Save PNG writes the current image.
