# Build, platform, and content

## Diligent path (primary interactive view)

CMake is the maintained build for the Diligent clustered-forward path. From the repo root:

```bash
cmake -S . -B build-diligent -G Ninja -DRAYTRACER_DILIGENT=ON -DRAYTRACER_BUILD_EDITOR=ON
cmake --build build-diligent --target diligent_smoke raytracer
```

- **DiligentCore / DiligentTools / DiligentFX** come in via CMake FetchContent at tag `v2.5.6`. Tools/FX headers expect sibling folders named `DiligentCore` and `DiligentTools`; the root `CMakeLists.txt` creates those symlinks under the build `_deps` tree before adding the subdirectories.
- **Backend:** Direct3D 12 on Windows (`RAYTRACER_DILIGENT_D3D12`), Vulkan elsewhere (`RAYTRACER_DILIGENT_VULKAN`). OpenGL is not used for the main window.
- **SDL3** (static, FetchContent) owns the window and native handle (`HWND` / X11). No `SDL_WINDOW_OPENGL`.
- **Targets:** `raytracer_core` (no GPU), `raytracer_rhi` (Diligent RHI + FX), `diligent_smoke` (clear + opaque draw), `raytracer` (SDL + Diligent editor). On Windows, `raytracer_gl` still links for `--self-test` / still frames only.
- Run smoke and the editor from the repo root so `assets/` resolves. Linux CI can use lavapipe (`VK_ICD_FILENAMES=.../lvp_icd.json`).

Shaders for the Diligent path live in `engine/shaders/` (HLSL). GLSL Whitted shaders stay in `GpuShaders.cpp` / `GpuShaderTrace.cpp` for the still path.

## Legacy Makefile (Windows OpenGL still / old editor)

The Windows Makefile build still uses MSVC from Visual Studio Build Tools 2022 and the Autodesk FBX SDK 2020.3.11. The Makefile calls `vcvars64.bat`, then `cl` and `link`. Flags are `/std:c++17 /EHsc /O2 /MD /bigobj`. Engine and editor translation units use `/W4`. `third_party` and the stb image wrappers stay at `/W0`.

Object files go in `dist/`. `raytracer.exe` stays in the project root so it still finds `assets/`. `build.bat` in the project root runs that build.

Runtime code lives in `engine/include` and `engine/src`. The legacy Win32+GL editor lives in `editor/src/editor.cpp` and related files; the Diligent interactive shell is `editor/src/EditorDiligent.cpp`. `editor/src/main.cpp` is the process entry and prefers Diligent when `RAYTRACER_DILIGENT` is defined.

The legacy link line includes `user32`, `gdi32`, `opengl32`, and the FBX SDK static libraries. OpenGL is 3.3 for stills / self-test only once Diligent is enabled. Mesh OBJ load works without FBX (`RAYTRACER_HAS_FBX` gates the SDK).

Assets the demo needs sit in `assets/`: `tree-trunk.obj`, `tree-crown.obj`, `door.obj`, `platform.obj`.

## Not built

- A folder a friend can run without this repo.
- Asset paths rewritten for that folder.
- An installer or a zip of the exe, assets, and runtime libraries.
- A content-license note for third-party meshes and the FBX SDK.
