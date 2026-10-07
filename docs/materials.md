# Materials and shading

One `Material` value sits on each sphere, plane, and mesh. The struct is in `engine/include/Material.hpp`.

The **Diligent opaque PSO** (`OpaquePS.hlsl`) shades with albedo × optional `AlbedoTex`, ambient, and clustered lights. Full Blinn-Phong / maps / glass remain on the **GL still** path in `engine/src/GpuShaders.cpp` / `GpuShaderTrace.cpp`.

## Fields

| Field | Meaning |
| --- | --- |
| `albedo` | RGB base color |
| `ambient`, `diffuse`, `specular` | Blinn-Phong weights |
| `shininess` | Specular exponent, used as stored when roughness is below 0 |
| `reflectivity` | Mirror weight. Mesh reflections trace only above 0.35 |
| `transmission`, `ior` | Glass. `makeGlass` sets transmission to 1 and ior to 1.5 |
| `uvScale` | Texture repeat |
| `uvScrollU`, `uvScrollV` | UV units per second added to the texture coordinate. Both 0 leave the sample unchanged. The scene file writes `scroll u v` only when one of them is not 0 |
| `roughness` | Below 0 means "use shininess". Otherwise it broadens the highlight |
| `emission` | Added on the surface, and also sent as a point light onto other surfaces. See `docs/lighting.md` |
| `albedoMap`, `normalMap` | Image paths. Empty means none |

`makeDiffuse`, `makeMetal`, and `makeGlass` fill a sensible set. The scene file writes the numeric fields in the order in `docs/world-and-scenes.md`, and writes `rough`, `emit`, `map`, `normal`, and `uv` only when they differ from the defaults.

## How a pixel is shaded

Opaque surfaces use Blinn-Phong plus the albedo image, the normal map, emission, and a reflection ray when reflectivity is above zero. Shadow rays sample the light radius for soft shadows. The display path tone-maps. The linear path used by tests does not. See `docs/rendering.md` for which surfaces can see a mesh in a reflection or through glass.

Planes may also wear a checker: a second albedo and a world scale, stored on the plane rather than on `Material`.

## Not built

- Material instances that share a shader and override a few values. Each object owns a full copy.
- Masks, detail maps, and emissive maps.
- A material editor beyond the inspector sliders.
- A second shading model. Toon or unlit would be a variant of the same shader, not a second program, if they are added later.
