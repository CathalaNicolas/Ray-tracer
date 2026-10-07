# Build, platform, and content

The supported Windows build is CMake with presets, MSVC 2022, and C++20. vcpkg runs in manifest mode from the root `vcpkg.json` (`builtin-baseline` pinned to `434307da09bc05b2c86996dccc8b2351fc0d5d37`). `vcpkg-configuration.json` sets the same git default-registry baseline and adds `overlay-ports/` so Jolt is built from the overlay, not the stock port. `build.bat` in the project root calls `vcvars64.bat`, then `cmake --preset windows-msvc` and `cmake --build --preset windows-msvc`. Double-click it, or run it from a terminal. It pauses at the end so the result stays on screen. Debug is `cmake --preset windows-msvc-debug` into `build-debug/`. Diligent is `cmake --preset diligent` into `build-diligent/` (`RAYTRACER_DILIGENT=ON`).

Repo-local `vcpkg/` is a working clone only (gitignored). Point `VCPKG_ROOT` at it or at another clone; do not commit it as a git submodule.

## Requirements

- Visual Studio 2022 Build Tools (C++ workload), with the CMake and Ninja components that ship with VS.
- Autodesk FBX SDK 2020.3.11 (VS2022 install) under `C:/Program Files/Autodesk/FBX/FBX SDK/2020.3.11`, the same external path the old Makefile used. Override with `-DFBX_SDK_ROOT=...` if needed.
- A vcpkg clone with `VCPKG_ROOT` set, or a repo-local `vcpkg/` directory that `build.bat` can pick up. First configure installs the ports in `vcpkg.json`: Phase 0 (entt, glm, enkits, spdlog, doctest, tracy, sdl3), Phase 1 (bitsery, sqlite3, joltphysics with `double-precision`, miniaudio), and Phase 2 assets (assimp, meshoptimizer, zstd, physfs, directxtex, flatbuffers).

## Layout

`CMakeLists.txt` defines static libraries and executables. `raytracer_core` owns EnTT/GLM scene and play code plus bitsery, SQLite, Jolt, OBJ mesh loading (`MeshObj.cpp`, `MeshLoad.cpp`), cooked `.rtm` (`MeshCooked.cpp`, zstd), PhysFS (`Content.cpp`), portable SHA-256 (`Hash.cpp`), DDS parse (`TextureCooked.cpp`, no DirectXTex), FlatBuffers catalog (`Catalog.cpp`), heightmap tiles (`Terrain.cpp`), and tile streaming (`Stream.cpp`). `flatc` writes `content_generated.h` into the CMake build `generated/` folder. `raytracer_cook` owns Assimp import, meshoptimizer, DirectXTex image cook, and `flatc -b` table cook (`cooker/src/`). `raytracer_import` owns Autodesk FBX loading. `raytracer_render` owns OpenGL/render/self-test code. `raytracer_audio` owns playback in `Sound.cpp` through miniaudio (`MINIAUDIO_IMPLEMENTATION` in `engine/src/Miniaudio.cpp`). With `RAYTRACER_DILIGENT=ON`, `raytracer_rhi` owns Diligent device/view/cluster/FX code and `diligent_smoke` is built.

| Target | Output | What it links |
| --- | --- | --- |
| `raytracer` | `build/bin/raytracer.exe` (Release) or `build-debug/bin/raytracer.exe` (Debug); Diligent preset uses `build-diligent/bin/` | core + import + render + audio, editor, ImGui, SDL3, and OpenGL; with Diligent also `raytracer_rhi` + `EditorDiligent.cpp`; FBX release or debug MD libs to match the preset |
| `diligent_smoke` | under the Diligent binary dir | `raytracer_rhi` only (clear + opaque draw smoke) |
| `tests` | `build/bin/tests.exe` or `build-debug/bin/tests.exe` | core + doctest; OBJ, `.rtm`, DDS, catalog, PhysFS, terrain `.rtt`, tile stream; no Assimp/import/FBX, render, ImGui, SDL, window, or OpenGL |
| `cooker` | `build/bin/cooker.exe` or `build-debug/bin/cooker.exe` | cook + core; Assimp, meshoptimizer, stb_image cook, zip pack, no window |

`find_package` names in use: `enkiTS`, `spdlog`, `Tracy`, `doctest`, `EnTT`, `glm`, `SDL3`, `Bitsery`, `unofficial-sqlite3`, `Jolt`, `zstd`, `assimp`, `meshoptimizer`, `PhysFS`, `directxtex`, and `flatbuffers`. miniaudio has no CONFIG package; CMake `find_path`s `miniaudio.h` and exposes `miniaudio::miniaudio`. SDL3 is linked only into `raytracer` (and Diligent RHI). miniaudio is linked only into `raytracer_audio`. Assimp, meshoptimizer, and DirectXTex are linked only into `raytracer_cook`. zstd, PhysFS, and flatbuffers are PUBLIC on `raytracer_core`. Each exe POST_BUILD copies `$<TARGET_RUNTIME_DLLS>` next to the binary.

Imported `target_link_libraries` names later streams can use:

| Library | CMake target |
| --- | --- |
| bitsery | `Bitsery::bitsery` (already PUBLIC on `raytracer_core`) |
| SQLite | `unofficial::sqlite3::sqlite3` (already PUBLIC on `raytracer_core`) |
| Jolt | `Jolt::Jolt` (already PUBLIC on `raytracer_core`; `JPH_DOUBLE_PRECISION` is a PUBLIC define on core) |
| miniaudio | `miniaudio::miniaudio` (PRIVATE on `raytracer_audio`; include `<miniaudio.h>`) |
| zstd | `zstd::libzstd` (PUBLIC on `raytracer_core`) |
| PhysFS | `PhysFS::PhysFS` (PUBLIC on `raytracer_core`) |
| DirectXTex | `Microsoft::DirectXTex` (PRIVATE on `raytracer_cook`) |
| FlatBuffers | `flatbuffers::flatbuffers` (PUBLIC on `raytracer_core`; `flatc` generates `content_generated.h`) |
| Assimp | `assimp::assimp` (PRIVATE on `raytracer_cook`) |
| meshoptimizer | `meshoptimizer::meshoptimizer` (PUBLIC on `raytracer_core` for MeshShape welding; also PRIVATE on `raytracer_cook`) |

Jolt is the overlay port `overlay-ports/joltphysics` with feature `double-precision`, which passes CMake `-DDOUBLE_PRECISION=ON` and injects `JPH_DOUBLE_PRECISION` into `Jolt/Core/Core.h`. `jobs::joltJobSystem()` returns Jolt's `JobSystemThreadPool`. `jolt_play::World` (`JoltPlay.hpp` / `JoltPlay.cpp` on `raytracer_core`) is the play CharacterVirtual world; `stepPlay` drives it and calls `PhysicsSystem::Update`. Play uses miniaudio in `Sound.cpp` (`playGameSound`, `startMusic`). Bitsery encodes play commands and a `SimPlayBlob`. SQLite play-session files are `SimSave.hpp` (`raytracer-play.sqlite`).

`raytracer_core` includes `engine/include`. `raytracer_import` also includes the FBX SDK headers. The editor target adds `editor/include` and ImGui. MSVC flags: `/EHsc /bigobj`, `/W4` for engine and editor units, `/W0` for the stb image wrappers, vendored ImGui, and `Miniaudio.cpp`, plus `NOMINMAX` and `_CRT_SECURE_NO_WARNINGS`. Runtime is `MultiThreadedDLL` (Release) or `MultiThreadedDebugDLL` (Debug preset). Configure uses C++20. Existing `filesystem::u8path` use is kept with `_SILENCE_CXX20_U8PATH_DEPRECATION_WARNING`. FBX is linked only through `raytracer_import`. `tests` does not link that library.

`raytracer` links `SDL3::SDL3`, `user32`, `gdi32`, `opengl32`, `comdlg32`, `advapi32`, and the FBX SDK static libraries `libfbxsdk-md`, `libxml2-md`, and `zlib-md` from `lib/x64/release` or `lib/x64/debug` to match `CMAKE_BUILD_TYPE`. Without Diligent, the editor window and GL context come from SDL3 (`SDL_WINDOW_OPENGL`, 3.3 core). The still shader is GLSL 330. File dialogs still call the Win32 common-dialog APIs. vcpkg applocal copies runtime DLLs (`SDL3`, `enkiTS`, `spdlog`, `TracyClient`, `fmt`, `Jolt`, `sqlite3`, `zstd`, `physfs`, Assimp, …) next to the exes under each preset's `bin/` folder. DirectXTex rides with `cooker.exe` only.

Object and intermediate files go under `build/` (Release), `build-debug/` (Debug), or `build-diligent/` (Diligent). Executables and their DLLs land in each preset's `bin/` so builds do not overwrite each other. Run them from the project root (or with that as the working directory) so `assets/` and `vcpkg.json` resolve. Preset names: `windows-msvc`, `windows-msvc-debug`, `diligent` (`CMakePresets.json`).

```bat
build.bat
cmake --build --preset windows-msvc --target tests
build\bin\tests.exe
cmake --build --preset windows-msvc --target cooker
build\bin\cooker.exe assets/platform.obj cooked/platform.rtm
build\bin\cooker.exe image assets/some.png cooked/some.dds
build\bin\cooker.exe tables content/catalog.json cooked/catalog.bin
build\bin\cooker.exe pack cooked cooked/pak0.zip
build\bin\cooker.exe manifest cooked
build\bin\cooker.exe terrain cooked/hill.rtt
```

`build.bat` configures and builds the default preset targets (including `raytracer`; Ninja builds every target in the build files unless a target is named). Prefer an explicit `--target` when you only want one binary.

The root `Makefile` is deprecated. It is not the supported build path; keep it only as a historical object list. Use `build.bat` or the CMake preset.

Vendored `third_party/imgui` and `third_party/stb` stay compiled into the GL editor path the same way as before. Diligent ImGui comes from DiligentTools.

## Diligent path (primary interactive view)

```bat
cmake --preset diligent
cmake --build --preset diligent
build-diligent\bin\diligent_smoke.exe
build-diligent\bin\raytracer.exe
```

- **DiligentCore / DiligentTools / DiligentFX** come in via CMake FetchContent at tag `v2.5.6`. Tools/FX headers expect sibling folders named `DiligentCore` and `DiligentTools`; the root `CMakeLists.txt` creates those symlinks under the build `_deps` tree before adding the subdirectories.
- **Backend:** Direct3D 12 on Windows (`RAYTRACER_DILIGENT_D3D12`), Vulkan elsewhere (`RAYTRACER_DILIGENT_VULKAN`). OpenGL is not used for the main Diligent window.
- **SDL3** comes from vcpkg (`SDL3::SDL3`) and owns the window / native handle. No `SDL_WINDOW_OPENGL` on the Diligent shell.
- **Targets:** `raytracer_rhi` (Diligent RHI + FX), `diligent_smoke`, and `raytracer` (links RHI + `EditorDiligent.cpp` when Diligent is on). GL still / `--self-test` stay on `raytracer_render`.
- Run smoke and the editor from the repo root so `assets/` resolves.

Shaders for the Diligent path live in `engine/shaders/` (HLSL). GLSL Whitted shaders stay in `GpuShaders.cpp` / `GpuShaderTrace.cpp` for the still path.

Assets the demo needs sit in `assets/`: `tree-trunk.obj` and `tree-crown.obj`. Paths in a scene file are stored as written. Moving the exe without those files, or without the FBX runtime pieces the SDK requires, is not a supported package.

## Not built

- A folder a friend can run without this repo.
- Asset paths rewritten for that folder.
- A maintained build for another OS.
- An installer or a zip of the exe, assets, and runtime libraries.
